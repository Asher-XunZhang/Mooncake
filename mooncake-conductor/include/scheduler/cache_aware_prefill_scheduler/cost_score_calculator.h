#pragma once
#include "node_hit_rate.h"
#include "node_metrics.h"
#include "dynamic_weight_manager.h"

struct CostScore {
    std::string node_id;
    double final_score;
    double cache_hit_component;
    double load_penalty_component;
    double slo_adherence_score;
    std::string scoring_strategy_used;
};

class CostScoreCalculator {
private:
    std::shared_ptr<DynamicWeightManager> weight_manager_;
    std::unordered_map<std::string, double> strategy_weights_;
    
    // 不同的成本计算策略
    std::unique_ptr<CacheAwareScoringStrategy> cache_strategy_;
    std::unique_ptr<LoadAwareScoringStrategy> load_strategy_;
    std::unique_ptr<SLOAwareScoringStrategy> slo_strategy_;

public:
    CostScoreCalculator(std::shared_ptr<DynamicWeightManager> weight_manager);
    
    // 计算节点的综合成本得分
    CostScore calculateNodeScore(const NodeHitRate& hit_rate, 
                                const NodeMetrics& metrics,
                                const SLORequirement& slo);
    
    // 批量计算多个节点的得分
    std::vector<CostScore> calculateBatchScores(
        const std::vector<NodeHitRate>& hit_rates,
        const std::unordered_map<std::string, NodeMetrics>& metrics,
        const SLORequirement& slo);
    
    // 应用动态权重调整
    void updateScoringWeights(const std::unordered_map<std::string, double>& new_weights);
    
    // 获取当前使用的权重配置
    ScoringWeights getCurrentWeights() const;

private:
    double calculateCacheComponent(const NodeHitRate& hit_rate);
    double calculateLoadComponent(const NodeMetrics& metrics);
    double calculateSLOComponent(const NodeMetrics& metrics, const SLORequirement& slo);
    double combineComponents(double cache_score, double load_penalty, double slo_score);
};