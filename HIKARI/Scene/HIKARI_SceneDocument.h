#pragma once

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"
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

    bool operator==(const TransformData& other) const noexcept {
        const auto same = [](const MATH::Vec3& a, const MATH::Vec3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        return same(position, other.position) && same(rotationEulerDeg, other.rotationEulerDeg) &&
               same(scale, other.scale);
    }
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
    uint32_t captureResolution = 32;
    uint32_t shOrder = 3;
};

struct SceneCameraSettings {
    std::optional<SceneObjectId> defaultCameraObjectId{};
};

inline uint32_t NormalizeLightProbeCaptureResolution(uint32_t resolution) {
    if (resolution <= 16u) {
        return 16u;
    }
    if (resolution <= 32u) {
        return 32u;
    }
    return 64u;
}

inline void ClampLightProbeVolumeSettings(LightProbeVolumeSettings& settings) {
    // 実時間の区域は更新を数フレームに分けるので、焼き込みより細かい格子を取れる。
    const bool realtime = settings.mode == LightProbeVolumeMode::Realtime;
    settings.countX = std::clamp(settings.countX, 2u, realtime ? 32u : 8u);
    settings.countY = std::clamp(settings.countY, realtime ? 2u : 1u, realtime ? 16u : 4u);
    settings.countZ = std::clamp(settings.countZ, 2u, realtime ? 32u : 8u);
    settings.captureResolution = NormalizeLightProbeCaptureResolution(settings.captureResolution);
    settings.shOrder = 3u;
    settings.intensity = std::clamp(settings.intensity, 0.0f, 4.0f);
    settings.normalBias = std::clamp(settings.normalBias, 0.0f, 0.5f);
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
    std::vector<SceneObjectData> objects{};
    std::vector<SceneSystemData> systems{};
    std::vector<std::string> editorFolders{};
};

} // namespace HIKARI
