#include "conductor_proxy.h"
#include "request_handler.h"

#include <glog/logging.h>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_http/coro_http_server.hpp>


namespace mooncake_conductor {

ProxyServer::ProxyServer(ProxyServerArgs& config)
    : http_server_(4, config.port),
      request_handler_(std::to_string(config.max_retries), std::to_string(config.retry_delay)) {
  init_http_server();
}

ProxyServer::~ProxyServer() {
    http_server_.stop();
}

//    curl -X POST http://localhost:9000/v1/completions \
//      -H "Content-Type: application/json" \
//      -d '{
//            "model": "your-model",
//            "prompt": "The quick brown fox jumps over the lazy dog",
//            "max_tokens": 16
//          }'

//  Or for chat completions:

//    curl -X POST http://localhost:9000/v1/chat/completions \
//      -H "Content-Type: application/json" \
//      -d '{
//            "model": "your-model",
//            "messages": [{"role": "user", "content": "Hello!"}],
//            "max_tokens": 16
//          }'
void ProxyServer::init_http_server() {
    using namespace coro_http;

    http_server_.set_http_handler<POST>(
        "/v1/completions", [&](coro_http_request& req, coro_http_response& resp) {
            auto request_body = req.get_queries();
            resp.add_header("Content-Type", "application/json");

            auto result = request_handler_.handleRequest(request_body);
            // TODO 增加对于result解析等操作
            if (result.size()) {
                resp.set_status_and_content(status_type::ok, std::move(result));
            } else {
                resp.set_status_and_content(status_type::internal_server_error,
                                            "Failed to handle request.");
            }
        });

    http_server_.set_http_handler<POST>(
        "/v1/chat/completions",
        [&](coro_http_request& req, coro_http_response& resp) {
            const auto& request_body = req.get_queries();
            resp.add_header("Content-Type", "application/json");
            std::string result = request_handler_.handleRequest(request_body);
            if (result.size()) {
                resp.set_status_and_content(status_type::ok, std::move(result));
            } else {
                resp.set_status_and_content(status_type::internal_server_error,
                                            "Failed to handle request.");
            }
        });
    
    http_server_.async_start();
    std::cout << "HTTP mooncake_conductor server started on port: "
              << http_server_.port();
}

}  // namespace mooncake_conductor

