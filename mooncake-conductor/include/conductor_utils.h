// conductor_utils.h
#pragma once

#include <string>

namespace mooncake_conductor {
    bool safe_env_to_positive_int(const char*, int&);
    std::string uuid_to_str(const std::pair<uint64_t, uint64_t>&);
}