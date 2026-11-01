#include "Render3D/Features/HIKARI_RenderFeaturePlanner.h"

#include <algorithm>

namespace HIKARI::RENDER3D::FEATURES {

namespace {
bool HasInputs(uint32_t available, uint32_t required) noexcept { return (available & required) == required; }

uint64_t SurfacePixelCount(const RenderFeatureAvailability& availability) noexcept {
    return static_cast<uint64_t>((std::max)(1u, availability.width)) *
           static_cast<uint64_t>((std::max)(1u, availability.height));
}

uint64_t SurfaceBytes(const RenderFeatureAvailability& availability) noexcept {
    return SurfacePixelCount(availability) * 8u;
}
} // namespace

const char* ToString(RenderFeatureKind kind) noexcept {
    switch (kind) {
    case RenderFeatureKind::GeometryOutline: return "GeometryOutline";
    case RenderFeatureKind::ScreenOutline: return "ScreenOutline";
    case RenderFeatureKind::VisibilityOverlay: return "VisibilityOverlay";
    case RenderFeatureKind::FaceHairShadow: return "FaceHairShadow";
    case RenderFeatureKind::Count: break;
    }
    return "Unknown";
}

RenderFeaturePlan BuildRenderFeaturePlan(const RenderFeatureRequestSet& requests,
                                         const RenderFeatureAvailability& availability) {
    RenderFeaturePlan plan{};
    for (uint32_t index = 0u; index < static_cast<uint32_t>(RenderFeatureKind::Count); ++index) {
        RenderFeaturePlanEntry& entry = plan.entries[index];
        entry.kind = static_cast<RenderFeatureKind>(index);
        entry.requested = requests.Requests(entry.kind);
        if (!entry.requested) {
            entry.diagnostic = "not requested";
            continue;
        }
        ++plan.requestedCount;
        if ((availability.enabledMask & RenderFeatureBit(entry.kind)) == 0u) {
            entry.diagnostic = "disabled by the view feature policy";
            ++plan.skippedCount;
            continue;
        }

        switch (entry.kind) {
        case RenderFeatureKind::GeometryOutline:
            if (HasInputs(availability.inputMask, RenderFeatureInputBit(RenderFeatureInput::GeometryBackend))) {
                entry.scheduled = true;
                entry.passCount = 1u;
            } else if (HasInputs(availability.inputMask,
                                 RenderFeatureInputBit(RenderFeatureInput::SceneColor) |
                                     RenderFeatureInputBit(RenderFeatureInput::SceneDepth) |
                                     RenderFeatureInputBit(RenderFeatureInput::SurfaceNormals) |
                                     RenderFeatureInputBit(RenderFeatureInput::SurfaceFeatureMask))) {
                entry.scheduled = true;
                entry.compatibilityFallback = true;
                entry.diagnostic = "using the deterministic screen-outline fallback";
                plan.screenCompositePassRequired = true;
                plan.screenOutlinePassRequired = true;
            } else {
                entry.diagnostic = "geometry outline backend or isolated screen inputs unavailable";
            }
            break;
        case RenderFeatureKind::ScreenOutline: {
            const uint32_t inputs = RenderFeatureInputBit(RenderFeatureInput::SceneColor) |
                                    RenderFeatureInputBit(RenderFeatureInput::SceneDepth) |
                                    RenderFeatureInputBit(RenderFeatureInput::SurfaceNormals) |
                                    RenderFeatureInputBit(RenderFeatureInput::SurfaceFeatureMask);
            if (HasInputs(availability.inputMask, inputs)) {
                entry.scheduled = true;
                plan.screenCompositePassRequired = true;
                plan.screenOutlinePassRequired = true;
            } else {
                entry.diagnostic = "scene color, depth, normals, or feature mask unavailable";
            }
            break;
        }
        case RenderFeatureKind::VisibilityOverlay:
            if (HasInputs(availability.inputMask, RenderFeatureInputBit(RenderFeatureInput::VisibilityBackend))) {
                entry.scheduled = true;
                entry.passCount = 1u;
            } else {
                entry.diagnostic = "visibility overlay backend unavailable";
            }
            break;
        case RenderFeatureKind::FaceHairShadow: {
            const uint32_t inputs = RenderFeatureInputBit(RenderFeatureInput::SceneColor) |
                                    RenderFeatureInputBit(RenderFeatureInput::SceneDepth) |
                                    RenderFeatureInputBit(RenderFeatureInput::SurfaceFeatureMask);
            if (HasInputs(availability.inputMask, inputs)) {
                entry.scheduled = true;
                plan.screenCompositePassRequired = true;
                entry.diagnostic = "scheduled in the isolated stylized screen composite";
            } else {
                entry.diagnostic = "scene color, depth, or feature mask unavailable";
            }
            break;
        }
        case RenderFeatureKind::Count: break;
        }

        if (entry.scheduled) {
            ++plan.scheduledCount;
            plan.passCount += entry.passCount;
        } else {
            ++plan.skippedCount;
        }
    }

    if (plan.screenCompositePassRequired) {
        ++plan.passCount;
        plan.requiresFeatureMask = true;
        // R32G32_UINT feature/group mask, R8G8B8A8 per-material outline
        // style, and one R16G16B16A16 scene snapshot.
        plan.transientBytes = SurfaceBytes(availability) * 2u + SurfacePixelCount(availability) * 4u;
        for (RenderFeaturePlanEntry& entry : plan.entries) {
            if (entry.scheduled && (entry.kind == RenderFeatureKind::ScreenOutline ||
                                    entry.kind == RenderFeatureKind::FaceHairShadow || entry.compatibilityFallback)) {
                entry.passCount = 1u;
                entry.transientBytes = plan.transientBytes;
                break;
            }
        }
    }
    return plan;
}

} // namespace HIKARI::RENDER3D::FEATURES
