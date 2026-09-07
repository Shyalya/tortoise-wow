// Real std::future lifecycles and the production provisioning/save loops.
// Account/database/player services are stand-ins; this does not start a realm.
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using uint32 = unsigned int;
struct Config
{
    uint32 randomBotAccountCount = 0;
    std::string randomBotAccountPrefix = "bot";
    bool randomBotRandomPassword = false;
} sPlayerbotAIConfig;
struct Database
{
    uint32 existing = 0;
    bool PQuery(const char*, const char* name) const
    {
        return std::stoul(std::string(name).substr(3)) < existing;
    }
} LoginDatabase;
struct AccountMgr
{
    std::atomic<uint32> created{0};
    void CreateAccount(const std::string&, const std::string&) { ++created; }
} sAccountMgr;
struct Log
{
    template<class... Args> void outDebug(const char*, Args...) {}
} sLog;
struct BarGoLink
{
    explicit BarGoLink(size_t) {}
    void step() {}
};
int urand(int min, int) { return min; }

std::atomic<uint32> saving{0}, peakSaving{0};
struct Player
{
    std::atomic<uint32> saves{0};
    void SaveToDB()
    {
        const uint32 concurrent = ++saving;
        uint32 peak = peakSaving.load();
        while (peak < concurrent && !peakSaving.compare_exchange_weak(peak, concurrent)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++saves;
        --saving;
    }
};
struct Accessor
{
    std::map<uint32, Player*> players;
    const std::map<uint32, Player*>& GetPlayers() const { return players; }
} sObjectAccessor;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Provision(uint32 missing, uint32 existing, uint32 characters)
{
    sPlayerbotAIConfig.randomBotAccountCount = missing + existing;
    LoginDatabase.existing = existing;
    sAccountMgr.created = 0;
    peakSaving = 0;
    std::vector<std::unique_ptr<Player>> players;
    sObjectAccessor.players.clear();
    for (uint32 i = 0; i < characters; ++i)
    {
        players.emplace_back(std::make_unique<Player>());
        sObjectAccessor.players.emplace(i, players.back().get());
    }
    int totalAccCount = sPlayerbotAIConfig.randomBotAccountCount;
#include "BotAccountCreation.inc"
    Require(sAccountMgr.created == missing, "all missing accounts must finish before character saves");
#include "BotCreationSaves.inc"
    Require(saving == 0, "no save may outlive player/session cleanup");
    for (const auto& player : players)
        Require(player->saves == 1, "each new character must be saved exactly once in this phase");
    Require(peakSaving <= 8, "character saves must have bounded concurrency");
}

int main()
{
    try
    {
        // Non-multiples of eight retain consumed account futures in the broken
        // implementation. Include character-only growth and unchanged restarts.
        for (uint32 missing : {0u, 1u, 7u, 8u, 9u, 15u, 16u, 17u, 1000u, 1001u})
            for (uint32 characters : {0u, 1u, 7u, 9u, 33u})
                Provision(missing, 3, characters);
        Provision(0, 1000, 0);
        Provision(0, 1000, 135);
        std::cout << "Bot provisioning lifecycle passed (52 scenarios).\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Bot provisioning lifecycle failed: " << error.what() << '\n';
        return 1;
    }
}
