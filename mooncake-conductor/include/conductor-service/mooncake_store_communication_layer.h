// mooncake_store_communication_layer.h
#pragma once

#include "rpc_types.h"

#include <ylt/util/tl/expected.hpp>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_io/client_pool.hpp>
#include <async_simple/coro/Mutex.h>

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>

using ErrorCode = ::mooncake::ErrorCode;
using GetReplicaListResponse = ::mooncake::GetReplicaListResponse; 

namespace mooncake_conductor {

static const std::string kDefaultMasterAddress = "localhost:50051";

class MooncakeStoreCommunicationLayer {
public:
    explicit MooncakeStoreCommunicationLayer(const std::string& master_addr = kDefaultMasterAddress);
    ~MooncakeStoreCommunicationLayer();

    MooncakeStoreCommunicationLayer(const MooncakeStoreCommunicationLayer&) = delete;
    MooncakeStoreCommunicationLayer& operator=(const MooncakeStoreCommunicationLayer&) = delete;

    [[nodiscard]] tl::expected<GetReplicaListResponse, ErrorCode>
    GetReplicaList(const std::string& object_key);

private:
    [[nodiscard]] ErrorCode Connect(const std::string& master_addr = kDefaultMasterAddress);
    
    class RpcClientAccessor {
    public:
        void SetClientPool(std::shared_ptr<coro_io::client_pool<coro_rpc::coro_rpc_client>> client_pool) {
            std::lock_guard<std::shared_mutex> lock(client_mutex_);
            client_pool_ = client_pool;
        }

        std::shared_ptr<coro_io::client_pool<coro_rpc::coro_rpc_client>>
        GetClientPool() {
            std::shared_lock<std::shared_mutex> lock(client_mutex_);
            return client_pool_;
        }

    private:
        mutable std::shared_mutex client_mutex_;
        std::shared_ptr<coro_io::client_pool<coro_rpc::coro_rpc_client>> client_pool_;
    };

    template <auto ServiceMethod, typename ReturnType, typename... Args>
    [[nodiscard]] tl::expected<ReturnType, ErrorCode> invoke_rpc(Args&&... args);

    std::string master_addr_;
    std::shared_ptr<coro_io::client_pools<coro_rpc::coro_rpc_client>> client_pools_;
    RpcClientAccessor client_accessor_;
    
    mutable Mutex connect_mutex_;
    std::string client_addr_param_ GUARDED_BY(connect_mutex_);
};

} // namespace mooncake_conductor