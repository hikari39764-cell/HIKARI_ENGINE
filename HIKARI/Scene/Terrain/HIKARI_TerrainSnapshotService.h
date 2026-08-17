#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace HIKARI::TERRAIN {

    // 地形/地表 feature の query service が共有する immutable snapshot store。
    // publish/clear は atomic に切り替わり、reader は常に同じ revision を読む。
    template <typename TEntry>
    class TerrainSnapshotService {
    public:
        struct Snapshot {
            uint64_t revision = 0u;
            std::vector<TEntry> entries{};
        };

        std::shared_ptr<const Snapshot> Current() const noexcept {
            return snapshot_.load(std::memory_order_acquire);
        }

        uint64_t Revision() const noexcept {
            const std::shared_ptr<const Snapshot> current = Current();
            return current != nullptr ? current->revision : 0u;
        }

        std::size_t Count() const noexcept {
            const std::shared_ptr<const Snapshot> current = Current();
            return current != nullptr ? current->entries.size() : 0u;
        }

        void Publish(std::vector<TEntry> entries) {
            const uint64_t nextRevision = revision_.load() + 1u;
            auto next = std::make_shared<Snapshot>();
            next->revision = nextRevision;
            next->entries = std::move(entries);

            std::lock_guard<std::mutex> lock(publishMutex_);
            snapshot_.store(
                std::shared_ptr<const Snapshot>(std::move(next)),
                std::memory_order_release);
            revision_.store(nextRevision);
        }

        void Clear() {
            const uint64_t nextRevision = revision_.load() + 1u;
            auto next = std::make_shared<Snapshot>();
            next->revision = nextRevision;

            std::lock_guard<std::mutex> lock(publishMutex_);
            snapshot_.store(
                std::shared_ptr<const Snapshot>(std::move(next)),
                std::memory_order_release);
            revision_.store(nextRevision);
        }

    private:
        std::atomic<std::shared_ptr<const Snapshot>> snapshot_{};
        std::atomic<uint64_t> revision_{ 0u };
        mutable std::mutex publishMutex_{};
    };

} // namespace HIKARI::TERRAIN
