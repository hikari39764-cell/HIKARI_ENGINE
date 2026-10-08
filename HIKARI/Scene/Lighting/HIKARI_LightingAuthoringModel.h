#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"
#include "Scene/Components/Lighting/HIKARI_LightSettings.h"
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
    // 回転から求めたローカル +Z（光の向き・発光面の法線）と +X（管の軸・長方形の幅）。
    MATH::Vec3 direction{0.0f, 0.0f, 1.0f};
    MATH::Vec3 tangent{1.0f, 0.0f, 0.0f};
    // 作者値。enabled は物体の有効も畳み込んだ値。
    LightSettings settings{};
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
