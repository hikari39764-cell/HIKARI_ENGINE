#pragma once

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldOptions.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakePolicy.h"
#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

// 10: 環境はプロファイル資産の参照と初期の時刻・地点・天気になった。
inline constexpr uint32_t kCurrentSceneDocumentVersion = 10;

struct TransformData {
    MATH::Vec3 position{0.0f, 0.0f, 0.0f};
    MATH::Vec3 rotationEulerDeg{0.0f, 0.0f, 0.0f};
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};

    bool operator==(const TransformData&) const = default;
};

struct SceneComponentData {
    std::string type{};
    nlohmann::json properties = nlohmann::json::object();
    SceneComponentId id{};

    bool operator==(const SceneComponentData&) const = default;
};

struct SceneObjectData {
    SceneObjectId id{};
    std::string name{};
    std::string sourcePrefabId{};
    std::optional<SceneObjectId> parent{};
    TransformData transform{};
    std::vector<SceneComponentData> components{};
    bool editorVisible = true;
    bool editorLocked = false;
    bool enabled = true;
    std::string editorFolder{};

    bool operator==(const SceneObjectData&) const = default;

    // 型名が一致する最初の component。無ければ nullptr。
    SceneComponentData* FindComponent(std::string_view type) noexcept {
        const auto found = std::find_if(components.begin(), components.end(),
                                        [type](const SceneComponentData& component) { return component.type == type; });
        return found != components.end() ? &*found : nullptr;
    }
    const SceneComponentData* FindComponent(std::string_view type) const noexcept {
        return const_cast<SceneObjectData*>(this)->FindComponent(type);
    }
};

// 編集操作で document の objects に起きた変化。runtime world の差分反映と差分履歴が使う。
struct SceneObjectChanges {
    std::vector<SceneObjectId> added{};
    std::vector<SceneObjectId> removed{};
    std::vector<SceneObjectId> changed{};

    bool Empty() const noexcept { return added.empty() && removed.empty() && changed.empty(); }
};

struct SceneSystemData {
    std::string systemId{};
    bool enabled = true;
    int executionOrder = 0;
    nlohmann::json settings = nlohmann::json::object();
};

// Baked は焼き込んだ probe（.hlpv）。Realtime は動的 GI が光追で毎フレーム更新する区域で、焼き込まない。
enum class LightProbeVolumeMode : uint8_t { Baked, Realtime };
inline constexpr TEXT::EnumName<LightProbeVolumeMode> kLightProbeVolumeModeNames[] = {
    {LightProbeVolumeMode::Baked, "Baked"},
    {LightProbeVolumeMode::Realtime, "Realtime"},
};

struct LightProbeVolumeSettings {
    bool enabled = true;
    LightProbeVolumeMode mode = LightProbeVolumeMode::Baked;
    MATH::Vec3 origin{-5.0f, 0.5f, -5.0f};
    MATH::Vec3 size{10.0f, 4.0f, 10.0f};
    uint32_t countX = 4;
    uint32_t countY = 2;
    uint32_t countZ = 4;
    float intensity = 1.0f;
    float blendDistance = 1.0f;
    float normalBias = 0.2f;
    uint32_t shOrder = 3;
};

// 場面の光照の焼き込みの質。光照贴图と焼き込み探針の DXR 経路追跡が共用し、変えると焼き込み済みの結果は待ちになる。
struct SceneLightingBakeSettings {
    uint32_t sampleCount = ASSETS::LIGHTING::kSurfaceLightmapDefaultSampleCount;
    uint32_t bounceCount = ASSETS::LIGHTING::kSurfaceLightmapDefaultBounceCount;
    bool operator==(const SceneLightingBakeSettings&) const = default;
};

inline constexpr auto kSceneLightingBakeFields = [](auto&& field) {
    using S = SceneLightingBakeSettings;
    using namespace JSONFIELDS;
    field("sampleCount", &S::sampleCount, Range{16.0, 4096.0}, Label{"Samples"});
    field("bounceCount", &S::bounceCount,
          Range{1.0, static_cast<double>(ASSETS::LIGHTING::kSurfaceLightmapMaximumBounceCount)}, Label{"Bounces"});
};

