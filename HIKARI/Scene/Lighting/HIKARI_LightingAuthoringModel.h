#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::LIGHTING_AUTHORING {

inline constexpr const char* kReflectionProbeComponentType = "ReflectionProbeComponent";
inline constexpr const char* kIrradianceVolumeComponentType = "IrradianceVolumeComponent";
inline constexpr const char* kLightComponentType = "LightComponent";

struct LightAuthoring {
    SceneObjectId objectId{};
    std::string name{};
    bool objectEnabled = true;
    MATH::Vec3 position{};
    MATH::Vec3 rotationEulerDeg{};
    MATH::Vec3 direction{0.0f, 0.0f, 1.0f};
    RENDER3D::LIGHTING::SceneLightType type = RENDER3D::LIGHTING::SceneLightType::Point;
    bool enabled = true;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    // 光束 [lm]。
    float intensity = 800.0f;
    float range = 10.0f;
    float sourceRadius = 0.0f;
    bool castsShadows = true;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    int priority = 0;
};

struct ReflectionProbeAuthoring {
    SceneObjectId objectId{};
    std::string name{};
    MATH::Vec3 position{};
    ReflectionProbeSettings settings{};
    uint32_t captureResolution = 128;
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

struct IrradianceVolumeAuthoring {
    SceneObjectId objectId{};
    std::string name{};
    MATH::Vec3 center{};
    LightProbeVolumeSettings settings{};
    int priority = 0;
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(const SceneDocument& document);
std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(std::span<const SceneObjectData> objects);
std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(const SceneDocument& document);
std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(std::span<const SceneObjectData> objects);
// 焼き込み対象だけ。Realtime の区域は動的 GI が毎フレーム更新する。
std::vector<IrradianceVolumeAuthoring> CollectBakedIrradianceVolumes(const SceneDocument& document);
std::vector<LightAuthoring> CollectLights(const SceneDocument& document);
std::vector<LightAuthoring> CollectLights(std::span<const SceneObjectData> objects);

SceneObjectId AddLight(SceneDocument& document, const LightAuthoring& light);
SceneObjectId AddReflectionProbe(SceneDocument& document, const ReflectionProbeAuthoring& probe);
SceneObjectId AddIrradianceVolume(SceneDocument& document, const IrradianceVolumeAuthoring& volume);

// ライト・反射プローブ・放射照度ボリュームのいずれか（照明ランタイムの再読込が必要な種類）。
bool IsLightingComponentType(std::string_view componentType) noexcept;
const SceneComponentData* FindLightingComponent(const SceneObjectData& object, const char* componentType);
SceneComponentData* FindLightingComponent(SceneObjectData& object, const char* componentType);

} // namespace HIKARI::LIGHTING_AUTHORING
