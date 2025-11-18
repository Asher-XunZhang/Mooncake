#pragma once

#include <string>
#include <vector>
#include <optional>

#include "mooncake_store_communication_layer.h"

namespace mooncake_conductor {
    struct BestPrefillResult {
        bool hit{false};
        size_t best_index{0};
        std::string best_key;
        std::string node_id;
    };

    class PrefillPlanner {
    public:
        PrefillPlanner();

        [[nodiscard]] BestPrefillResult
        find_best_prefill(const std::vector<std::string>& keys,
                        const std::vector<tl::expected<GetReplicaListResponse, ErrorCode>>& results) const;
    };
}  // namespace mooncake_conductor