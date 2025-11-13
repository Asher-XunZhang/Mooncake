// mooncake_store_communication_layer.cpp
#include <async_simple/coro/SyncAwait.h>
#include <ylt/coro_io/client_pool.hpp>
#include <glog/logging.h>

#include "mooncake_store_communication_layer.h"
#include "master_client.h"
#include "rpc_service.h"

namespace mooncake_conductor {

MooncakeStoreCommunicationLayer::MooncakeStoreCommunicationLayer(const std::string& master_addr)
    : master_addr_(master_addr) {
    coro_io::client_pool<coro_rpc::coro_rpc_client>::pool_config pool_config{};
    
    client_pools_ = std::make_shared<coro_io::client_pools<coro_rpc::coro_rpc_client>>(pool_config);
    
    auto connect_result = Connect(master_addr_);
    if (connect_result != ErrorCode::OK) {
        LOG(ERROR) << "Failed to connect to master during construction: " 
                   << static_cast<int>(connect_result);
        throw std::runtime_error("MooncakeStoreCommunicationLayer initialization failed");
    }
}

MooncakeStoreCommunicationLayer::~MooncakeStoreCommunicationLayer() = default;


ErrorCode MooncakeStoreCommunicationLayer::Connect(const std::string& master_addr) {
    MutexLocker lock(&connect_mutex_);
    
    if (this->client_addr_param_ == master_addr) {
        auto pool = this->client_accessor_.GetClientPool();
        if (pool) {
            auto result = invoke_rpc<&::mooncake::WrappedMasterService::ServiceReady, void>();
            if (result && result.has_value()) {
                return ErrorCode::OK;
            }
        }
    }

    auto client_pool = client_pools_->at(master_addr);
    if (!client_pool) {
        LOG(ERROR) << "Failed to get client pool for " << master_addr;
        return ErrorCode::RPC_FAIL;
    }
    
    this->client_accessor_.SetClientPool(client_pool);
    this->client_addr_param_ = master_addr;

    return ErrorCode::OK;
}


template <auto ServiceMethod, typename ReturnType, typename... Args>
tl::expected<ReturnType, ErrorCode> MooncakeStoreCommunicationLayer::invoke_rpc(Args&&... args) {
    auto pool = client_accessor_.GetClientPool();
    if (!pool) {
        LOG(ERROR) << "Client pool not available";
        return tl::make_unexpected(ErrorCode::RPC_FAIL);
    }
    
    return async_simple::coro::syncAwait(
        [&]() -> async_simple::coro::Lazy<tl::expected<ReturnType, ErrorCode>> {
            auto ret = co_await pool->send_request(
                [&](coro_io::client_reuse_hint hint, 
                    coro_rpc::coro_rpc_client& client) {
                    return client.send_request<ServiceMethod>(std::forward<Args>(args)...);
                });
            
            if (!ret.has_value()) {
                LOG(ERROR) << "Client not available";
                co_return tl::make_unexpected(ErrorCode::RPC_FAIL);
            }
            
            auto result = co_await std::move(ret.value());
            if (!result) {
                LOG(ERROR) << "RPC call failed: " << result.error().msg;
                co_return tl::make_unexpected(ErrorCode::RPC_FAIL);
            }
            co_return result->result();
        }());
}


template <auto ServiceMethod, typename ResultType, typename... Args>
std::vector<tl::expected<ResultType, ErrorCode>> MooncakeStoreCommunicationLayer::invoke_batch_rpc(
    size_t input_size, Args&&... args) {
    auto pool = client_accessor_.GetClientPool();
    if (!pool) {
        LOG(ERROR) << "Client pool not available";
        return tl::make_unexpected(ErrorCode::RPC_FAIL);
    }

    return async_simple::coro::syncAwait(
        [&]() -> async_simple::coro::Lazy<
                  std::vector<tl::expected<ResultType, ErrorCode>>> {
            auto ret = co_await pool->send_request(
                [&](coro_io::client_reuse_hint,
                    coro_rpc::coro_rpc_client& client) {
                    return client.send_request<ServiceMethod>(
                        std::forward<Args>(args)...);
                });
            if (!ret.has_value()) {
                LOG(ERROR) << "Client not available";
                co_return std::vector<tl::expected<ResultType, ErrorCode>>(
                    input_size, tl::make_unexpected(ErrorCode::RPC_FAIL));
            }
            auto result = co_await std::move(ret.value());
            if (!result) {
                LOG(ERROR) << "Batch RPC call failed: " << result.error().msg;
                std::vector<tl::expected<ResultType, ErrorCode>> error_results;
                error_results.reserve(input_size);
                for (size_t i = 0; i < input_size; ++i) {
                    error_results.emplace_back(
                        tl::make_unexpected(ErrorCode::RPC_FAIL));
                }
                co_return error_results;
            }
            co_return result->result();
        }());
}


tl::expected<GetReplicaListResponse, ErrorCode> 
MooncakeStoreCommunicationLayer::GetReplicaList(const std::string& object_key) {
    auto result = invoke_rpc<&::mooncake::WrappedMasterService::GetReplicaList, 
                            GetReplicaListResponse>(object_key);
    return result;
}

std::vector<tl::expected<GetReplicaListResponse, ErrorCode>>
MooncakeStoreCommunicationLayer::BatchGetReplicaList(const std::vector<std::string>& object_keys) {
    auto result = invoke_batch_rpc<&::mooncake::WrappedMasterService::BatchGetReplicaList,
                                   GetReplicaListResponse>(object_keys.size(),
                                                           object_keys);
    return result;
}

} // namespace mooncake::conductor