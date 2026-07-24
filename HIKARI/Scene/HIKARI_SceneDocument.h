#pragma once

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    inline constexpr uint32_t kCurrentSceneDocumentVersion = 4;

    struct TransformData {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotationEulerDeg{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct SceneComponentData {
        std::string type{};
        nlohmann::json properties = nlohmann::json::object();
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
    };

    struct SceneSystemData {
        std::string systemId{};
        bool enabled = true;
        int executionOrder = 0;
        nlohmann::json settings = nlohmann::json::object();
    };

    struct LightProbeVolumeSettings {
        bool enabled = true;
        MATH::Vec3 origin{ -5.0f, 0.5f, -5.0f };
        MATH::Vec3 size{ 10.0f, 4.0f, 10.0f };
        uint32_t countX = 4;
        uint32_t countY = 2;
        uint32_t countZ = 4;
        float intensity = 1.0f;
        uint32_t captureResolution = 32;
        uint32_t shOrder = 3;
    };

    struct SceneLightingBakeSettings {
        LightProbeVolumeSettings lightProbeVolume{};
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
        settings.countX = std::clamp(settings.countX, 2u, 8u);
        settings.countY = std::clamp(settings.countY, 1u, 4u);
        settings.countZ = std::clamp(settings.countZ, 2u, 8u);
        settings.captureResolution = NormalizeLightProbeCaptureResolution(settings.captureResolution);
        settings.shOrder = 3u;
        settings.intensity = std::clamp(settings.intensity, 0.0f, 4.0f);
        settings.size.x = (std::max)(settings.size.x, 0.1f);
        settings.size.y = (std::max)(settings.size.y, 0.1f);
        settings.size.z = (std::max)(settings.size.z, 0.1f);
    }

    inline uint32_t GetLightProbeVolumeProbeCount(const LightProbeVolumeSettings& settings) {
        return settings.countX * settings.countY * settings.countZ;
    }

    inline MATH::Vec3 GetLightProbeVolumeSpacing(const LightProbeVolumeSettings& settings) {
        return {
            settings.countX > 1u ? settings.size.x / static_cast<float>(settings.countX - 1u) : 1.0f,
            settings.countY > 1u ? settings.size.y / static_cast<float>(settings.countY - 1u) : 1.0f,
            settings.countZ > 1u ? settings.size.z / static_cast<float>(settings.countZ - 1u) : 1.0f
        };
    }

    inline MATH::Vec3 GetLightProbeVolumeProbePosition(
        const LightProbeVolumeSettings& settings,
        uint32_t x,
        uint32_t y,
        uint32_t z) {

        const MATH::Vec3 spacing = GetLightProbeVolumeSpacing(settings);
        return {
            settings.origin.x + spacing.x * static_cast<float>(x),
            settings.origin.y + spacing.y * static_cast<float>(y),
            settings.origin.z + spacing.z * static_cast<float>(z)
        };
    }

    struct SceneDocument {
        uint32_t version = kCurrentSceneDocumentVersion;
        std::string sceneName = "Untitled";
        SceneEnvironment environment{};
        SceneLightingBakeSettings lightingBake{};
        SceneCameraSettings camera{};
        SceneCinematicsSettings cinematics{};
        std::vector<SceneObjectData> objects{};
        std::vector<SceneSystemData> systems{};
    };

} // namespace HIKARI
