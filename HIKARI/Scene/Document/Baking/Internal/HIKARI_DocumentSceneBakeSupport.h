#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI::DOCUMENT_SCENE_BAKING {

    const char* ReflectionProbeInfluenceShapeName(
        ReflectionProbeInfluenceShape shape);
    const char* ReflectionProbeProjectionShapeName(
        ReflectionProbeProjectionShape shape);
    const char* ProbeFaceName(uint32_t faceIndex);
    void AddBakeError(
        TOOLS::BAKING::LightingBakeReport& report,
        std::string message);
    std::filesystem::path ReflectionProbeOutputDirectory(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid);
    std::filesystem::path LightProbeOutputDirectory(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid);
    std::filesystem::path LightProbeCaptureDirectory(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid);
    std::filesystem::path LightProbeCapturePath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid,
        uint32_t probeIndex);

    ASSETS::LIGHTING::LightingBakeManifest LoadOrCreateLightingBakeManifest(
        const std::filesystem::path& manifestPath,
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid,
        std::vector<std::string>& warnings);
    void ReplaceReflectionProbeRecord(
        ASSETS::LIGHTING::LightingBakeManifest& manifest,
        const ASSETS::LIGHTING::ReflectionProbeBakeRecord& record);
    void ReplaceLightProbeVolumeRecord(
        ASSETS::LIGHTING::LightingBakeManifest& manifest,
        const ASSETS::LIGHTING::LightProbeBakeRecord& record);

    uint32_t GetLightProbeCaptureFaceCount(
        const LightProbeVolumeSettings& settings);
    MATH::Vec3 GetLightProbePositionByIndex(
        const LightProbeVolumeSettings& settings,
        uint32_t probeIndex);
    float GetLightProbeCaptureFarPlane(
        const LightProbeVolumeSettings& settings);
    void SetBakeJobState(
        TOOLS::BAKING::LightingBakeReport& report,
        TOOLS::BAKING::LightingBakeJobState state);
    Camera3D MakeProbeFaceCamera(
        const MATH::Vec3& position,
        uint32_t faceIndex,
        float farPlane);
    SceneEnvironment MakeProbeCaptureEnvironment(
        const SceneEnvironment& source);

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
