#include "Render3D/Sand/HIKARI_SandSurfaceRenderData.h"

#include "Render3D/Sand/HIKARI_SandSurfaceGpuData.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::SAND {

    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }
    }

    bool SandSurfaceRenderDataEqual(
        const SandSurfaceRenderData& lhs,
        const SandSurfaceRenderData& rhs) noexcept {

        return lhs.gridResolution == rhs.gridResolution &&
            lhs.extents.x == rhs.extents.x &&
            lhs.extents.y == rhs.extents.y &&
            lhs.heightScale == rhs.heightScale &&
            lhs.rimStrength == rhs.rimStrength &&
            lhs.lightTint.x == rhs.lightTint.x &&
            lhs.lightTint.y == rhs.lightTint.y &&
            lhs.lightTint.z == rhs.lightTint.z &&
            lhs.sparkleStrength == rhs.sparkleStrength &&
            lhs.darkTint.x == rhs.darkTint.x &&
            lhs.darkTint.y == rhs.darkTint.y &&
            lhs.darkTint.z == rhs.darkTint.z &&
            lhs.rippleAmount == rhs.rippleAmount &&
            lhs.pitDarkening == rhs.pitDarkening;
    }

    SandSurfaceGpuData BuildSandSurfaceGpuDataSentinel() noexcept {
        SandSurfaceGpuData result{};
        result.schemaVersion = kSandSurfaceGpuSchemaVersion;
        result.flags = 0u;
        result.gridResolution = 0u;
        result.heightOffset = 1u;
        return result;
    }

    SandSurfaceGpuData BuildSandSurfaceGpuData(
        const SandSurfaceRenderData& source,
        uint32_t heightOffset) noexcept {

        SandSurfaceGpuData result{};
        result.schemaVersion = kSandSurfaceGpuSchemaVersion;
        result.flags =
            static_cast<uint32_t>(SandSurfaceGpuFlags::Enabled);
        result.gridResolution =
            std::clamp(source.gridResolution, 1u, 512u);
        result.heightOffset = heightOffset;
        result.extentsScale = {
            (std::max)(FiniteOr(source.extents.x, 20.0f), 0.001f),
            (std::max)(FiniteOr(source.extents.y, 20.0f), 0.001f),
            std::clamp(FiniteOr(source.heightScale, 0.32f), 0.0f, 2.0f),
            std::clamp(FiniteOr(source.rimStrength, 0.28f), 0.0f, 2.0f)
        };
        result.styleLight = {
            std::clamp(FiniteOr(source.lightTint.x, 1.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.lightTint.y, 1.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.lightTint.z, 1.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.sparkleStrength, 0.35f), 0.0f, 2.0f)
        };
        result.styleDark = {
            std::clamp(FiniteOr(source.darkTint.x, 0.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.darkTint.y, 0.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.darkTint.z, 0.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(source.rippleAmount, 0.45f), 0.0f, 3.0f)
        };
        result.stylePit = {
            std::clamp(FiniteOr(source.pitDarkening, 0.32f), 0.0f, 1.0f),
            0.0f,
            0.0f,
            0.0f
        };
        return result;
    }

} // namespace HIKARI::RENDER3D::SAND
