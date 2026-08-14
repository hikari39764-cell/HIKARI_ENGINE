#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"

namespace HIKARI::SAND {

    // System が所有する可変状態。query と renderer bridge はこの共有
    // ポインタ越しに参照するため、World 再構築で vector が再配置されても
    // ポインタは安定する。高さ配列の書換は main-thread の fixed step だけが行う。
    struct SandSurfaceRuntimeState {
        uint32_t gridResolution = 0u;
        SandSurfaceSettings settings{};
        std::vector<float> heights{};

        MATH::Mat4 world = MATH::Mat4::Identity();
        MATH::Mat4 inverseWorld = MATH::Mat4::Identity();
        MATH::Vec2 halfExtents{ 10.0f, 10.0f };
    };

    using SandSurfaceRuntimeStatePtr =
        std::shared_ptr<SandSurfaceRuntimeState>;

    struct SandSurfaceSnapshotEntry {
        RuntimeObjectHandle object{};
        SandSurfaceRuntimeStatePtr state{};
    };

    class SandSurfaceQueryService final {
    public:
        struct Snapshot {
            uint64_t revision = 0u;
            std::vector<SandSurfaceSnapshotEntry> entries{};
        };

        bool SampleWorldHeight(
            const MATH::Vec3& worldPosition,
            float& outHeight,
            MATH::Vec3& outNormal) const noexcept;

        bool SampleLocalHeight(
            const SandSurfaceRuntimeState& state,
            const MATH::Vec2& localXZ,
            float& outHeight,
            MATH::Vec3& outNormal) const noexcept;

        // Renderer bridge 用。現在の snapshot から object の高さ配列を借用する。
        bool TryGetHeights(
            RuntimeObjectHandle object,
            const std::vector<float>*& outHeights) const noexcept;

        uint64_t GetSnapshotRevision() const noexcept;
        std::size_t GetSurfaceCount() const noexcept;
        void PublishSnapshot(
            std::vector<SandSurfaceSnapshotEntry> entries);
        void Clear();

    private:
        std::atomic<std::shared_ptr<const Snapshot>> snapshot_{};
        std::atomic<uint64_t> revision_{ 0u };
        mutable std::mutex publishMutex_{};
    };

} // namespace HIKARI::SAND
