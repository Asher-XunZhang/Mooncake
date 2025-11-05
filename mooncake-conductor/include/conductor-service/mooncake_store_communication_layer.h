// mooncake_store_communication_layer.h
#pragma once
#include "key_distribution.h"
#include "cache_operation_result.h"
#include <grpcpp/grpcpp.h>

class MooncakeStoreCommunicationLayer {
private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<MooncakeStore::Stub> stub_;
    std::chrono::milliseconds timeout_;
    RetryPolicy retry_policy_;
    
    // 连接池和性能优化
    std::shared_ptr<ConnectionPool> connection_pool_;
    std::shared_ptr<RequestBatcher> request_batcher_;

public:
    MooncakeStoreCommunicationLayer(const std::string& server_address,
                                   std::chrono::milliseconds timeout = std::chrono::seconds(30));
    
    // 批量查询键分布
    std::vector<KeyDistribution> batchGetKeyDistribution(
        const std::vector<std::string>& physical_keys,
        const std::string& target_node = "");
    
    // 单个键查询（兼容性接口）
    KeyDistribution getKeyDistribution(const std::string& physical_key,
                                      const std::string& target_node = "");
    
    // 缓存操作
    CacheOperationResult putCache(const std::string& key, 
                                 const std::vector<uint8_t>& data,
                                 std::chrono::seconds ttl = std::chrono::hours(24));
    
    CacheOperationResult getCache(const std::string& key);
    
    CacheOperationResult deleteCache(const std::string& key);
    
    // 集群管理操作
    ClusterInfo getClusterInfo();
    bool migrateCache(const std::string& source_key, const std::string& target_node);
    
    // 连接管理
    bool isConnected() const;
    void reconnect();
    void setTimeout(std::chrono::milliseconds timeout);
    
    // 性能统计
    CommunicationStats getCommunicationStats() const;

private:
    grpc::Status executeWithRetry(std::function<grpc::Status()> operation);
    std::vector<KeyDistribution> processBatchResponse(const BatchDistributionResponse& response);
    void updatePerformanceMetrics(const grpc::Status& status, std::chrono::milliseconds duration);
};