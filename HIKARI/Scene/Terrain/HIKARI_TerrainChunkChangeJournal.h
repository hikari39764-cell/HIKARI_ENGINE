#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <vector>

#include "Scene/Terrain/HIKARI_TerrainChunks.h"

namespace HIKARI::TERRAIN {

// TerrainSurface の authored 差分を chunk key 単位で保持する。
// 履歴切れ、topology、owner-wide policy の変更は false を返し、
// consumer に owner 全体の再同期を要求する。
class TerrainChunkChangeJournal final {
public:
    static constexpr std::size_t kDefaultHistoryLimit = 4096u;

    explicit TerrainChunkChangeJournal(std::size_t historyLimit = kDefaultHistoryLimit) noexcept
        : historyLimit_((std::max)(historyLimit, std::size_t{1u})) {}

    uint64_t GetRevision() const noexcept { return revision_; }

    void RecordChunk(TerrainChunkKey key) noexcept { Record({key, false}); }

    void RecordFullRebuild() noexcept { Record({{}, true}); }

    bool TryGetChangedChunkKeys(uint64_t sinceRevision, std::vector<TerrainChunkKey>& outKeys) const {
        outKeys.clear();
        if (sinceRevision == revision_) {
            return true;
        }
        if (sinceRevision > revision_ || history_.empty()) {
            return false;
        }
        const uint64_t oldestRevision = history_.front().revision;
        if (oldestRevision > 0u && sinceRevision < oldestRevision - 1u) {
            return false;
        }

        std::set<TerrainChunkKey> changed{};
        for (const RecordEntry& entry : history_) {
            if (entry.revision <= sinceRevision) {
                continue;
            }
            if (entry.fullRebuild) {
                outKeys.clear();
                return false;
            }
            changed.insert(entry.key);
        }
        outKeys.assign(changed.begin(), changed.end());
        return true;
    }

private:
    struct RecordEntry {
        uint64_t revision = 0u;
        TerrainChunkKey key{};
        bool fullRebuild = false;
    };

    struct PendingRecord {
        TerrainChunkKey key{};
        bool fullRebuild = false;
    };

    void Record(PendingRecord pending) noexcept {
        ++revision_;
        if (revision_ == 0u) {
            // 0 は initial state の sentinel。実用上到達不能だが、
            // wrap 後に古い interval を誤って incremental 扱いしない。
            revision_ = 1u;
            history_.clear();
            pending.fullRebuild = true;
        }
        history_.push_back({
            revision_,
            pending.key,
            pending.fullRebuild,
        });
        if (history_.size() > historyLimit_) {
            history_.pop_front();
        }
    }

    std::size_t historyLimit_ = kDefaultHistoryLimit;
    uint64_t revision_ = 0u;
    std::deque<RecordEntry> history_{};
};

} // namespace HIKARI::TERRAIN
