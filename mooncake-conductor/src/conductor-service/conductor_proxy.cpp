#include "conductor_proxy.h"
#include "request_handler.h"
#include "conductor_types.h"
#include "cli_parse.h"

#include <glog/logging.h>
#include <memory>
#include <string>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/easylog/record.hpp>

namespace mooncake_conductor {

ProxyServer::ProxyServer(const ProxyServerArgs& config)
    : port_(config.port),
      host_(config.host),
      http_server_(std::make_unique<coro_http::coro_http_server>(4, config.port)),
      request_handler_(std::make_unique<RequestHandler>("12", "34")) {
    init_http_server();
    // init proxy state
    std::unique_ptr<ProxyState> proxy_state_ = std::make_unique<ProxyState>(
        config.prefiller_instances, config.decoder_instances);
}

ProxyServer::~ProxyServer() {
    http_server_->stop();
}

void ProxyServer::init_http_server() {
//    curl -X POST http://localhost:8000/v1/completions \
//      -H "Content-Type: application/json" \
//      -d '{
//            "model": "your-model",
//            "prompt": "The quick brown fox jumps over the lazy dog",
//            "max_tokens": 16
//          }'

//  Or for chat completions:

//    curl -X POST http://localhost:8000/v1/chat/completions \
//      -H "Content-Type: application/json" \
//      -d '{
//            "model": "your-model",
//            "messages": [{"role": "user", "content": "Hello!"}],
//            "max_tokens": 16
//          }'
    using namespace coro_http;

    http_server_->set_http_handler<POST>(
        "/v1/completions", [&](coro_http_request& req, coro_http_response& resp) {
            auto request_body = req.get_queries();
            resp.add_header("Content-Type", "application/json");
            LOG(INFO) << "reievce request /v1/completions";
            auto result = request_handler_->handleRequest(request_body);
            // TODO 增加对于result解析等操作
            if (result.size()) {
                resp.set_status_and_content(status_type::ok, std::move(result));
            } else {
                resp.set_status_and_content(status_type::internal_server_error,
                                            "Failed to handle request.");
            }
        });

    http_server_->set_http_handler<POST>(
        "/v1/chat/completions",
        [&](coro_http_request& req, coro_http_response& resp) {
            const auto& request_body = req.get_queries();
            resp.add_header("Content-Type", "application/json");
            LOG(INFO) << "reievce request /v1/chat/completions";
            std::string result = request_handler_->handleRequest(request_body);
            if (result.size()) {
                resp.set_status_and_content(status_type::ok, std::move(result));
            } else {
                resp.set_status_and_content(status_type::internal_server_error,
                                            "Failed to handle request.");
            }
        });
}

void ProxyServer::start_server() {
    http_server_->async_start();
}

void ProxyServer::stop_server() {
    http_server_->stop();
}

}  // namespace mooncake_conductor


void signal_handler(int signal) {
    LOG(INFO) << "\nreceive signal: " << signal << ", start stoping the server...";
    g_stop_flag.store(true);
}

void StartProxyServer(
    const mooncake_conductor::ProxyServerArgs& config) {
    std::signal(SIGINT, signal_handler);  // Ctrl+C
    std::signal(SIGTERM, signal_handler); // kill
    auto server = std::make_unique<mooncake_conductor::ProxyServer>(config);
    server->start_server();
    LOG(INFO) << "Async Starting mooncake-conductor server on " << config.host << ":" << config.port;
    // wait 1s to let server start
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    LOG(INFO) << "\n  press Ctrl+C to stop server..  ";
    while (!g_stop_flag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG(INFO) << " server STOP finished. ";
}

int main(int argc, char* argv[]) {
    easylog::set_min_severity(easylog::Severity::WARN);
    mooncake_conductor::ProxyServerArgs config;
    try {
        config = parse_args(argc, argv);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error parsing arguments: " << e.what();
        return 1;
    }
    StartProxyServer(config);
    return 0;
}
//mooncake_conductor --port=8080 --prefiller_hosts="127.0.0.1,127.0.0.1" --prefiller_ports="8001,8002"
