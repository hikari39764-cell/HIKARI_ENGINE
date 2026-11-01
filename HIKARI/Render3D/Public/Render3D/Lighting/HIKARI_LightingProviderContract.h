#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Decal/HIKARI_DecalContract.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"

namespace HIKARI::RENDER3D::LIGHTING {

// Scene は任意数の Provider を保持できる。描画 View は候補を絞り、
// GPU に同時公開する Provider 数だけをこの固定予算で制限する。
inline constexpr size_t kLightingProviderViewSlotCount = 2u;

// solar ray に直交する列と、その列内の累積透過率。未公開は descriptor=0。
struct CloudSolarVisibilityGpuData {
    MATH::Vec4 originDescriptor{0, 0, 0, 0};
    MATH::Vec4 rightExtent{0, 0, 0, 0};
    MATH::Vec4 upResolution{0, 0, 0, 0};
    MATH::Vec4 directionSteps{0, 0, 0, 0};
};
static_assert(sizeof(CloudSolarVisibilityGpuData) == 64);

// 局所光源。太陽と月は環境（SceneEnvironment::frame）が持つ。
// 管は中心を通る線分（長さ length、向き tangent）、長方形は length x height の片面の発光面（法線 direction、
// 幅の向き tangent）。
enum class SceneLightType : uint8_t {
    Point,
    Spot,
    Tube,
    Rect,
};

inline constexpr TEXT::EnumName<SceneLightType> kSceneLightTypeNames[] = {
    {SceneLightType::Point, "Point"},
    {SceneLightType::Spot, "Spot"},
    {SceneLightType::Tube, "Tube"},
    {SceneLightType::Rect, "Rect"},
};

struct SceneLightProvider {
    std::string id{};
    std::string name{};
    bool enabled = false;
    SceneLightType type = SceneLightType::Point;
    MATH::Vec3 position{};
    MATH::Vec3 direction{0.0f, 0.0f, 1.0f};
    // 管の軸・長方形の幅の向き（ローカル +X）。
    MATH::Vec3 tangent{1.0f, 0.0f, 0.0f};
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    // 光度 [cd]。距離の二乗で減衰し、range で滑らかに打ち切る。
    float intensity = 1.0f;
    float range = 5.0f;
    float sourceRadius = 0.0f;
    // 管の長さ・長方形の幅と高さ [m]。
    float length = 1.0f;
    float height = 1.0f;
    bool castsShadows = false;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    int priority = 0;
    // 点・スポットの配光（LightProfile 資産）とスポットのクッキー（Texture 資産）の GUID。
    std::string iesProfile{};
    std::string cookie{};
    // 上の資産の描画用テクスチャの記述子番号 + 1（0 は無し）。描画の直前に LocalTextureResolver が埋める。
    uint32_t iesProfileTexture = 0u;
    uint32_t cookieTexture = 0u;
};

struct ReflectionProbeProvider {
    std::string id{};
    std::string name{};
    bool enabled = false;
    MATH::Vec3 position{};
    float radius = 0.0f;
    float intensity = 1.0f;
    HIKARI::REFLECTION::RuntimeReflectionProbeInfluenceShape influenceShape =
        HIKARI::REFLECTION::RuntimeReflectionProbeInfluenceShape::Sphere;
    HIKARI::REFLECTION::RuntimeReflectionProbeProjectionShape projectionShape =
        HIKARI::REFLECTION::RuntimeReflectionProbeProjectionShape::Infinite;
    MATH::Vec3 influenceBoxCenter{};
    MATH::Vec3 influenceBoxSize{};
    MATH::Vec3 projectionBoxCenter{};
    MATH::Vec3 projectionBoxSize{};
    float blendDistance = 1.0f;
    int priority = 0;
    std::string sourceCubemapAssetId{};
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

struct IrradianceVolumeProvider {
    std::string id{};
    std::string name{};
    bool enabled = false;
    // 動的 GI が光追で更新する区域。焼き込みの読み込みは行わない。
    bool realtime = false;
    MATH::Vec3 origin{};
    MATH::Vec3 size{};
    MATH::Vec3 spacing{};
    uint32_t countX = 0;
    uint32_t countY = 0;
    uint32_t countZ = 0;
    float intensity = 1.0f;
    float blendDistance = 1.0f;
    float normalBias = 0.2f;
    int priority = 0;
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

struct SceneLightingSet {
    std::string activeScenario{"Default"};
    std::vector<SceneLightProvider> lights{};
    // 面へ投影するデカール（sortOrder の昇順）。局所光と同じクラスタと記録バッファで公開する。
    std::vector<DECAL::SceneDecalProvider> decals{};
    std::vector<ReflectionProbeProvider> reflectionProbes{};
    std::vector<IrradianceVolumeProvider> irradianceVolumes{};
};

} // namespace HIKARI::RENDER3D::LIGHTING