struct SceneCameraSettings {
    std::optional<SceneObjectId> defaultCameraObjectId{};
};

inline constexpr float kMaximumLightProbeVolumeIntensity = 4.0f;
inline constexpr float kMaximumLightProbeVolumeNormalBias = 0.5f;

inline void ClampLightProbeVolumeSettings(LightProbeVolumeSettings& settings) {
    // 実時間の区域は更新を数フレームに分けるので、焼き込みより細かい格子を取れる。焼き込みは DXR の経路で探針ごとに
    // 256 方向を積むので 16x8x16（2048 個）まで。
    const bool realtime = settings.mode == LightProbeVolumeMode::Realtime;
    settings.countX = std::clamp(settings.countX, 2u, realtime ? 32u : 16u);
    settings.countY = std::clamp(settings.countY, realtime ? 2u : 1u, realtime ? 16u : 8u);
    settings.countZ = std::clamp(settings.countZ, 2u, realtime ? 32u : 16u);
    settings.shOrder = 3u;
    settings.intensity = std::clamp(settings.intensity, 0.0f, kMaximumLightProbeVolumeIntensity);
    settings.normalBias = std::clamp(settings.normalBias, 0.0f, kMaximumLightProbeVolumeNormalBias);
    settings.blendDistance =
        std::clamp(settings.blendDistance, 0.0f,
                   (std::max)(0.0f, (std::min)({settings.size.x, settings.size.y, settings.size.z}) * 0.5f));
    settings.size.x = (std::max)(settings.size.x, 0.1f);
    settings.size.y = (std::max)(settings.size.y, 0.1f);
    settings.size.z = (std::max)(settings.size.z, 0.1f);
}

inline uint32_t GetLightProbeVolumeProbeCount(const LightProbeVolumeSettings& settings) {
    return settings.countX * settings.countY * settings.countZ;
}

inline MATH::Vec3 GetLightProbeVolumeSpacing(const LightProbeVolumeSettings& settings) {
    return {settings.countX > 1u ? settings.size.x / static_cast<float>(settings.countX - 1u) : 1.0f,
            settings.countY > 1u ? settings.size.y / static_cast<float>(settings.countY - 1u) : 1.0f,
            settings.countZ > 1u ? settings.size.z / static_cast<float>(settings.countZ - 1u) : 1.0f};
}

// 焼き込み volume が全面を覆う箱の大きさ（中心は格子と同じ）。格子の外へ半間隔ずつ広い
// （HikariEvaluateBakedVolumeInfluence、そこから blendDistance で消える）。
inline MATH::Vec3 GetBakedLightProbeVolumeCoveredSize(const LightProbeVolumeSettings& settings) {
    return settings.size + GetLightProbeVolumeSpacing(settings);
}

inline MATH::Vec3 GetLightProbeVolumeProbePosition(const LightProbeVolumeSettings& settings, uint32_t x, uint32_t y,
                                                   uint32_t z) {
    const MATH::Vec3 spacing = GetLightProbeVolumeSpacing(settings);
    return {settings.origin.x + spacing.x * static_cast<float>(x),
            settings.origin.y + spacing.y * static_cast<float>(y),
            settings.origin.z + spacing.z * static_cast<float>(z)};
}

struct SceneDocument {
    uint32_t version = kCurrentSceneDocumentVersion;
    std::string sceneName = "Untitled";
    ENVIRONMENT::SceneEnvironmentBinding environment{};
    SceneCameraSettings camera{};
    SceneCinematicsSettings cinematics{};
    SceneLightingBakeSettings lightingBake{};
    std::vector<SceneObjectData> objects{};
    std::vector<SceneSystemData> systems{};
    std::vector<std::string> editorFolders{};
};

} // namespace HIKARI
