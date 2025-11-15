#pragma once
#include "adapter_factory.h"
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>
#include <optional>
#include <sstream>
#include <iostream>

namespace mooncake_conductor {
namespace nl = nlohmann;

class VLLMEndpointAdapter : public APIEndpointAdapter<VLLMEndpointAdapter> {
public:
    static const std::string getFrameworkTypeImpl() { return "vllm"; }
    static void registerAdapter();

    // Tokenization implementation
    HttpRequest createTokenizationRequestImpl(
        std::string_view prompt, 
        std::string_view endpoint
    ) const  {
        nl::json request_body = {
            {"text", std::string(prompt)},
            {"add_special_tokens", false}
        };
        return createPostRequest(endpoint, request_body.dump());
    }

    TokenizationResult parseTokenizationResponseImpl(
        std::string_view raw_response
    ) const  {
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

    // Config implementation
    HttpRequest createConfigRequestImpl(
        std::string_view endpoint
    ) const  {
        return createGetRequest(endpoint);
    }

    EngineConfig parseConfigResponseImpl(
        std::string_view raw_response
    ) const  {
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
            std::cerr << "[VLLM] Config parse error: " << e.what() << std::endl;
        }
        return config;
    }

    // Metrics implementation
    HttpRequest createMetricsRequestImpl(
        std::string_view endpoint
    ) const  {
        return createGetRequest(endpoint);
    }

    LoadMetrics parseMetricsResponseImpl(
        std::string_view raw_response
    ) const  {
        LoadMetrics metrics;
        try {
            if (isPrometheusMetrics(raw_response)) {
                metrics = parsePrometheusMetrics(raw_response);
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

    // Health check implementation
    HttpRequest createHealthRequestImpl(
        std::string_view endpoint
    ) const  {
        return createGetRequest(endpoint);
    }

    bool parseHealthResponseImpl(
        std::string_view raw_response
    ) const  {
        try {
            auto j = nl::json::parse(raw_response);
            return j.value("status", "") == "healthy" || j.value("healthy", false);
        } catch (...) {
            return false;
        }
    }

    // Endpoint builders
    std::string buildConfigEndpointImpl(std::string_view base_url) const  {
        return buildUrl(base_url, "/v1/models");
    }

    std::string buildMetricsEndpointImpl(std::string_view base_url) const  {
        return buildUrl(base_url, "/metrics");
    }

    std::string buildTokenizeEndpointImpl(std::string_view base_url) const  {
        return buildUrl(base_url, "/v1/tokenize");
    }

    std::string buildHealthEndpoint(std::string_view base_url) const {
        return buildUrl(base_url, "/health");
    }

private:
    static bool isPrometheusMetrics(std::string_view text) {
        return text.find("vllm:") != std::string_view::npos || 
               text.find("vllm_") != std::string_view::npos;
    }

    static LoadMetrics parsePrometheusMetrics(std::string_view metrics_text) {
        LoadMetrics metrics;
        std::string tmp_str{metrics_text};
        std::istringstream stream(tmp_str);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (line.find("vllm:gpu_utilization") != std::string::npos ||
                line.find("vllm_gpu_utilization") != std::string::npos) {
                size_t last_space = line.find_last_of(' ');
                if (last_space != std::string::npos) {
                    try {
                        double value = std::stod(line.substr(last_space + 1));
                        metrics.gpu_utilization = value / 100.0;
                    } catch (...) {}
                }
            }
        }
        return metrics;
    }
};

// Explicit registration function
inline void VLLMEndpointAdapter::registerAdapter() {
    EndpointAdapterFactory::registerAdapter<VLLMEndpointAdapter>("vllm");
}

} // namespace mooncake_conductor