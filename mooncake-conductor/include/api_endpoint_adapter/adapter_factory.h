#pragma once
#include "api_endpoint_adapter.h"
#include <memory>
#include <unordered_map>
#include <iostream>
#include <functional>
#include <vector>
#include <string>

namespace mooncake_conductor {

template<typename AdapterType>
class EndpointAdapterWrapper : public IEndpointAdapter {
    AdapterType adapter_;

public:
    HttpRequest createTokenizationRequest(
        std::string_view prompt, std::string_view endpoint) const {
        return adapter_.createTokenizationRequest(prompt, endpoint);
    }

    TokenizationResult parseTokenizationResponse(
        std::string_view raw_response) const {
        return adapter_.parseTokenizationResponse(raw_response);
    }

    HttpRequest createConfigRequest(std::string_view endpoint) const {
        return adapter_.createConfigRequest(endpoint);
    }

    EngineConfig parseConfigResponse(std::string_view raw_response) const {
        return adapter_.parseConfigResponse(raw_response);
    }

    HttpRequest createMetricsRequest(std::string_view endpoint) const {
        return adapter_.createMetricsRequest(endpoint);
    }

    LoadMetrics parseMetricsResponse(std::string_view raw_response) const {
        return adapter_.parseMetricsResponse(raw_response);
    }

    HttpRequest createHealthRequest(std::string_view endpoint) const {
        return adapter_.createHealthRequest(endpoint);
    }

    bool parseHealthResponse(std::string_view raw_response) const {
        return adapter_.parseHealthResponse(raw_response);
    }

    std::string buildConfigEndpoint(std::string_view base_url) const {
        return adapter_.buildConfigEndpoint(base_url);
    }

    std::string buildMetricsEndpoint(std::string_view base_url) const {
        return adapter_.buildMetricsEndpoint(base_url);
    }

    std::string buildTokenizeEndpoint(std::string_view base_url) const {
        return adapter_.buildTokenizeEndpoint(base_url);
    }

    std::string buildHealthEndpoint(std::string_view base_url) const {
        return adapter_.buildHealthEndpoint(base_url);
    }

    std::string getFrameworkType() const {
        return adapter_.getFrameworkType();
    }
};

class EndpointAdapterFactory {
private:
    using AdapterCreator = std::function<std::unique_ptr<IEndpointAdapter>()>;
    using AdapterRegistry = std::unordered_map<std::string, AdapterCreator>;

    static AdapterRegistry& getRegistry() {
        static AdapterRegistry registry;
        return registry;
    }

public:
    template<typename AdapterType>
    static void registerAdapter(std::string_view framework_type) {
        std::string type_str(framework_type);
        getRegistry()[type_str] = []() {
            return std::make_unique<EndpointAdapterWrapper<AdapterType>>();
        };
        std::cout << "[AdapterFactory] Registered: " << type_str << std::endl;
    }

    static std::unique_ptr<IEndpointAdapter> createAdapter(std::string_view framework_type) {
        auto& registry = getRegistry();
        std::string type_str(framework_type);

        auto it = registry.find(type_str);
        if (it != registry.end() && it->second) {
            return it->second();
        }

        std::cerr << "[AdapterFactory] Unknown framework: " << framework_type
                  << ". Available: ";
        for (const auto& pair : registry) {
            std::cerr << pair.first << " ";
        }
        std::cerr << std::endl;

        return nullptr;
    }

    static std::vector<std::string> getSupportedFrameworks() {
        std::vector<std::string> frameworks;
        auto& registry = getRegistry();
        frameworks.reserve(registry.size());
        for (const auto& pair : registry) {
            frameworks.push_back(pair.first);
        }
        return frameworks;
    }
};

} // namespace mooncake_conductor