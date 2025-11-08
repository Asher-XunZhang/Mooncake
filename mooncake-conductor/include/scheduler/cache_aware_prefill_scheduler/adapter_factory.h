// adapter_factory.h
#pragma once
#include "api_endpoint_adapter.h"
#include <memory>
#include <unordered_map>
#include <iostream>
#include <functional>

namespace mooncake_conductor {

// ============ 类型擦除包装器 ============
    template<typename AdapterType>
    class EndpointAdapterWrapper : public IEndpointAdapter {
        AdapterType adapter_;

    public:
        HttpRequest createTokenizationRequest(
                std::string_view prompt, std::string_view endpoint) const override {
            return adapter_.createTokenizationRequest(prompt, endpoint);
        }

        TokenizationResult parseTokenizationResponse(
                std::string_view raw_response) const override {
            return adapter_.parseTokenizationResponse(raw_response);
        }

        HttpRequest createConfigRequest(std::string_view endpoint) const override {
            return adapter_.createConfigRequest(endpoint);
        }

        EngineConfig parseConfigResponse(std::string_view raw_response) const override {
            return adapter_.parseConfigResponse(raw_response);
        }

        HttpRequest createMetricsRequest(std::string_view endpoint) const override {
            return adapter_.createMetricsRequest(endpoint);
        }

        LoadMetrics parseMetricsResponse(std::string_view raw_response) const override {
            return adapter_.parseMetricsResponse(raw_response);
        }

        HttpRequest createHealthRequest(std::string_view endpoint) const override {
            return adapter_.createHealthRequest(endpoint);
        }

        bool parseHealthResponse(std::string_view raw_response) const override {
            return adapter_.parseHealthResponse(raw_response);
        }

        std::string buildConfigEndpoint(std::string_view base_url) const override {
            return adapter_.buildConfigEndpoint(base_url);
        }

        std::string buildMetricsEndpoint(std::string_view base_url) const override {
            return adapter_.buildMetricsEndpoint(base_url);
        }

        std::string buildTokenizeEndpoint(std::string_view base_url) const override {
            return adapter_.buildTokenizeEndpoint(base_url);
        }

        std::string getFrameworkType() const override {
            return adapter_.getFrameworkType();
        }
    };

// ============ 适配器工厂 ============
    class EndpointAdapterFactory {
    private:
        using AdapterCreator = std::function<std::unique_ptr<IEndpointAdapter>()>;
        using AdapterRegistry = std::unordered_map<std::string, AdapterCreator>;

    public:
        template<typename AdapterType>
        static void autoRegister() {
            constexpr auto framework_type = AdapterType::getFrameworkTypeImpl();
            static_assert(!std::string_view(framework_type).empty(),
                          "Framework type must not be empty");

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
            for (const auto& pair : getRegistry()) {
                frameworks.push_back(pair.first);
            }
            return frameworks;
        }

    private:
        static AdapterRegistry& getRegistry() {
            static AdapterRegistry registry;
            return registry;
        }
    };

// ============ 自动注册代理 ============
    template<typename AdapterType>
    class AutoRegisterProxy {
    private:
        struct AutoRegistrar {
            AutoRegistrar() {
                EndpointAdapterFactory::template autoRegister<AdapterType>();
            }
        };

        static inline AutoRegistrar auto_registrar_{};

    public:
        AutoRegisterProxy() { (void)auto_registrar_; }
    };

} // namespace mooncake_conductor