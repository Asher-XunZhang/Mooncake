// vllm_endpoint_adapter.h
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

    [[nodiscard]] std::string buildConfigEndpointImpl(std::string_view base_url) const;
    [[nodiscard]] std::string buildMetricsEndpointImpl(std::string_view base_url) const;
    [[nodiscard]] std::string buildTokenizeEndpointImpl(std::string_view base_url) const;
    [[nodiscard]] std::string buildHealthEndpoint(std::string_view base_url) const;
    [[nodiscard]] std::string buildCompletionsEndpoint(std::string_view base_url) const;
    [[nodiscard]] std::string buildChatCompletionsEndpoint(std::string_view base_url) const;
};

} // namespace mooncake_conductor