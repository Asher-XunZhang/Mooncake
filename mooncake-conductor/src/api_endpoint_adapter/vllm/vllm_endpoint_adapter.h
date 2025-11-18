#pragma once

#include "api_endpoint_adapter.h"

#include <string_view>
#include <iostream>

namespace mooncake_conductor {

class VLLMEndpointAdapter : public APIEndpointAdapter<VLLMEndpointAdapter> {
public:
    static void registerAdapter();

    [[nodiscard]] static std::string getFrameworkTypeImpl() { 
        return "vllm"; 
    }
    
    [[nodiscard]] std::string getFrameworkType() const {
        return getFrameworkTypeImpl();
    }

    [[nodiscard]] HttpRequest createTokenizationRequestImpl(
        std::string_view prompt, 
        std::string_view endpoint
    ) const;

    [[nodiscard]] TokenizationResult parseTokenizationResponseImpl(
        std::string_view raw_response
    ) const;

    [[nodiscard]] HttpRequest createConfigRequestImpl(
        std::string_view endpoint
    ) const;

    [[nodiscard]] EngineConfig parseConfigResponseImpl(
        std::string_view raw_response
    ) const;

    [[nodiscard]] HttpRequest createMetricsRequestImpl(
        std::string_view endpoint
    ) const;

    [[nodiscard]] LoadMetrics parseMetricsResponseImpl(
        std::string_view raw_response
    ) const;

    [[nodiscard]] HttpRequest createHealthRequestImpl(
        std::string_view endpoint
    ) const;

    [[nodiscard]] HttpRequest createCompletionsRequestImpl(
        std::string_view endpoint
    ) const;

    [[nodiscard]] HttpRequest createChatCompletionsRequestImpl(
        std::string_view endpoint
    ) const;

    [[nodiscard]] bool parseHealthResponseImpl(
        std::string_view raw_response
    ) const;

    [[nodiscard]] std::string buildConfigEndpointImpl(std::string_view base_url) const;
    [[nodiscard]] std::string buildMetricsEndpointImpl(std::string_view base_url) const;
    [[nodiscard]] std::string buildTokenizeEndpointImpl(std::string_view base_url) const;
    [[nodiscard]] std::string buildHealthEndpoint(std::string_view base_url) const;
    [[nodiscard]] std::string buildCompletionsEndpoint(std::string_view base_url) const;
    [[nodiscard]] std::string buildChatCompletionsEndpoint(std::string_view base_url) const;

private:
    static bool isPrometheusMetrics(std::string_view text);
    static LoadMetrics parsePrometheusMetrics(std::string_view metrics_text);
};

} // namespace mooncake_conductor