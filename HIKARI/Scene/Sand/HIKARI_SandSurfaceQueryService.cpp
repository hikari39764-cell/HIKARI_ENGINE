#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace HIKARI::SAND {

    namespace {
        std::shared_ptr<const SandSurfaceQueryService::Snapshot>
            MakeEmptySnapshot() {
            return std::make_shared<const SandSurfaceQueryService::Snapshot>();
        }

        float SampleGrid(
            const SandSurfaceRuntimeState& state,
            float u,
            float v) noexcept {

            const uint32_t resolution =
                (std::max)(state.gridResolution, 1u);
            const float fu = std::clamp(u, 0.0f, 1.0f) *
                static_cast<float>(resolution);
            const float fv = std::clamp(v, 0.0f, 1.0f) *
                static_cast<float>(resolution);
            uint32_t x0 = static_cast<uint32_t>(fu);
            uint32_t y0 = static_cast<uint32_t>(fv);
            uint32_t x1 = (std::min)(x0 + 1u, resolution);
            uint32_t y1 = (std::min)(y0 + 1u, resolution);
            const float tx = fu - static_cast<float>(x0);
            const float ty = fv - static_cast<float>(y0);
            const uint32_t stride = resolution + 1u;
            const std::vector<float>& heights = state.heights;
            if (heights.size() !=
                static_cast<size_t>(stride) * stride) {
                return 0.0f;
            }
            const float h00 = heights[y0 * stride + x0];
            const float h10 = heights[y0 * stride + x1];
            const float h01 = heights[y1 * stride + x0];
            const float h11 = heights[y1 * stride + x1];
            return std::lerp(
                std::lerp(h00, h10, tx),
                std::lerp(h01, h11, tx),
                ty);
        }

        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) {

            const MATH::Vec4 transformed = matrix.TransformPoint({
                point.x, point.y, point.z, 1.0f });
            return {
                transformed.x,
                transformed.y,
                transformed.z
            };
        }

        MATH::Vec3 TransformDirection(
            const MATH::Mat4& matrix,
            const MATH::Vec3& direction) {

            const MATH::Vec4 transformed = matrix.TransformPoint({
                direction.x, direction.y, direction.z, 0.0f });
            return MATH::Normalize({
                transformed.x,
                transformed.y,
                transformed.z
            });
        }
    }

    bool SandSurfaceQueryService::SampleLocalHeight(
        const SandSurfaceRuntimeState& state,
        const MATH::Vec2& localXZ,
        float& outHeight,
        MATH::Vec3& outNormal) const noexcept {

        const float halfX = (std::max)(state.halfExtents.x, 1.0e-3f);
        const float halfZ = (std::max)(state.halfExtents.y, 1.0e-3f);
        const float u = localXZ.x / (2.0f * halfX) + 0.5f;
        const float v = localXZ.y / (2.0f * halfZ) + 0.5f;
        const bool inside =
            u >= -1.0e-4f && u <= 1.0f + 1.0e-4f &&
            v >= -1.0e-4f && v <= 1.0f + 1.0e-4f;
        if (!inside) {
            outHeight = 0.0f;
            outNormal = { 0.0f, 1.0f, 0.0f };
            return false;
        }

        const float combined = SampleGrid(state, u, v);
        const float ripple = EvaluateSandSurfaceRippleHeight(
            localXZ,
            state.settings.rippleAmount,
            state.settings.heightScale);
        outHeight = combined + ripple;

        const uint32_t resolution =
            (std::max)(state.gridResolution, 1u);
        const float cellX = (2.0f * halfX) /
            static_cast<float>(resolution);
        const float cellZ = (2.0f * halfZ) /
            static_cast<float>(resolution);
        const float du = cellX / (2.0f * halfX);
        const float dv = cellZ / (2.0f * halfZ);
        const float hL = SampleGrid(state, u - du, v);
        const float hR = SampleGrid(state, u + du, v);
        const float hD = SampleGrid(state, u, v - dv);
        const float hU = SampleGrid(state, u, v + dv);
        const float slopeX = (hR - hL) / (2.0f * cellX);
        const float slopeZ = (hU - hD) / (2.0f * cellZ);
        MATH::Vec3 normal{ -slopeX, 1.0f, -slopeZ };
        const float lengthSquared = MATH::Dot(normal, normal);
        outNormal = lengthSquared > 1.0e-12f
            ? MATH::Normalize(normal)
            : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
        return true;
    }

    bool SandSurfaceQueryService::SampleWorldHeight(
        const MATH::Vec3& worldPosition,
        float& outHeight,
        MATH::Vec3& outNormal) const noexcept {

        outHeight = 0.0f;
        outNormal = { 0.0f, 1.0f, 0.0f };
        const std::shared_ptr<const Snapshot> current =
            snapshot_.load();
        if (!current || current->entries.empty()) {
            return false;
        }

        float bestDistanceSquared = FLT_MAX;
        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (!entry.state) {
                continue;
            }
            const MATH::Vec3 localPosition =
                TransformPoint(entry.state->inverseWorld, worldPosition);
            if (std::abs(localPosition.x) >
                    entry.state->halfExtents.x + 1.0e-3f ||
                std::abs(localPosition.z) >
                    entry.state->halfExtents.y + 1.0e-3f) {
                continue;
            }
            const float distanceSquared =
                localPosition.x * localPosition.x +
                localPosition.z * localPosition.z;
            if (distanceSquared >= bestDistanceSquared) {
                continue;
            }
            float localHeight = 0.0f;
            MATH::Vec3 localNormal{};
            if (SampleLocalHeight(
                    *entry.state,
                    { localPosition.x, localPosition.z },
                    localHeight,
                    localNormal)) {
                bestDistanceSquared = distanceSquared;
                const MATH::Vec3 worldPoint = TransformPoint(
                    entry.state->world,
                    { localPosition.x, localHeight, localPosition.z });
                outHeight = worldPoint.y;
                outNormal = TransformDirection(
                    entry.state->world,
                    localNormal);
            }
        }
        return bestDistanceSquared < FLT_MAX;
    }

    bool SandSurfaceQueryService::TryGetHeights(
        RuntimeObjectHandle object,
        const std::vector<float>*& outHeights) const noexcept {

        outHeights = nullptr;
        const std::shared_ptr<const Snapshot> current =
            snapshot_.load();
        if (!current) {
            return false;
        }
        for (const SandSurfaceSnapshotEntry& entry : current->entries) {
            if (entry.object == object && entry.state) {
                outHeights = &entry.state->heights;
                return true;
            }
        }
        return false;
    }

    uint64_t SandSurfaceQueryService::GetSnapshotRevision() const noexcept {
        return revision_.load();
    }

    std::size_t SandSurfaceQueryService::GetSurfaceCount() const noexcept {
        const std::shared_ptr<const Snapshot> current =
            snapshot_.load();
        return current ? current->entries.size() : 0u;
    }

    void SandSurfaceQueryService::PublishSnapshot(
        std::vector<SandSurfaceSnapshotEntry> entries) {

        auto next = std::make_shared<Snapshot>();
        next->revision = revision_.load() + 1u;
        next->entries = std::move(entries);

        std::lock_guard<std::mutex> lock(publishMutex_);
        snapshot_.store(next);
        revision_.store(next->revision);
    }

    void SandSurfaceQueryService::Clear() {
        std::lock_guard<std::mutex> lock(publishMutex_);
        snapshot_.store(MakeEmptySnapshot());
        revision_.store(revision_.load() + 1u);
    }

} // namespace HIKARI::SAND
