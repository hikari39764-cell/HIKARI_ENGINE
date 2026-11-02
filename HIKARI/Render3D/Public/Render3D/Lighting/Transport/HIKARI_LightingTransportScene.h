#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <cstdint>
#include <string>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightingBakePolicy.h"
#include "RenderContracts/Lighting/HIKARI_LightmapAtlasLayout.h"
#include "RenderContracts/Lighting/HIKARI_LightmapSurfaceContract.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_SkyAsset.h"
#include "Render3D/Material/HIKARI_MaterialSnapshot.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

struct LightingTransportTriangle {
    MATH::Vec3 position[3]{};
    MATH::Vec3 normal[3]{};
    MATH::Vec2 lightmapUv[3]{};
    // atlas 配置で lightmapUv を変更しても、材質の UV は保持する。
    MATH::Vec2 materialUv0[3]{};
    MATH::Vec2 materialUv1[3]{};
    MATH::Vec4 tangent[3]{};
    uint32_t materialIndex = MATERIAL::kInvalidGpuMaterialIndex;
    uint64_t objectId = 0u;
    uint32_t lightmapAllocationIndex = ASSETS::LIGHTING::kInvalidLightmapSurfaceIndex;
    // 実行時の描画と同じ両面判定。両面の面は裏から当たっても埋まった texel の証拠にしない。
    bool doubleSided = false;
};

struct LightingTransportLightmapPage {
    uint32_t outputResolution = ASSETS::LIGHTING::kSurfaceLightmapDefaultResolution;
    uint32_t uvSet = ASSETS::LIGHTING::kSurfaceLightmapUvSet;
    std::vector<uint32_t> triangleIndices{};
    std::vector<ASSETS::LIGHTING::LightmapSurfaceBinding> surfaceBindings{};
    std::vector<ASSETS::LIGHTING::LightmapAtlasAllocation> atlasAllocations{};
    float requestedTexelsPerWorldUnit = 0.0f;
    float resolvedTexelsPerWorldUnit = 0.0f;
    bool atlasDensityReduced = false;
};

struct LightingTransportSurfaceTarget {
    uint64_t objectId = 0u;
    std::string name{};
    bool eligible = false;
    std::string eligibilityMessage{};
    std::vector<LightingTransportLightmapPage> pages{};
    uint32_t analyzedSurfaceCount = 0u;
    uint32_t eligibleSurfaceCount = 0u;
    uint32_t unsupportedSurfaceCount = 0u;
    uint32_t invalidUvSurfaceCount = 0u;
    // 切り抜きの面は遮蔽と反射だけに参加し、光照贴图は持たない（照明は焼き付け探針から）。
    uint32_t alphaMaskedSurfaceCount = 0u;
    uint32_t blendedSurfaceCount = 0u;
};

// 焼き付け shader（Baking/HIKARI_DxrLightmapCS.hlsl）と共有する光源の種類。
inline constexpr uint32_t kTransportLightSun = 0u;
inline constexpr uint32_t kTransportLightPoint = 1u;
inline constexpr uint32_t kTransportLightSpot = 2u;

struct LightingTransportLight {
    uint32_t type = kTransportLightSun;
    MATH::Vec3 position{};
    float range = 0.0f;
    MATH::Vec3 direction{0.0f, -1.0f, 0.0f};
    float outerConeCos = -1.0f;
    MATH::Vec3 radiance{1.0f, 1.0f, 1.0f};
    float innerConeCos = -1.0f;
    float angularFalloff = 1.0f;
    float sourceRadius = 0.0f;
};

struct LightingTransportEnvironment {
    ATMOSPHERE::AtmosphereLutConstants atmosphere{};
    // 天空光の倍率。
    float skyStrength = 0.0f;
    MATH::Vec4 skyTintExposure{1, 1, 1, 1};
    SkyMode skyMode = SkyMode::None;
    float skyYaw = 0;
    TextureSnapshotSource skyTexture{};
    std::string error{};
};

struct LightingTransportScene {
    std::vector<LightingTransportTriangle> triangles{};
    std::vector<MATERIAL::MaterialSnapshot> materials{};
    std::vector<LightingTransportSurfaceTarget> targets{};
    std::vector<LightingTransportLight> lights{};
    LightingTransportEnvironment environment{};
};

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
