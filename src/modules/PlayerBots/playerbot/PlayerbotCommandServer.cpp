#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotCommandServer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

INSTANTIATE_SINGLETON_1(PlayerbotCommandServer);

#include <boost/asio.hpp>

using boost::asio::ip::tcp;

namespace
{
    // These are also enforced while loading configuration. Keep the limits in
    // this translation unit as a second line of defense for malformed state.
    constexpr std::size_t COMMAND_SERVER_HARD_MAX_LINE_LENGTH = 64 * 1024;
    constexpr std::size_t COMMAND_SERVER_HARD_MAX_RESPONSE_LENGTH = 1024 * 1024;
    constexpr std::size_t COMMAND_SERVER_READ_BUFFER_SIZE = 1024;

    bool ConstantTimeEqual(const std::string& left, const std::string& right)
    {
        if (left.size() != right.size())
            return false;

        unsigned char difference = 0;
        for (std::size_t i = 0; i < left.size(); ++i)
            difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);

        return difference == 0;
    }

    class RemoteSession : public std::enable_shared_from_this<RemoteSession>
    {
    public:
        RemoteSession(std::shared_ptr<tcp::socket> socket,
                      std::shared_ptr<std::atomic<uint32>> activeClients,
                      std::string secret,
                      uint32 maxLineLength,
                      uint32 maxResponseLength,
                      uint32 readTimeout)
            : socket_(std::move(socket)),
              timer_(socket_->get_executor()),
              activeClients_(std::move(activeClients)),
              secret_(std::move(secret)),
              maxLineLength_(std::min<std::size_t>(maxLineLength, COMMAND_SERVER_HARD_MAX_LINE_LENGTH)),
              maxResponseLength_(std::min<std::size_t>(maxResponseLength, COMMAND_SERVER_HARD_MAX_RESPONSE_LENGTH)),
              readTimeout_(readTimeout),
              authenticated_(false),
              writeInProgress_(false),
              closed_(false)
        {
        }

        ~RemoteSession()
        {
            if (!closed_)
                activeClients_->fetch_sub(1, std::memory_order_relaxed);
        }

        void Start()
        {
            ArmReadTimeout();
            StartRead();
        }

    private:
        void ArmReadTimeout()
        {
            timer_.expires_from_now(boost::posix_time::seconds(readTimeout_));
            std::shared_ptr<RemoteSession> self = shared_from_this();
            timer_.async_wait([self](boost::system::error_code const& error)
            {
                if (!error)
                    self->Close();
            });
        }

        void StartRead()
        {
            if (closed_ || writeInProgress_)
                return;

            std::shared_ptr<RemoteSession> self = shared_from_this();
            socket_->async_read_some(boost::asio::buffer(readBuffer_),
                [self](boost::system::error_code const& error, std::size_t bytesRead)
                {
                    self->OnRead(error, bytesRead);
                });
        }

        void OnRead(boost::system::error_code const& error, std::size_t bytesRead)
        {
            if (closed_)
                return;

            if (error)
            {
                Close();
                return;
            }

            if (bytesRead == 0)
            {
                StartRead();
                return;
            }

            // The timer is an idle/read timeout: every successful read grants
            // the client another bounded interval to make progress.
            timer_.cancel();
            ArmReadTimeout();
            input_.append(readBuffer_.data(), bytesRead);

            ProcessInput();
            if (!closed_ && !writeInProgress_)
                StartRead();
        }

        void ProcessInput()
        {
            while (!closed_ && !writeInProgress_)
            {
                std::string::size_type newline = input_.find('\n');
                if (newline == std::string::npos)
                {
                    if (input_.size() > maxLineLength_)
                        Close();
                    return;
                }

                if (newline > maxLineLength_)
                {
                    Close();
                    return;
                }

                std::string line = input_.substr(0, newline);
                input_.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (!ProcessLine(line))
                    return;
            }
        }

        bool ProcessLine(std::string const& line)
        {
            if (!authenticated_)
            {
                // Authentication is a separate first line so existing command
                // syntax remains unchanged after the handshake.
                if (!ConstantTimeEqual(line, secret_))
                    Close();
                else
                    authenticated_ = true;
                return !closed_;
            }

            std::string response;
            try
            {
                response = sRandomPlayerbotMgr.HandleRemoteCommand(line);
            }
            catch (std::exception const& error)
            {
                sLog.outError("Playerbot command server request failed: %s", error.what());
                Close();
                return false;
            }

            // Include the protocol newline in the response limit.
            if (response.size() >= maxResponseLength_)
            {
                sLog.outError("Playerbot command server response exceeded its configured limit");
                Close();
                return false;
            }

            pendingResponse_ = std::move(response);
            pendingResponse_.push_back('\n');
            writeInProgress_ = true;

            std::shared_ptr<RemoteSession> self = shared_from_this();
            boost::asio::async_write(*socket_, boost::asio::buffer(pendingResponse_),
                [self](boost::system::error_code const& error, std::size_t)
                {
                    self->OnWrite(error);
                });
            return true;
        }

        void OnWrite(boost::system::error_code const& error)
        {
            if (closed_)
                return;

            writeInProgress_ = false;
            pendingResponse_.clear();
            if (error)
            {
                Close();
                return;
            }

            ProcessInput();
            if (!closed_ && !writeInProgress_)
                StartRead();
        }

        void Close()
        {
            if (closed_)
                return;

            closed_ = true;
            boost::system::error_code ignored;
            timer_.cancel(ignored);
            socket_->shutdown(tcp::socket::shutdown_both, ignored);
            socket_->close(ignored);
            activeClients_->fetch_sub(1, std::memory_order_relaxed);
        }

        std::shared_ptr<tcp::socket> socket_;
        boost::asio::deadline_timer timer_;
        std::shared_ptr<std::atomic<uint32>> activeClients_;
        std::string secret_;
        std::string input_;
        std::string pendingResponse_;
        std::array<char, COMMAND_SERVER_READ_BUFFER_SIZE> readBuffer_;
        std::size_t maxLineLength_;
        std::size_t maxResponseLength_;
        uint32 readTimeout_;
        bool authenticated_;
        bool writeInProgress_;
        bool closed_;
    };

    void RunServer(boost::asio::io_context& ioContext)
    {
        if (sPlayerbotAIConfig.commandServerPort <= 0 ||
            sPlayerbotAIConfig.commandServerPort > 65535)
        {
            sLog.outError("Playerbot command server disabled: invalid port %d", sPlayerbotAIConfig.commandServerPort);
            return;
        }

        if (sPlayerbotAIConfig.commandServerSecret.empty())
        {
            sLog.outError("Playerbot command server disabled: AiPlayerbot.CommandServerSecret/Token is not configured");
            return;
        }

        if (sPlayerbotAIConfig.commandServerSecret.find_first_of("\r\n") != std::string::npos ||
            sPlayerbotAIConfig.commandServerSecret.size() > sPlayerbotAIConfig.commandServerMaxLineLength)
        {
            sLog.outError("Playerbot command server disabled: configured secret is not a valid line");
            return;
        }

        boost::system::error_code addressError;
        boost::asio::ip::address bindAddress = boost::asio::ip::address::from_string(
            sPlayerbotAIConfig.commandServerAddress, addressError);
        if (addressError)
        {
            sLog.outError("Playerbot command server disabled: invalid bind address %s",
                sPlayerbotAIConfig.commandServerAddress.c_str());
            return;
        }

        tcp::endpoint endpoint(bindAddress, static_cast<unsigned short>(sPlayerbotAIConfig.commandServerPort));
        tcp::acceptor acceptor(ioContext);
        acceptor.open(endpoint.protocol());
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        sLog.outString("Starting Playerbot Command Server on %s:%d (max clients: %u)",
            sPlayerbotAIConfig.commandServerAddress.c_str(),
            sPlayerbotAIConfig.commandServerPort,
            sPlayerbotAIConfig.commandServerMaxClients);

        std::shared_ptr<std::atomic<uint32>> activeClients(new std::atomic<uint32>(0));
        std::function<void()> acceptNext;
        acceptNext = [&]()
        {
            std::shared_ptr<tcp::socket> socket(new tcp::socket(ioContext));
            acceptor.async_accept(*socket, [&, socket](boost::system::error_code const& error)
            {
                if (!error)
                {
                    uint32 currentClients = activeClients->load(std::memory_order_relaxed);
                    if (currentClients >= sPlayerbotAIConfig.commandServerMaxClients)
                    {
                        boost::system::error_code ignored;
                        socket->close(ignored);
                    }
                    else
                    {
                        activeClients->fetch_add(1, std::memory_order_relaxed);
                        std::shared_ptr<RemoteSession> session(new RemoteSession(
                            socket, activeClients, sPlayerbotAIConfig.commandServerSecret,
                            sPlayerbotAIConfig.commandServerMaxLineLength,
                            sPlayerbotAIConfig.commandServerMaxResponseLength,
                            sPlayerbotAIConfig.commandServerReadTimeout));
                        session->Start();
                    }
                }

                if (acceptor.is_open())
                    acceptNext();
            });
        };

        acceptNext();
        ioContext.run();
    }

    void Run()
    {
        if (!sPlayerbotAIConfig.commandServerPort)
            return;

        try
        {
            boost::asio::io_context ioContext;
            RunServer(ioContext);
        }
        catch (std::exception const& error)
        {
            sLog.outError("Playerbot command server stopped: %s", error.what());
        }
    }
}

void PlayerbotCommandServer::Start()
{
    std::thread serverThread(Run);
    serverThread.detach();
}
