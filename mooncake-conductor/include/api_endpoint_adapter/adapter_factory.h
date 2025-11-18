#pragma once
#include "api_endpoint_adapter.h"
#include "adapter_initializer.h"

#include <glog/logging.h>

#include <memory>
#include <unordered_map>
#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include <sstream>
#include <string_view>

namespace mooncake_conductor {

template<typename AdapterType>
class EndpointAdapterWrapper : public IEndpointAdapter {
    AdapterType adapter_;

public:
    [[nodiscard]] HttpRequest createTokenizationRequest(
        std::string_view prompt, std::string_view endpoint) const override {
        return adapter_.createTokenizationRequest(prompt, endpoint);
    }

    [[nodiscard]] TokenizationResult parseTokenizationResponse(
        std::string_view raw_response) const override {
        return adapter_.parseTokenizationResponse(raw_response);
    }

    [[nodiscard]] HttpRequest createConfigRequest(std::string_view endpoint) const override {
        return adapter_.createConfigRequest(endpoint);
    }

    [[nodiscard]] EngineConfig parseConfigResponse(std::string_view raw_response) const override {
        return adapter_.parseConfigResponse(raw_response);
    }

    [[nodiscard]] HttpRequest createMetricsRequest(std::string_view endpoint) const override {
        return adapter_.createMetricsRequest(endpoint);
    }

    [[nodiscard]] LoadMetrics parseMetricsResponse(std::string_view raw_response) const override {
        return adapter_.parseMetricsResponse(raw_response);
    }

    [[nodiscard]] HttpRequest createHealthRequest(std::string_view endpoint) const override {
        return adapter_.createHealthRequest(endpoint);
    }

    [[nodiscard]] bool parseHealthResponse(std::string_view raw_response) const override {
        return adapter_.parseHealthResponse(raw_response);
    }

    [[nodiscard]] std::string buildConfigEndpoint(std::string_view base_url) const override {
        return adapter_.buildConfigEndpoint(base_url);
    }

    [[nodiscard]] std::string buildMetricsEndpoint(std::string_view base_url) const override {
        return adapter_.buildMetricsEndpoint(base_url);
    }

    [[nodiscard]] std::string buildTokenizeEndpoint(std::string_view base_url) const override {
        return adapter_.buildTokenizeEndpoint(base_url);
    }

    [[nodiscard]] std::string buildHealthEndpoint(std::string_view base_url) const override {
        return adapter_.buildHealthEndpoint(base_url);
    }

    [[nodiscard]] std::string getFrameworkType() const override {
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

    template<typename AdapterType>
    friend void registerAdapterImpl(std::string_view framework_type);

public:
    template<typename AdapterType>
    static void registerAdapter(std::string_view framework_type) {
        std::string type_str(framework_type);
        getRegistry()[type_str] = []() {
            return std::make_unique<EndpointAdapterWrapper<AdapterType>>();
        };
        LOG(INFO) << "[AdapterFactory] Registered: " << type_str;
    }

    static std::unique_ptr<IEndpointAdapter> createAdapter(std::string_view framework_type) {
        internal::AdapterInitializer::ensureRegistered(framework_type);

        auto& registry = getRegistry();
        std::string type_str(framework_type);

        auto it = registry.find(type_str);
        if (it != registry.end() && it->second) {
            return it->second();
        }

        std::vector<std::string> available;
        for (const auto& pair : registry) {
            available.push_back(pair.first);
        }
        
        std::ostringstream oss;
        oss << "[AdapterFactory] Unknown framework: " << framework_type << ". Available: ";

        std::string_view delim = "";
        for (const auto& item : available) {
            oss << delim << item;
            delim = ", ";
        }
        LOG(ERROR) << oss.str();

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

template<typename AdapterType>
void registerAdapterImpl(std::string_view framework_type) {
    internal::AdapterInitializer::registerAdapter(
        framework_type,
        [framework_type = std::string(framework_type)]() { 
            EndpointAdapterFactory::registerAdapter<AdapterType>(framework_type); 
        }
    );
}

} // namespace mooncake_conductor