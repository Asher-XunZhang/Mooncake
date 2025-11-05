#pragma once
#include <unordered_map>
#include <atomic>

struct ScoringWeights {
    double cache_hit_weight{0.6};
    double load_penalty_weight{0.3};
    double slo_adherence_weight{0.1};
    double diversity_bonus_weight{0.05};
};

class DynamicWeightManager {
private:
    ScoringWeights current_weights_;
    mutable std::shared_mutex weights_mutex_;
    
    // 自适应调整策略
    std::unique_ptr<WeightAdaptationStrategy> adaptation_strategy_;
    std::chrono::steady_clock::time_point last_adaptation_time_;
    
    // 历史权重记录（用于回滚）
    std::vector<std::pair<std::chrono::steady_clock::time_point, ScoringWeights>> weight_history_;

public:
    DynamicWeightManager();
    
    // 获取当前权重配置
    ScoringWeights getCurrentWeights() const;
    
    // 动态调整权重基于系统状态
    void adaptWeightsBasedOnSystemState(const SystemMetrics& system_metrics);
    
    // 手动设置权重（用于测试或特定场景）
    void setWeights(const ScoringWeights& new_weights);
    
    // 回滚到之前的权重配置
    bool rollbackWeights(std::chrono::seconds time_back);
    
    // 注册权重变更回调
    void registerWeightChangeCallback(std::function<void(const ScoringWeights&)> callback);

private:
    bool shouldAdaptWeights(const SystemMetrics& metrics) const;
    ScoringWeights calculateAdaptedWeights(const SystemMetrics& metrics);
    void notifyWeightChange(const ScoringWeights& old_weights, const ScoringWeights& new_weights);
};