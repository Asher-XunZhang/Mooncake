#include "vllm_endpoint_adapter.h"

namespace mooncake_conductor {
namespace {
    // Static registration variable (guaranteed to be initialized before main)
    bool vllm_registered = [] {
        VLLMEndpointAdapter::registerAdapter();
        return true;
    }();
} // namespace
} // namespace mooncake_conductor