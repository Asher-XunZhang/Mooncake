#include "vllm_endpoint_adapter.h"
#include "adapter_factory.h"

#include <nlohmann/json.hpp>
#include <glog/logging.h>

#include <sstream>
#include <vector>
#include <string>
#include <cctype>

namespace mooncake_conductor {
namespace nl = nlohmann;

namespace {
    bool isPrometheusMetricsImpl(std::string_view text) {
        return text.find("vllm:") != std::string_view::npos || 
               text.find("vllm_") != std::string_view::npos;
    }

    LoadMetrics parsePrometheusMetricsImpl(std::string_view metrics_text) {
        LoadMetrics metrics;
        std::string tmp_str(metrics_text);
        std::istringstream stream(tmp_str);
        std::string line;
        
        while (std::getline(stream, line)) {
            // 跳过注释和空行
            if (line.empty() || line.front() == '#') continue;
            
            if (line.find("vllm:gpu_utilization") != std::string::npos ||
                line.find("vllm_gpu_utilization") != std::string::npos) {
                // 从行尾提取数值
                size_t last_space = line.find_last_of(" \t");
                if (last_space != std::string::npos) {
                    std::string value_str = line.substr(last_space + 1);
                    // 移除可能的尾随字符
                    value_str.erase(std::find_if(value_str.rbegin(), value_str.rend(), 
                        [](unsigned char ch) { return !std::isspace(ch) && ch != '\n'; }).base(), 
                        value_str.end());
                    
                    try {
                        double value = std::stod(value_str);
                        metrics.gpu_utilization = value / 100.0;
                        break; // 找到第一个匹配项即可
                    } catch (...) {
                        // 忽略解析错误
                    }
                }
            }
        }
        return metrics;
    }
} // namespace

// 实现所有声明的函数
HttpRequest VLLMEndpointAdapter::createTokenizationRequestImpl(
    std::string_view prompt, 
    std::string_view endpoint
) const {
    nl::json request_body = {
        {"text", std::string(prompt)},
        {"add_special_tokens", false}
    };
    return createPostRequest(endpoint, request_body.dump());
}

TokenizationResult VLLMEndpointAdapter::parseTokenizationResponseImpl(
    std::string_view raw_response
) const {
    TokenizationResult result;
    try {
        auto j = nl::json::parse(raw_response);
        if (j.contains("tokens") && j["tokens"].is_array()) {
            result.token_ids = j["tokens"].get<std::vector<int>>();
            result.token_count = result.token_ids.size();
        }
        result.model_name = j.value("model", "unknown");
        result.truncated = j.value("truncated", false);
        if (j.contains("error")) {
            result.error_message = j["error"].get<std::string>();
        }
    } catch (const nl::json::exception& e) {
        result.error_message = "JSON parse error: " + std::string(e.what());
    }
    return result;
}

HttpRequest VLLMEndpointAdapter::createConfigRequestImpl(
    std::string_view endpoint
) const {
    return createGetRequest(endpoint);
}

EngineConfig VLLMEndpointAdapter::parseConfigResponseImpl(
    std::string_view raw_response
) const {
    EngineConfig config;
    try {
        auto j = nl::json::parse(raw_response);
        if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
            auto& model = j["data"][0];
            config.model_name = model.value("id", "unknown");
            config.max_sequence_length = model.value("max_model_len", 4096);
            config.dtype = model.value("dtype", "float16");
            config.block_size = model.value("block_size", 16);
        }
    } catch (const nl::json::exception& e) {
        LOG(ERROR) << "[VLLM] Config parse error: " << e.what();
    }
    return config;
}

HttpRequest VLLMEndpointAdapter::createMetricsRequestImpl(
    std::string_view endpoint
) const {
    return createGetRequest(endpoint);
}

LoadMetrics VLLMEndpointAdapter::parseMetricsResponseImpl(
    std::string_view raw_response
) const {
    LoadMetrics metrics;
    try {
        if (isPrometheusMetricsImpl(raw_response)) {
            metrics = parsePrometheusMetricsImpl(raw_response);
        } else {
            auto j = nl::json::parse(raw_response);
            if (j.contains("gpu_util")) {
                metrics.gpu_utilization = j["gpu_util"].get<double>() / 100.0;
            }
        }
    } catch (...) {
        // Keep default values on error
    }
    metrics.is_healthy = (metrics.gpu_utilization >= 0 && metrics.gpu_utilization <= 1.0);
    return metrics;
}

HttpRequest VLLMEndpointAdapter::createHealthRequestImpl(
    std::string_view endpoint
) const {
    return createGetRequest(endpoint);
}

bool VLLMEndpointAdapter::parseHealthResponseImpl(
    std::string_view raw_response
) const {
    try {
        auto j = nl::json::parse(raw_response);
        return j.value("status", "") == "healthy" || j.value("healthy", false);
    } catch (...) {
        return false;
    }
}

std::string VLLMEndpointAdapter::buildConfigEndpointImpl(std::string_view base_url) const {
    return buildUrl(base_url, "/v1/models");
}

std::string VLLMEndpointAdapter::buildMetricsEndpointImpl(std::string_view base_url) const {
    return buildUrl(base_url, "/metrics");
}

std::string VLLMEndpointAdapter::buildTokenizeEndpointImpl(std::string_view base_url) const {
    return buildUrl(base_url, "/v1/tokenize");
}

std::string VLLMEndpointAdapter::buildHealthEndpoint(std::string_view base_url) const {
    return buildUrl(base_url, "/health");
}

bool VLLMEndpointAdapter::isPrometheusMetrics(std::string_view text) {
    return isPrometheusMetricsImpl(text);
}

LoadMetrics VLLMEndpointAdapter::parsePrometheusMetrics(std::string_view metrics_text) {
    return parsePrometheusMetricsImpl(metrics_text);
}

void VLLMEndpointAdapter::registerAdapter() {
    registerAdapterImpl<VLLMEndpointAdapter>("vllm");
}

namespace {
    [[maybe_unused]] bool vllm_registered = [] {
        VLLMEndpointAdapter::registerAdapter();
        return true;
    }();
} // namespace

} // namespace mooncake_conductor