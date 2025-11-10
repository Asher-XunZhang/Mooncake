#pragma once
#include "api_endpoint_adapter.h"
#include "conductor_types.h"
#include "adapter_factory.h"
#include <sstream>
#include <algorithm>
#include <optional>

namespace mooncake_conductor {

class VLLMEndpointAdapter : public APIEndpointAdapter<VLLMEndpointAdapter> {
    public:
        // ============ 框架标识 ============
        static constexpr std::string_view getFrameworkTypeImpl() {
            return "vllm";
        }

        // ============ Tokenization实现 ============
        HttpRequest createTokenizationRequestImpl(
                std::string_view prompt, std::string_view endpoint) const {

            HttpRequest request;
            request.url = std::string(endpoint);
            request.method = "POST";
            request.headers = {
                    {"Content-Type", "application/json"},
                    {"Accept", "application/json"}
            };
            request.body = buildTokenizationBody(prompt);

            return request;
        }

        TokenizationResult parseTokenizationResponseImpl(
                std::string_view raw_response) const {

            TokenizationResult result;

            try {
                // 简化的VLLM响应解析
                if (auto tokens = extractJsonArray(raw_response, "tokens")) {
                    result.token_ids = std::move(*tokens);
                    result.token_count = result.token_ids.size();
                }

                result.model_name = extractJsonString(raw_response, "model_name").value_or("unknown");
                result.truncated = extractJsonBool(raw_response, "truncated").value_or(false);

                if (auto error = extractJsonString(raw_response, "error")) {
                    result.error_message = std::move(*error);
                }

            } catch (const std::exception& e) {
                result.error_message = std::string("VLLM tokenization解析错误: ") + e.what();
            }

            return result;
        }

        // ============ 配置获取实现 ============
        HttpRequest createConfigRequestImpl(std::string_view endpoint) const {
            return createGetRequest(endpoint);
        }

        EngineConfig parseConfigResponseImpl(std::string_view raw_response) const {
            EngineConfig config;

            config.model_name = extractJsonString(raw_response, "model_name").value_or("unknown");
            config.tensor_parallel_size = extractJsonInt(raw_response, "tensor_parallel_size").value_or(1);
            config.pipeline_parallel_size = extractJsonInt(raw_response, "pipeline_parallel_size").value_or(1);
            config.max_sequence_length = extractJsonInt(raw_response, "max_sequence_length").value_or(4096);
            config.dtype = extractJsonString(raw_response, "dtype").value_or("float16");
            config.block_size = extractJsonInt(raw_response, "block_size").value_or(128);

            return config;
        }

        // ============ 指标获取实现 ============
        HttpRequest createMetricsRequestImpl(std::string_view endpoint) const {
            return createGetRequest(endpoint);
        }

        LoadMetrics parseMetricsResponseImpl(std::string_view raw_response) const {
            LoadMetrics metrics;

            // 先尝试Prometheus格式
            if (isPrometheusMetrics(raw_response)) {
                metrics = parsePrometheusMetrics(raw_response);
            }
                // 再尝试JSON格式
            else {
                metrics = parseJsonMetrics(raw_response);
            }

            metrics.is_healthy = (metrics.gpu_utilization >= 0 && metrics.gpu_utilization <= 1.0);
            return metrics;
        }

        // ============ 健康检查实现 ============
        HttpRequest createHealthRequestImpl(std::string_view endpoint) const {
            return createGetRequest(buildUrl(endpoint, "/health"));
        }

        bool parseHealthResponseImpl(std::string_view raw_response) const {
            // 简单检查：包含"healthy"或状态码为200
            return raw_response.find("\"healthy\":true") != std::string_view::npos ||
                   raw_response.find("\"status\":\"healthy\"") != std::string_view::npos ||
                   raw_response.find("200") != std::string_view::npos;
        }

        // ============ 端点构建实现 ============
        std::string buildConfigEndpointImpl(std::string_view base_url) const {
            return buildUrl(base_url, "/v1/models");
        }

        std::string buildMetricsEndpointImpl(std::string_view base_url) const {
            return buildUrl(base_url, "/metrics");
        }

        std::string buildTokenizeEndpointImpl(std::string_view base_url) const {
            return buildUrl(base_url, "/v1/tokenize");
        }

