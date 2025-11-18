#include "conductor-service/prefill_planner.h"

#include <glog/logging.h>
#include <unordered_map>

namespace mooncake_conductor {

// Helper: collect all node endpoints for COMPLETE memory replicas.
static std::vector<std::string> extract_node_ids(const GetReplicaListResponse& resp) {
    std::vector<std::string> node_ids;
    for (const auto& rep_desc : resp.replicas) {
        if (rep_desc.status != ::mooncake::ReplicaStatus::COMPLETE) {
            continue;
        }
        if (rep_desc.is_memory_replica()) {
            const auto& mem_desc = rep_desc.get_memory_descriptor();
            if (!mem_desc.buffer_descriptors.empty()) {
                node_ids.push_back(mem_desc.buffer_descriptors.front().transport_endpoint_);
            }
        }
        // ignoring disk replicas for now
    }
    return node_ids;
}

PrefillPlanner::PrefillPlanner() {}

BestPrefillResult PrefillPlanner::find_best_prefill(
    const std::vector<std::string>& keys,
    const std::vector<tl::expected<GetReplicaListResponse, ErrorCode>>& results) const {
    BestPrefillResult out;

    if (keys.size() != results.size()) {
        LOG(WARNING) << "PrefillPlanner::find_best_prefill: keys/results size mismatch: "
                     << keys.size() << " vs " << results.size();
        return out;
    }
    if (keys.empty()) {
        return out;
    }

    const std::size_t N = keys.size();

    // Map: node_id -> vector<bool> of size N, where pos i indicates whether the node
    // has a replica for keys[i].
    std::unordered_map<std::string, std::vector<bool>> node_prefix_hit;

    // 1) Build coverage map from results
    for (std::size_t i = 0; i < N; ++i) {
        const auto& res = results[i];
        if (!res.has_value()) {
            // error or object not found for this key
            continue;
        }
        const auto& resp = res.value();
        if (resp.replicas.empty()) {
            continue;
        }

        auto node_ids = extract_node_ids(resp);
        for (const auto& node_id : node_ids) {
            auto& hits = node_prefix_hit[node_id];
            if (hits.empty()) {
                hits.assign(N, false);
            }
            hits[i] = true;
        }
    }

    // 2) For each node, compute the longest continuous prefix length starting from 0
    std::size_t best_length = 0;  // number of prefixes [0..L-1]
    std::string best_node;

    for (auto& [node_id, hits] : node_prefix_hit) {
        std::size_t length = 0;
        for (; length < N; ++length) {
            if (length >= hits.size() || !hits[length]) {
                break;
            }
        }
        if (length > best_length) {
            best_length = length;
            best_node = node_id;
        }
    }

    if (best_length == 0 || best_node.empty()) {
        // No node has even the first prefix
        return out;
    }

    // best_length is count; best index is best_length - 1
    const std::size_t best_index = best_length - 1;

    out.hit        = true;
    out.best_index = best_index;
    out.best_key   = keys[best_index];
    out.node_id    = best_node;
    return out;
}

} // namespace mooncake_conductor