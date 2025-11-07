#pragma once

#include "conductor_types.h"
#include <vector>
#include <string>

std::vector<std::string> split_str_list(const std::string& str, char delimiter);

std::vector<int> parse_int_list(const std::string& str, char delimiter);

mooncake_conductor::ProxyServerArgs parse_args(int argc, char** argv);