// conductor_utils.h
#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <string_view>
#include <source_location>

namespace mooncake_conductor {

bool safe_env_to_positive_int(const char*, int&);
std::string uuid_to_str(const std::pair<uint64_t, uint64_t>&);

class Timer {
private:
    std::chrono::steady_clock::time_point start_time;
    std::string message;
    std::source_location location;
public:
    explicit Timer(std::string_view msg = "", 
                  std::source_location loc = std::source_location::current());
    
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;
    
    ~Timer();
    
    [[nodiscard]] auto elapsed() const -> int64_t;
    void reset();
    static auto create(std::string_view msg) -> Timer;
};
}