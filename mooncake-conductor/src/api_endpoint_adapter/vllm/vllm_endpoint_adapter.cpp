// vllm_endpoint_adapter.cpp
#include "vllm_endpoint_adapter.h"
#include "adapter_factory.h"

#include <nlohmann/json.hpp>
#include <glog/logging.h>

#include <sstream>
#include <vector>
#include <string>
#include <cctype>

namespace mooncake_conductor {

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

std::string VLLMEndpointAdapter::buildCompletionsEndpoint(std::string_view base_url) const {
    return buildUrl(base_url, "/v1/completions");
}

std::string VLLMEndpointAdapter::buildChatCompletionsEndpoint(std::string_view base_url) const {
    return buildUrl(base_url, "/v1/chat/completions");
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