    private:
        // ============ 私有工具方法 ============
        std::string buildTokenizationBody(std::string_view prompt) const {
            std::ostringstream oss;
            oss << R"({"text":")" << escapeJsonString(prompt)
                << R"(","return_tokens":true})";
            return oss.str();
        }

        static std::string escapeJsonString(std::string_view str) {
            std::string result;
            result.reserve(str.length());

            for (char c : str) {
                switch (c) {
                    case '"': result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n"; break;
                    case '\r': result += "\\r"; break;
                    case '\t': result += "\\t"; break;
                    default: result += c; break;
                }
            }
            return result;
        }

        // JSON解析工具（简化实现）
        static std::optional<std::vector<int>> extractJsonArray(std::string_view json, std::string_view key) {
            std::string pattern = "\"" + std::string(key) + "\":";
            auto pos = json.find(pattern);
            if (pos == std::string_view::npos) return std::nullopt;

            pos += pattern.length();
            auto array_start = json.find('[', pos);
            if (array_start == std::string_view::npos) return std::nullopt;

            auto array_end = json.find(']', array_start);
            if (array_end == std::string_view::npos) return std::nullopt;

            std::string array_str(json.substr(array_start + 1, array_end - array_start - 1));
            return parseCommaSeparatedInts(array_str);
        }

        static std::optional<std::string> extractJsonString(std::string_view json, std::string_view key) {
            std::string pattern = "\"" + std::string(key) + "\": \"";
            auto pos = json.find(pattern);
            if (pos == std::string_view::npos) return std::nullopt;

            pos += pattern.length();
            auto end = json.find('"', pos);
            if (end == std::string_view::npos) return std::nullopt;

            return std::string(json.substr(pos, end - pos));
        }

        static std::optional<int> extractJsonInt(std::string_view json, std::string_view key) {
            if (auto str = extractJsonString(json, key)) {
                try {
                    return std::stoi(*str);
                } catch (...) {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        static std::optional<bool> extractJsonBool(std::string_view json, std::string_view key) {
            if (auto str = extractJsonString(json, key)) {
                return *str == "true" || *str == "1";
            }
            return std::nullopt;
        }

        static std::vector<int> parseCommaSeparatedInts(std::string_view str) {
            std::vector<int> result;
            std::string p_str{str};
            std::stringstream ss(p_str);
            std::string item;

            while (std::getline(ss, item, ',')) {
                try {
                    result.push_back(std::stoi(item));
                } catch (...) {
                    // 忽略解析错误
                }
            }
            return result;
        }

        static bool isPrometheusMetrics(std::string_view text) {
            return text.find("vllm_") != std::string_view::npos &&
                   text.find('\n') != std::string_view::npos;
        }

        static LoadMetrics parsePrometheusMetrics(std::string_view metrics_text) {
            LoadMetrics metrics;
            std::string p_str{metrics_text};
            std::istringstream stream(p_str);
            std::string line;

            while (std::getline(stream, line)) {
                if (line.empty() || line[0] == '#') continue;

                if (line.find("vllm_gpu_utilization") == 0) {
                    metrics.gpu_utilization = extractPrometheusValue(line) / 100.0;
                } else if (line.find("vllm_cpu_utilization") == 0) {
                    metrics.cpu_utilization = extractPrometheusValue(line) / 100.0;
                }
                // ... 其他指标解析
            }
            return metrics;
        }

        static LoadMetrics parseJsonMetrics(std::string_view json) {
            LoadMetrics metrics;
            // JSON格式解析实现
            return metrics;
        }

        static double extractPrometheusValue(const std::string& metric_line) {
            size_t space_pos = metric_line.find_last_of(' ');
            if (space_pos != std::string::npos && space_pos + 1 < metric_line.length()) {
                try {
                    return std::stod(metric_line.substr(space_pos + 1));
                } catch (...) {}
            }
            return 0.0;
        }
};

// ============ 自动注册触发 ============
namespace {
    [[maybe_unused]] mooncake_conductor::AutoRegisterProxy<mooncake_conductor::VLLMEndpointAdapter> vllm_adapter_auto_registrar;
}

} // namespace mooncake_conductor