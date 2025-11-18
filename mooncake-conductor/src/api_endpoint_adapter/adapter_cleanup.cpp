#include "api_endpoint_adapter/internal/adapter_initializer.h"

#include <cstdlib>

namespace {
    struct AdapterCleanup {
        AdapterCleanup() {
            std::atexit([]() {
                mooncake_conductor::internal::AdapterInitializer::cleanup();
            });
        }
    } cleanup;
} // namespace