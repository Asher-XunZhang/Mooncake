#pragma once
#include "types.h"
#include "request_handler.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <glog/logging.h>
#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>


namespace mooncake_conductor {

class ProxyServer {
   public:
    ProxyServer(ProxyServerArgs& config);
    ~ProxyServer();

    void init_http_server();
   private:
    coro_http::coro_http_server http_server_;
    RequestHandler request_handler_;
};

} // namespace mooncake-conductor