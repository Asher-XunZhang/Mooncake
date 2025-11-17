#include <glog/logging.h>

#include <cstdlib>
#include <charconv>
#include <string>
#include <system_error>

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

}