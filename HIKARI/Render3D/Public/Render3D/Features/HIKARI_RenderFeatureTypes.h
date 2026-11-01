#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace HIKARI::RENDER3D::FEATURES {

enum class RenderFeatureKind : uint8_t {
    GeometryOutline = 0,
    ScreenOutline,
    VisibilityOverlay,
    FaceHairShadow,
    Count,
};

enum class RenderFeatureInput : uint32_t {
    None = 0u,
    SceneColor = 1u << 0u,
    SceneDepth = 1u << 1u,
    SurfaceNormals = 1u << 2u,
    SurfaceFeatureMask = 1u << 3u,
    GeometryBackend = 1u << 4u,
    VisibilityBackend = 1u << 5u,
};

constexpr uint32_t RenderFeatureBit(RenderFeatureKind kind) noexcept { return 1u << static_cast<uint32_t>(kind); }

constexpr uint32_t RenderFeatureInputBit(RenderFeatureInput input) noexcept { return static_cast<uint32_t>(input); }

namespace SurfaceFeatureMaskBits {
inline constexpr uint32_t GeometryOutline = 1u << 0u;
inline constexpr uint32_t ScreenOutline = 1u << 1u;
inline constexpr uint32_t VisibilityOverlay = 1u << 2u;
inline constexpr uint32_t FaceHairShadow = 1u << 3u;
inline constexpr uint32_t RoleFace = 1u << 8u;
inline constexpr uint32_t RoleHair = 1u << 9u;
inline constexpr uint32_t RoleEye = 1u << 10u;
inline constexpr uint32_t RoleSkin = 1u << 11u;
inline constexpr uint32_t RoleCloth = 1u << 12u;
inline constexpr uint32_t RoleHybrid = 1u << 13u;
inline constexpr uint32_t AutoGroup = 1u << 31u;
} // namespace SurfaceFeatureMaskBits

struct RenderFeatureRequestSet {
    uint32_t requestedMask = 0u;
    // Screen-space features require a stable opaque/masked surface sample.
    // Depth-aware and alpha-blended queues remain eligible for geometry
    // features, but do not force creation of the shared surface mask.
    uint32_t opaqueSurfaceRequestedMask = 0u;
    std::array<uint32_t, static_cast<size_t>(RenderFeatureKind::Count)> surfaceCounts{};
    uint32_t postGroupMask = 0u;
    uint32_t isolatedSurfaceCount = 0u;

    bool Requests(RenderFeatureKind kind) const noexcept { return (requestedMask & RenderFeatureBit(kind)) != 0u; }

    bool RequestsOpaqueSurface(RenderFeatureKind kind) const noexcept {
        return (opaqueSurfaceRequestedMask & RenderFeatureBit(kind)) != 0u;
    }
};

struct RenderFeatureAvailability {
    uint32_t enabledMask = 0xffffffffu;
    uint32_t inputMask = 0u;
    uint32_t width = 1u;
    uint32_t height = 1u;
};

struct RenderFeaturePlanEntry {
    RenderFeatureKind kind = RenderFeatureKind::ScreenOutline;
    bool requested = false;
    bool scheduled = false;
    bool fusedIntoOpaque = false;
    bool compatibilityFallback = false;
    uint32_t passCount = 0u;
    uint64_t transientBytes = 0u;
    std::string diagnostic{};
};

struct RenderFeaturePlan {
    std::array<RenderFeaturePlanEntry, static_cast<size_t>(RenderFeatureKind::Count)> entries{};
    uint32_t requestedCount = 0u;
    uint32_t scheduledCount = 0u;
    uint32_t skippedCount = 0u;
    uint32_t passCount = 0u;
    uint64_t transientBytes = 0u;
    bool requiresFeatureMask = false;
    bool screenCompositePassRequired = false;
    // Kept as a narrow compatibility signal for existing diagnostics.
    bool screenOutlinePassRequired = false;
};

const char* ToString(RenderFeatureKind kind) noexcept;

} // namespace HIKARI::RENDER3D::FEATURES
