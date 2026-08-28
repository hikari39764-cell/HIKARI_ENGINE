#include "Scene/HIKARI_World.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace HIKARI {
    namespace {

        struct RuntimeWorldRegistryEntry {
            World* world = nullptr;
            std::thread::id ownerThread{};
        };

        struct RuntimeWorldRegistry {
            std::mutex mutex{};
            std::unordered_map<
                uint64_t,
                RuntimeWorldRegistryEntry> entries{};
            std::atomic<uint64_t> nextIdentity{ 1u };
        };

        RuntimeWorldRegistry& GetRuntimeWorldRegistry() {
            static RuntimeWorldRegistry* registry =
                new RuntimeWorldRegistry{};
            return *registry;
        }

        uint64_t RegisterRuntimeWorld(
            World& world,
            std::thread::id ownerThread) {
            RuntimeWorldRegistry& registry = GetRuntimeWorldRegistry();
            for (;;) {
                const uint64_t identity =
                    registry.nextIdentity.fetch_add(
                        1u,
                        std::memory_order_relaxed);
                if (identity == 0u) {
                    continue;
                }
                std::scoped_lock lock(registry.mutex);
                const auto [iterator, inserted] =
                    registry.entries.emplace(
                        identity,
                        RuntimeWorldRegistryEntry{
                            &world,
                            ownerThread
                        });
                (void)iterator;
                if (inserted) {
                    return identity;
                }
            }
        }

        void UnregisterRuntimeWorld(uint64_t identity) noexcept {
            if (identity == 0u) {
                return;
            }
            try {
                RuntimeWorldRegistry& registry =
                    GetRuntimeWorldRegistry();
                std::scoped_lock lock(registry.mutex);
                registry.entries.erase(identity);
            }
            catch (...) {
            }
        }

    } // namespace

    World::World()
        : ownerThread_(std::this_thread::get_id()) {
        pendingCommands_.reserve(kWorldCommandInitialCapacity);
        executionCommands_.reserve(kWorldCommandInitialCapacity);
        commandFailures_.reserve(kWorldCommandFailureCapacity);
        runtimeIdentity_ = RegisterRuntimeWorld(
            *this,
            ownerThread_);
    }

    void World::UnregisterRuntimeIdentity() noexcept {
        UnregisterRuntimeWorld(runtimeIdentity_);
        runtimeIdentity_ = 0u;
    }

    World* World::ResolveRuntimeWorld(
        uint64_t identity,
        RuntimeWorldAccessResult* outAccess) noexcept {
        if (outAccess != nullptr) {
            *outAccess = RuntimeWorldAccessResult::InvalidWorld;
        }
        if (identity == 0u) {
            return nullptr;
        }
        try {
            RuntimeWorldRegistry& registry = GetRuntimeWorldRegistry();
            std::scoped_lock lock(registry.mutex);
            const auto found = registry.entries.find(identity);
            if (found == registry.entries.end() ||
                found->second.world == nullptr) {
                return nullptr;
            }
            if (found->second.ownerThread !=
                std::this_thread::get_id()) {
                if (outAccess != nullptr) {
                    *outAccess = RuntimeWorldAccessResult::WrongThread;
                }
                return nullptr;
            }
            if (outAccess != nullptr) {
                *outAccess = RuntimeWorldAccessResult::Success;
            }
            return found->second.world;
        }
        catch (...) {
            return nullptr;
        }
    }

} // namespace HIKARI
