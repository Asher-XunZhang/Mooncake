// api_endpoint_adapter.h
#pragma once
#include "common_types.h"
#include <string>
#include <string_view>
#include <memory>

namespace mooncake_conductor {

// ============ 您的接口设计实现 ============
    class IEndpointAdapter {
    public:
        virtual ~IEndpointAdapter() = default;

        // === 编码功能：构造请求 ===
        [[nodiscard]] virtual HttpRequest createTokenizationRequest(
                std::string_view prompt,
                std::string_view endpoint) const = 0;

        [[nodiscard]] virtual TokenizationResult parseTokenizationResponse(
                std::string_view raw_response) const = 0;

        // === 配置获取：构造请求 ===
        [[nodiscard]] virtual HttpRequest createConfigRequest(
                std::string_view endpoint) const = 0;

        [[nodiscard]] virtual EngineConfig parseConfigResponse(
                std::string_view raw_response) const = 0;

        // === 指标获取：构造请求 ===
        [[nodiscard]] virtual HttpRequest createMetricsRequest(
                std::string_view endpoint) const = 0;

        [[nodiscard]] virtual LoadMetrics parseMetricsResponse(
                std::string_view raw_response) const = 0;

        // === 健康检查：构造请求 ===
        [[nodiscard]] virtual HttpRequest createHealthRequest(
                std::string_view endpoint) const = 0;

        [[nodiscard]] virtual bool parseHealthResponse(
                std::string_view raw_response) const = 0;

        // === 端点构建辅助 ===
        [[nodiscard]] virtual std::string buildConfigEndpoint(
                std::string_view base_url) const = 0;

        [[nodiscard]] virtual std::string buildMetricsEndpoint(
                std::string_view base_url) const = 0;

        [[nodiscard]] virtual std::string buildTokenizeEndpoint(
                std::string_view base_url) const = 0;

        [[nodiscard]] virtual std::string getFrameworkType() const = 0;
    };

// ============ CRTP基类模板（实现您的接口） ============
    template<typename Derived>
    class APIEndpointAdapter : public IEndpointAdapter {
    public:
        // === 编码功能实现 ===
        [[nodiscard]] HttpRequest createTokenizationRequest(
                std::string_view prompt, std::string_view endpoint) const override {
            return derived().createTokenizationRequestImpl(prompt, endpoint);
        }

        [[nodiscard]] TokenizationResult parseTokenizationResponse(
                std::string_view raw_response) const override {
            return derived().parseTokenizationResponseImpl(raw_response);
        }

        // === 配置获取实现 ===
        [[nodiscard]] HttpRequest createConfigRequest(std::string_view endpoint) const override {
            return derived().createConfigRequestImpl(endpoint);
        }

        [[nodiscard]] EngineConfig parseConfigResponse(std::string_view raw_response) const override {
            return derived().parseConfigResponseImpl(raw_response);
        }

        // === 指标获取实现 ===
        [[nodiscard]] HttpRequest createMetricsRequest(std::string_view endpoint) const override {
            return derived().createMetricsRequestImpl(endpoint);
        }

        [[nodiscard]] LoadMetrics parseMetricsResponse(std::string_view raw_response) const override {
            return derived().parseMetricsResponseImpl(raw_response);
        }

        // === 健康检查实现 ===
        [[nodiscard]] HttpRequest createHealthRequest(std::string_view endpoint) const override {
            return derived().createHealthRequestImpl(endpoint);
        }

        [[nodiscard]] bool parseHealthResponse(std::string_view raw_response) const override {
            return derived().parseHealthResponseImpl(raw_response);
        }

        // === 端点构建实现 ===
        [[nodiscard]] std::string buildConfigEndpoint(std::string_view base_url) const override {
            return std::string{derived().buildConfigEndpointImpl(base_url)};
        }

        [[nodiscard]] std::string buildMetricsEndpoint(std::string_view base_url) const override {
            return std::string{derived().buildMetricsEndpointImpl(base_url)};
        }

        [[nodiscard]] std::string buildTokenizeEndpoint(std::string_view base_url) const override {
            return std::string{derived().buildTokenizeEndpointImpl(base_url)};
        }

        [[nodiscard]] std::string getFrameworkType() const override {
            return std::string{derived().getFrameworkTypeImpl()};
        }

    protected:
        // === 工具方法 ===
        static HttpRequest createGetRequest(std::string_view url) {
            return HttpRequest{std::string(url), "GET", {}, {}};
        }

        static HttpRequest createPostRequest(std::string_view url, std::string_view body) {
            return HttpRequest{std::string(url), "POST", {}, std::string(body)};
        }

        static std::string buildUrl(std::string_view base_url, std::string_view path) {
            if (base_url.empty()) return std::string(path);
            if (path.empty()) return std::string(base_url);

            if (base_url.back() == '/' && path.front() == '/') {
                return std::string(base_url) + std::string(path.substr(1));
            } else if (base_url.back() != '/' && path.front() != '/') {
                return std::string(base_url) + "/" + std::string(path);
            }
            return std::string(base_url) + std::string(path);
        }

    private:
        // CRTP辅助方法
        Derived& derived() { return static_cast<Derived&>(*this); }
        const Derived& derived() const { return static_cast<const Derived&>(*this); }
    };

} // namespace mooncake_conductor