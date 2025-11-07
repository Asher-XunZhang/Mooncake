#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <shared_mutex>

enum class NodeStatus {
    HEALTHY,
    UNHEALTHY, 
    DRAINING,
    UNKNOWN
};

struct PNodeInfo {
    std::string node_id;           // 节点唯一标识
    std::string endpoint;         // 服务端点地址
    int rank_id;                  // TP组内的rank ID
    int world_size;               // 分布式组大小
    std::string model_name;       // 加载的模型
    std::string engine_type;      // 推理引擎类型（vllm/sglang等）
    NodeStatus status;           // 节点状态
    int64_t last_heartbeat;      // 最后心跳时间戳
    double current_load;          // 当前负载系数（0-1）
    
    // 便捷方法
    bool isAvailable() const { return status == NodeStatus::HEALTHY; }
    std::string getLogicalIdentifier() const {
        return model_name + "@" + std::to_string(world_size) + "@" + std::to_string(rank_id);
    }
};

class NodeRegister {
private:
    std::unordered_map<std::string, PNodeInfo> nodes_;
    mutable std::shared_mutex mutex_;
    std::chrono::seconds heartbeat_timeout_{30};

public:
    // 节点注册与管理
    void registerNode(const PNodeInfo& node_info);
    void unregisterNode(const std::string& node_id);
    void updateNodeStatus(const std::string& node_id, NodeStatus status);
    void updateNodeMetrics(const std::string& node_id, double load_factor);
    
    // 节点查询
    std::vector<PNodeInfo> getAvailableNodes() const;
    std::vector<PNodeInfo> getNodesByRank(int rank_id) const;
    std::vector<PNodeInfo> getNodesByModel(const std::string& model_name) const;
    std::optional<PNodeInfo> getNode(const std::string& node_id) const;
    
    // 健康检查
    void removeStaleNodes();
    bool isNodeHealthy(const std::string& node_id) const;
    
    // 统计信息
    size_t getTotalNodeCount() const;
    size_t getAvailableNodeCount() const;
    std::vector<int> getAllRanks() const;

private:
    void cleanupExpiredNodes();
};