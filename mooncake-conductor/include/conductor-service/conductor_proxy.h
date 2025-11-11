#pragma once
#include "conductor_types.h"
#include "request_handler.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>
#include <glog/logging.h>
#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>

// used to notify the server to exit
std::atomic<bool> g_stop_flag{false};

namespace mooncake_conductor {

class ProxyServer {
   public:
    ProxyServer(const ProxyServerArgs& config);
    ~ProxyServer();

    void init_http_server();

    void start_server();

    void stop_server();

    ProxyServer(const ProxyServer&) = delete;
    ProxyServer& operator=(const ProxyServer&) = delete;

   private:
    uint16_t port_;
    std::string host_;
    std::unique_ptr<coro_http::coro_http_server> http_server_;
    std::unique_ptr<RequestHandler> request_handler_;
};

} // namespace mooncake-conductor