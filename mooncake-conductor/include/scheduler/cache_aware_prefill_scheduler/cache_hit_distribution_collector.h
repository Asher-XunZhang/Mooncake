#pragma once
#include "logical_key_generator.h"
#include "physical_key_generator.h"
#include "api_endpoint_adapter.h"
#include "mooncake_store_client.h"
#include "node_registry.h"

struct NodeHitRate {
    std::string node_id;
    int rank_id;
    double hit_rate;
    double load_adjusted_score;
    std::vector<PNodeInfo> candidate_nodes;
};

class CacheHitDistributionCollector {
private:
    std::shared_ptr<APIEndpointAdapter> endpoint_adapter_;
    std::shared_ptr<PhysicalKeyGeneratorFactory> key_generator_factory_;
    std::shared_ptr<MooncakeStoreClient> mooncake_client_;
    std::shared_ptr<NodeRegistry> node_registry_;
    
    // 性能优化组件
    std::shared_ptr<BloomFilterCache> bloom_filter_;
    std::shared_ptr<BatchQueryOptimizer> batch_optimizer_;
    std::shared_ptr<LocalResultCache> local_cache_;

public:
    CacheHitDistributionCollector(
        std::shared_ptr<APIEndpointAdapter> adapter,
        std::shared_ptr<PhysicalKeyGeneratorFactory> factory,
        std::shared_ptr<MooncakeStoreClient> client,
        std::shared_ptr<NodeRegistry> registry);
    
    // 核心方法：计算请求在所有候选节点上的缓存命中率分布
    std::vector<NodeHitRate> calculateHitDistribution(const UserRequest& request);
    
    // 批量计算多个请求的命中率分布
    std::unordered_map<std::string, std::vector<NodeHitRate>> 
    calculateBatchHitDistribution(const std::vector<UserRequest>& requests);
    
    // 获取详细的分布信息（用于监控和调试）
    CacheDistributionDetails getDetailedDistribution(const std::string& request_id) const;

private:
    LogicalCacheKey generateLogicalKey(const UserRequest& request);
    std::vector<std::string> generatePhysicalKeys(const LogicalCacheKey& logical_key, 
                                                 const PNodeInfo& target_node);
    double calculateNodeHitRate(const std::vector<std::string>& physical_keys, 
                               const std::string& node_id);
    std::vector<PNodeInfo> getCandidateNodes(const UserRequest& request);
};