#pragma once
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <stdexcept>

// A separate shared bounded lane: map owners may wait here, never on jobs
// queued behind themselves in the map-owner pool. Zero workers runs inline.
// Gameplay may use a dedicated lane only with a joined whole-map ownership
// handoff: never submit independent actors from the same map concurrently.
// Destruction drains accepted jobs. Callers must join before releasing captures.
class MapTaskExecutor
{
public:
    explicit MapTaskExecutor(size_t count, std::function<void()> enter = {}, std::function<void()> leave = {})
    {
        try
        {
            for (size_t n = 0; n < count; ++n)
            {
                std::promise<void> initialized;
                auto started = initialized.get_future();
                workers.emplace_back([this, enter, leave, initialized = std::move(initialized)]() mutable {
                    try { if (enter) enter(); initialized.set_value(); }
                    catch (...) { initialized.set_exception(std::current_exception()); return; }
                    struct Exit
                    {
                        std::function<void()> leave;
                        ~Exit() { if (leave) leave(); }
                    } exit{leave};
                    for (;;)
                    {
                        std::packaged_task<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(mutex);
                            ready.wait(lock, [this] { return stopping || !queue.empty(); });
                            if (queue.empty()) return;
                            task = std::move(queue.front());
                            queue.pop_front();
                        }
                        task(); // packaged_task propagates exceptions to the owner
                    }
                });
                started.get(); // propagate initialization failure; Stop joins earlier workers
            }
        }
        catch (...) { Stop(); throw; }
    }
    ~MapTaskExecutor() { Stop(); }
    MapTaskExecutor(MapTaskExecutor const&) = delete;
    MapTaskExecutor& operator=(MapTaskExecutor const&) = delete;
    size_t Size() const { return workers.size(); }
    template<class F> std::future<void> Submit(F&& function)
    {
        std::packaged_task<void()> task(std::forward<F>(function));
        auto result = task.get_future();
        if (workers.empty()) { task(); return result; }
        bool inlineWork = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) throw std::logic_error("MapTaskExecutor stopped");
            // Bound aggregate pressure across any number of maps. Disjoint
            // fallback on the submitting owner cannot deadlock other owners.
            inlineWork = queue.size() >= 256;
            if (!inlineWork) queue.emplace_back(std::move(task));
        }
        if (inlineWork) task(); else ready.notify_one();
        return result;
    }
private:
    void Stop()
    {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        ready.notify_all();
        for (auto& worker : workers) if (worker.joinable()) worker.join();
    }
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::packaged_task<void()>> queue;
    std::vector<std::thread> workers;
    bool stopping = false;
};

// Even an exception or a timeout must NOT let captured map/grid data escape.
class MapTaskJoin
{
public:
    std::vector<std::future<void>> tasks;
    ~MapTaskJoin() { for (auto& task : tasks) if (task.valid()) task.wait(); }
    void Get()
    {
        std::exception_ptr failure;
        for (auto& task : tasks)
            try { if (task.valid()) task.get(); } catch (...) { if (!failure) failure = std::current_exception(); }
        if (failure) std::rethrow_exception(failure);
    }
};
