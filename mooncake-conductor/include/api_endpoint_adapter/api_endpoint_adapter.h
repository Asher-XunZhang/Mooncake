// api_endpoint_adapter.h
#pragma once

#include "conductor_types.h"

#include <string>
#include <string_view>
#include <memory>

namespace mooncake_conductor {

class IEndpointAdapter {
public:
    virtual ~IEndpointAdapter() = default;

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