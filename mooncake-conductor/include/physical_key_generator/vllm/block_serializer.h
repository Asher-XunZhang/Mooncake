#pragma once

#include "python_pickle_serializer.h"

#include <vector>
#include <cstdint>
#include <optional>

namespace mooncake_conductor {

class BlockSerializer {
private:
    BoostPythonPickleSerializer serializer;
    std::vector<uint8_t> none_hash;
    
public:
    BlockSerializer() : none_hash(32, 0) {}
    
    static std::string to_hex(const std::vector<uint8_t>& data);

    std::vector<uint8_t> serialize_block(
        const std::optional<std::vector<uint8_t>>& parent_block_hash,
        const std::vector<int64_t>& curr_block_token_ids);

    std::vector<std::vector<uint8_t>> serialize_blocks(
        const std::vector<int64_t>& all_token_ids,
        size_t block_size);
};

}