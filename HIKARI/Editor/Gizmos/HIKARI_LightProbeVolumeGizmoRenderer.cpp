#include "HIKARI_LightProbeVolumeGizmoRenderer.h"

#include <algorithm>
#include <cstdint>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"

namespace HIKARI::EDITOR {

    namespace {

        constexpr uint32_t kSampledProbeCrossCap = 64u;

        uint32_t ModeToStat(LightProbeVolumeOverlayMode mode) {
            switch (mode) {
            case LightProbeVolumeOverlayMode::BoundsOnly: return 1u;
            case LightProbeVolumeOverlayMode::SampledPoints: return 2u;
            case LightProbeVolumeOverlayMode::AllPoints: return 3u;
            case LightProbeVolumeOverlayMode::Off:
            default:
                return 0u;
            }
        }

        unsigned int ResolveVolumeColor(const LightProbeVolumeSettings& settings) {
            const RENDER3D::LIGHTPROBE::LightProbeVolumeRuntimeData& runtime =
                RENDER3D::LIGHTPROBE::GetRuntimeData();
            const RENDER3D::LIGHTING::SceneLightingRuntimeData& lighting =
                RENDER3D::LIGHTING::GetLastLightingRuntimeData();
            const bool runtimeMatchesAuthoring =
                runtime.countX == settings.countX &&
                runtime.countY == settings.countY &&
                runtime.countZ == settings.countZ &&
                runtime.origin.x == settings.origin.x &&
                runtime.origin.y == settings.origin.y &&
                runtime.origin.z == settings.origin.z &&
                runtime.size.x == settings.size.x &&
                runtime.size.y == settings.size.y &&
                runtime.size.z == settings.size.z;

            if (!settings.enabled) {
                return 0x6A6F78AA;
            }
            if (runtime.valid &&
                runtimeMatchesAuthoring &&
                lighting.source == RENDER3D::LIGHTING::LightingRuntimeSource::BakedRuntime) {
                return 0x61E6A8FF;
            }
            if (lighting.lightProbeVolumeLoaded || runtime.probeCount > 0u) {
                return 0xFF4C5CFF;
            }
            return 0x9B6CFFFF;
        }

        RENDERER3D::DEBUG::DebugDepthMode ResolveDepthMode(const ViewportOverlayState& overlays) {
            return overlays.showXRayGizmos
                ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
        }

        void SubmitLine(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            RENDERER3D::DEBUG::SubmitLine3D({
                from,
                to,
                color,
                depthMode
            });
        }

        void SubmitBox(
            const MATH::Vec3& origin,
            const MATH::Vec3& size,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            const MATH::Vec3 p000 = origin;
            const MATH::Vec3 p100{ origin.x + size.x, origin.y, origin.z };
            const MATH::Vec3 p010{ origin.x, origin.y + size.y, origin.z };
            const MATH::Vec3 p110{ origin.x + size.x, origin.y + size.y, origin.z };
            const MATH::Vec3 p001{ origin.x, origin.y, origin.z + size.z };
            const MATH::Vec3 p101{ origin.x + size.x, origin.y, origin.z + size.z };
            const MATH::Vec3 p011{ origin.x, origin.y + size.y, origin.z + size.z };
            const MATH::Vec3 p111{ origin.x + size.x, origin.y + size.y, origin.z + size.z };

            SubmitLine(p000, p100, color, depthMode);
            SubmitLine(p100, p110, color, depthMode);
            SubmitLine(p110, p010, color, depthMode);
            SubmitLine(p010, p000, color, depthMode);
            SubmitLine(p001, p101, color, depthMode);
            SubmitLine(p101, p111, color, depthMode);
            SubmitLine(p111, p011, color, depthMode);
            SubmitLine(p011, p001, color, depthMode);
            SubmitLine(p000, p001, color, depthMode);
            SubmitLine(p100, p101, color, depthMode);
            SubmitLine(p110, p111, color, depthMode);
            SubmitLine(p010, p011, color, depthMode);
        }

        void SubmitProbeCross(
            const MATH::Vec3& position,
            const MATH::Vec3& spacing,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            const float sx = (std::max)(0.05f, spacing.x);
            const float sy = (std::max)(0.05f, spacing.y);
            const float sz = (std::max)(0.05f, spacing.z);
            const float minSpacing = (std::min)((std::min)(sx, sy), sz);
            const float size = (std::clamp)(minSpacing * 0.08f, 0.06f, 0.25f);

            SubmitLine(
                { position.x - size, position.y, position.z },
                { position.x + size, position.y, position.z },
                color,
                depthMode);
            SubmitLine(
                { position.x, position.y - size, position.z },
                { position.x, position.y + size, position.z },
                color,
                depthMode);
            SubmitLine(
                { position.x, position.y, position.z - size },
                { position.x, position.y, position.z + size },
                color,
                depthMode);
        }

        bool ShouldDrawProbePoint(
            LightProbeVolumeOverlayMode mode,
            uint32_t probeIndex,
            uint32_t probeCount,
            uint32_t drawnCount) {

            if (mode == LightProbeVolumeOverlayMode::AllPoints) {
                return true;
            }
            if (mode != LightProbeVolumeOverlayMode::SampledPoints || probeCount == 0u) {
                return false;
            }

            const uint32_t stride = (std::max)(1u, (probeCount + kSampledProbeCrossCap - 1u) / kSampledProbeCrossCap);
            return drawnCount < kSampledProbeCrossCap && (probeIndex % stride) == 0u;
        }

    } // namespace

    void LightProbeVolumeGizmoRenderer::Submit(
        const LightProbeVolumeSettings& sourceSettings,
        const ViewportOverlayState& overlays) const {

        LightProbeVolumeSettings settings = sourceSettings;
        ClampLightProbeVolumeSettings(settings);
        const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);

        if (!overlays.showLightProbeVolume ||
            overlays.lightProbeVolumeMode == LightProbeVolumeOverlayMode::Off) {
            RENDERER3D::DEBUG::SetLightProbeVolumeGizmoStats(
                probeCount,
                0u,
                ModeToStat(LightProbeVolumeOverlayMode::Off),
                false);
            return;
        }

        const unsigned int color = ResolveVolumeColor(settings);
        const MATH::Vec3 spacing = GetLightProbeVolumeSpacing(settings);
        const RENDERER3D::DEBUG::DebugDepthMode depthMode = ResolveDepthMode(overlays);

        // Volume はまず bounds を描き、probe 点は mode に応じて間引く。
        SubmitBox(settings.origin, settings.size, color, depthMode);

        uint32_t drawnProbeCount = 0u;
        uint32_t probeIndex = 0u;
        for (uint32_t z = 0; z < settings.countZ; ++z) {
            for (uint32_t y = 0; y < settings.countY; ++y) {
                for (uint32_t x = 0; x < settings.countX; ++x) {
                    if (ShouldDrawProbePoint(
                            overlays.lightProbeVolumeMode,
                            probeIndex,
                            probeCount,
                            drawnProbeCount)) {
                        SubmitProbeCross(
                            GetLightProbeVolumeProbePosition(settings, x, y, z),
                            spacing,
                            color,
                            depthMode);
                        ++drawnProbeCount;
                    }
                    ++probeIndex;
                }
            }
        }

        RENDERER3D::DEBUG::SetLightProbeVolumeGizmoStats(
            probeCount,
            drawnProbeCount,
            ModeToStat(overlays.lightProbeVolumeMode),
            overlays.lightProbeVolumeMode == LightProbeVolumeOverlayMode::SampledPoints &&
                probeCount > drawnProbeCount);
    }

} // namespace HIKARI::EDITOR
