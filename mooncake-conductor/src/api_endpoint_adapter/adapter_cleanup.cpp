// adapter_cleanup.cpp
#include "adapter_initializer.h"

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