#include "ApiServer.hpp"
#include "BaseController.hpp"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"

using namespace httplib;

namespace HttpApi
{
    namespace
    {
        constexpr std::size_t MaxPayloadLength = 16 * 1024 * 1024;
        constexpr std::size_t MaxKeepAliveRequests = 100;
        constexpr time_t KeepAliveTimeoutSeconds = 15;
        constexpr time_t ReadTimeoutSeconds = 15;
        constexpr time_t WriteTimeoutSeconds = 30;
    }

    void ApiServer::Start(const std::string& address, int port)
    {
        std::string certPath = sConfig.GetStringDefault("Api.CertificatePath", "turtle.cer");
        std::string privateKeyPath = sConfig.GetStringDefault("ApiPrivateKeyPath", "turtle.pkey");

        sLog.out(LOG_API, string_format("Starting HTTP API server on {}:{}.", address, port).c_str());

        _server = std::make_unique<SSLServer>(certPath.c_str(), privateKeyPath.c_str());

        _server->set_payload_max_length(MaxPayloadLength);
        _server->set_read_timeout(ReadTimeoutSeconds);
        _server->set_write_timeout(WriteTimeoutSeconds);
        _server->set_keep_alive_max_count(MaxKeepAliveRequests);
        _server->set_keep_alive_timeout(KeepAliveTimeoutSeconds);
        _server->set_default_headers({
            { "Cache-Control", "no-store" },
            { "X-Content-Type-Options", "nosniff" }
        });

        _server->set_error_handler([](const auto& req, auto& res) {
            (void)req;
            if (res.status < 400)
                res.status = 500;

            res.set_content("Request failed.", "text/plain");
        });

        _server->set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
            (void)ep;
            sLog.out(LOG_API, "HTTP API request handler failed for %s %s.", req.remote_addr.c_str(), req.path.c_str());
            res.status = 500;
            res.set_content("Internal server error.", "text/plain");
        });

        _server->set_logger([](const Request& req, const Response& res) {
            sLog.out(LOG_API, "HTTP API request from %s: %s %s -> %d.",
                req.remote_addr.c_str(), req.method.c_str(), req.path.c_str(), res.status);
        });

        BaseController::RegisterAll(_server.get());

        _running = true;
        _listenThread = std::thread([this, address, port]()
        {
                mysql_thread_init(); // not really good but eh
                while (_running)
                {
                    if (!_server->listen(address, port))
                    {
                        _running = false;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(33)); // 30 FPS
                }
                mysql_thread_end();
        });

    }

    void ApiServer::Stop()
    {
        _running = false;
        if (_server && _server->is_running())
            _server->stop();
    }
}
