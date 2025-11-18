#pragma once

#include <glog/logging.h>

#include <mutex>
#include <atomic>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <vector>

namespace mooncake_conductor {
namespace internal {

class AdapterInitializer {
public:
    using RegistrationFunc = std::function<void()>;
    
    static void registerAdapter(std::string_view name, RegistrationFunc func) {
        auto& registry = getRegistry();
        std::lock_guard<std::mutex> lock(getMutex());
        registry.emplace(std::string(name), std::move(func));
    }

    static void ensureRegistered(std::string_view adapter_name) {
        std::string name_str(adapter_name);
        
        {
            auto states = getInitStates().load(std::memory_order_acquire);
            if (states && states->count(name_str) && (*states)[name_str]) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(getMutex());
        
        auto& registry = getRegistry();
        auto it = registry.find(name_str);
        if (it != registry.end() && it->second) {
            it->second();
        }
        
        auto states = getInitStates().load(std::memory_order_relaxed);
        if (!states) {
            states = new std::unordered_map<std::string, bool>();
            getInitStates().store(states, std::memory_order_release);
        }
        (*states)[name_str] = true;
    }

    static void cleanup() {
        // auto states = getInitStates().exchange(nullptr);
        // delete states;
        auto states = getInitStates().exchange(nullptr);
        if (states) {
            LOG(INFO) << "[AdapterCleanup] Releasing adapter states map (" 
                    << states->size() << " entries)";
            delete states;
        }
        
    }

private:
    using RegistryMap = std::unordered_map<std::string, RegistrationFunc>;
    
    static RegistryMap& getRegistry() {
        static RegistryMap registry;
        return registry;
    }
    
    static std::mutex& getMutex() {
        static std::mutex mutex;
        return mutex;
    }
    
    static std::atomic<std::unordered_map<std::string, bool>*> &getInitStates() {
        static std::atomic<std::unordered_map<std::string, bool>*> states{nullptr};
        return states;
    }
};

} // namespace internal
} // namespace mooncake_conductor