#pragma once

#include "conductor_types.h"

#include <string>
#include <string_view>
#include <memory>

namespace mooncake_conductor {

class IEndpointAdapter {
public:
    virtual ~IEndpointAdapter() = default;

    [[nodiscard]] virtual HttpRequest createTokenizationRequest(
        std::string_view prompt,
        std::string_view endpoint) const = 0;

    [[nodiscard]] virtual TokenizationResult parseTokenizationResponse(
        std::string_view raw_response) const = 0;

    [[nodiscard]] virtual HttpRequest createConfigRequest(
        std::string_view endpoint) const = 0;

    [[nodiscard]] virtual EngineConfig parseConfigResponse(
        std::string_view raw_response) const = 0;

    [[nodiscard]] virtual HttpRequest createMetricsRequest(
        std::string_view endpoint) const = 0;

    [[nodiscard]] virtual LoadMetrics parseMetricsResponse(
        std::string_view raw_response) const = 0;

    [[nodiscard]] virtual HttpRequest createHealthRequest(
        std::string_view endpoint) const = 0;
    
    [[nodiscard]] virtual HttpRequest createCompletionsRequest(
        std::string_view endpoint) const = 0;
    
    [[nodiscard]] virtual HttpRequest createChatCompletionsRequest(
        std::string_view endpoint) const = 0;

    [[nodiscard]] virtual bool parseHealthResponse(
        std::string_view raw_response) const = 0;

    [[nodiscard]] virtual std::string buildConfigEndpoint(
        std::string_view base_url) const = 0;

    [[nodiscard]] virtual std::string buildMetricsEndpoint(
        std::string_view base_url) const = 0;

    [[nodiscard]] virtual std::string buildTokenizeEndpoint(
        std::string_view base_url) const = 0;

    [[nodiscard]] virtual std::string buildHealthEndpoint(
        std::string_view base_url) const = 0;

    [[nodiscard]] virtual std::string buildCompletionsEndpoint(
        std::string_view base_url) const = 0;
    
    [[nodiscard]] virtual std::string buildChatCompletionsEndpoint(
        std::string_view base_url) const = 0;

    [[nodiscard]] virtual std::string getFrameworkType() const = 0;
};

template<typename Derived>
class APIEndpointAdapter : public IEndpointAdapter {
public:
    [[nodiscard]] HttpRequest createTokenizationRequest(
        std::string_view prompt, std::string_view endpoint) const {
        return derived().createTokenizationRequestImpl(prompt, endpoint);
    }

    [[nodiscard]] TokenizationResult parseTokenizationResponse(
        std::string_view raw_response) const {
        return derived().parseTokenizationResponseImpl(raw_response);
    }

    [[nodiscard]] HttpRequest createConfigRequest(std::string_view endpoint) const {
        return derived().createConfigRequestImpl(endpoint);
    }

    [[nodiscard]] EngineConfig parseConfigResponse(std::string_view raw_response) const {
        return derived().parseConfigResponseImpl(raw_response);
    }

    [[nodiscard]] HttpRequest createMetricsRequest(std::string_view endpoint) const {
        return derived().createMetricsRequestImpl(endpoint);
    }

    [[nodiscard]] LoadMetrics parseMetricsResponse(std::string_view raw_response) const {
        return derived().parseMetricsResponseImpl(raw_response);
    }

    [[nodiscard]] HttpRequest createHealthRequest(std::string_view endpoint) const {
        return derived().createHealthRequestImpl(endpoint);
    }

    [[nodiscard]] HttpRequest createCompletionsRequest(std::string_view endpoint) const {
        return derived().createCompletionsRequestImpl(endpoint);
    }

    [[nodiscard]] HttpRequest createChatCompletionsRequest(std::string_view endpoint) const {
        return derived().createChatCompletionsRequestImpl(endpoint);
    }

    [[nodiscard]] bool parseHealthResponse(std::string_view raw_response) const {
        return derived().parseHealthResponseImpl(raw_response);
    }

    [[nodiscard]] std::string buildConfigEndpoint(std::string_view base_url) const {
        return derived().buildConfigEndpointImpl(base_url);
    }

    [[nodiscard]] std::string buildMetricsEndpoint(std::string_view base_url) const {
        return derived().buildMetricsEndpointImpl(base_url);
    }

    [[nodiscard]] std::string buildTokenizeEndpoint(std::string_view base_url) const {
        return derived().buildTokenizeEndpointImpl(base_url);
    }

    [[nodiscard]] std::string buildHealthEndpoint(std::string_view base_url) const {
        return derived().buildHealthEndpoint(base_url);
    }

    [[nodiscard]] std::string buildCompletionsEndpoint(std::string_view base_url) const {
        return derived().buildCompletionsEndpoint(base_url);
    }

    [[nodiscard]] std::string buildChatCompletionsEndpoint(std::string_view base_url) const {
        return derived().buildChatCompletionsEndpoint(base_url);
    }

    [[nodiscard]] std::string getFrameworkType() const {
        return derived().getFrameworkTypeImpl();
    }

protected:
    static HttpRequest createGetRequest(std::string_view url) {
        return HttpRequest{std::string(url), "GET", {}, {}};
    }

    static HttpRequest createPostRequest(std::string_view url, std::string_view body) {
        return HttpRequest{
            std::string(url), 
            "POST", 
            {{"Content-Type", "application/json"}, {"Accept", "application/json"}}, 
            std::string(body)
        };
    }

    static std::string buildUrl(std::string_view base_url, std::string_view path) {
        if (base_url.empty()) return std::string(path);
        if (path.empty()) return std::string(base_url);

        std::string result{base_url};
        if (result.back() != '/' && path.front() != '/') {
            result += '/';
        } else if (result.back() == '/' && path.front() == '/') {
            path = path.substr(1);
        }
        result += path;
        return result;
    }

private:
    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }
};

} // namespace mooncake_conductor