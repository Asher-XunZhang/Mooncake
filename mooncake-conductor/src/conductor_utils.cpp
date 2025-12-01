// conductor_utils.cpp
#include "conductor_utils.h"

#include <glog/logging.h>

#include <cstdlib>
#include <charconv>
#include <string>
#include <system_error>
#include <charconv>
#include <array>
#include <format>

namespace mooncake_conductor {

bool safe_env_to_positive_int(const char* env_name, int& out_value) {

    const char* env_value = std::getenv(env_name);
    if (env_value == nullptr) {
        LOG(WARNING) << "警告：环境变量 '" << env_name << "' 未设置。";
        return false;
    }

    std::string env_str(env_value);
    if (env_str.empty()) {
        LOG(ERROR) << "错误：环境变量 '" << env_name << "' 的值为空。";
        return false;
    }

    int num;
    auto [ptr, ec] = std::from_chars(env_str.data(), env_str.data() + env_str.size(), num);

    if (ec != std::errc()) {
        if (ec == std::errc::invalid_argument) {
            LOG(ERROR) << "错误：环境变量 '" << env_name << "' 的值 '" << env_str << "' 不是有效的数字格式。";
        } else if (ec == std::errc::result_out_of_range) {
            LOG(ERROR) << "错误：环境变量 '" << env_name << "' 的值 '" << env_str << "' 超出 int 类型的表示范围。";
        }
        return false;
    }

    if (ptr != env_str.data() + env_str.size()) {
        LOG(ERROR) << "错误：环境变量 '" << env_name << "' 的值 '" << env_str << "' 包含非数字后缀。";
        return false;
    }

    if (num <= 0) {
        LOG(ERROR) << "错误：环境变量 '" << env_name << "' 的值 " << num << " 不是正整数（必须大于0）。";
        return false;
    }

    out_value = num;
    return true;
}

std::string uuid_to_str(const std::pair<uint64_t, uint64_t>& my_pair) {
    std::array<char, 50> buffer1{}; // 预留足够空间存储两个uint64_t的字符串形式
    std::array<char, 50> buffer2{};
    
    auto [ptr1, ec1] = std::to_chars(buffer1.data(), buffer1.data() + buffer1.size(), my_pair.first);
    auto [ptr2, ec2] = std::to_chars(buffer2.data(), buffer2.data() + buffer2.size(), my_pair.second);
    
    if (ec1 == std::errc() && ec2 == std::errc()) {
        return std::string(buffer1.data(), ptr1) + std::string(buffer2.data(), ptr2);
    }
    // 如果转换失败，回退到to_string
    return std::to_string(my_pair.first) + std::to_string(my_pair.second);
}

Timer::Timer(std::string_view msg, std::source_location loc)
    : start_time(std::chrono::steady_clock::now())
    , message(msg)
    , location(loc) 
{
    if (!message.empty()) {
        LOG(INFO) << std::format("[{}:{}] {} - started\n", 
                               location.file_name(), location.line(), message);
    }
}

Timer::~Timer() {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    if (!message.empty()) {
        LOG(INFO) << std::format("[{}:{}] {} - completed in {}ms\n", 
                               location.file_name(), location.line(), 
                               message, duration.count());
    } else {
        LOG(INFO) << std::format("Operation completed in {}ms\n", duration.count());
    }
}

auto Timer::elapsed() const -> int64_t {
    auto current_time = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        current_time - start_time).count();
}

void Timer::reset() {
    start_time = std::chrono::steady_clock::now();
}

auto Timer::create(std::string_view msg) -> Timer {
    return Timer(msg);
}

}