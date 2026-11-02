#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI::DOCUMENT_SCENE_BAKING {

const char* ReflectionProbeInfluenceShapeName(ReflectionProbeInfluenceShape shape);
const char* ReflectionProbeProjectionShapeName(ReflectionProbeProjectionShape shape);
const char* ProbeFaceName(uint32_t faceIndex);
void AddBakeError(TOOLS::BAKING::LightingBakeReport& report, std::string message);
std::filesystem::path ReflectionProbeOutputDirectory(const std::filesystem::path& projectRoot,
                                                     const std::string& sceneGuid);
std::filesystem::path LightmapOutputDirectory(const std::filesystem::path& projectRoot, const std::string& sceneGuid);

ASSETS::LIGHTING::LightingBakeManifest LoadOrCreateLightingBakeManifest(const std::filesystem::path& manifestPath,
                                                                        const std::filesystem::path& projectRoot,
                                                                        const std::string& sceneGuid,
                                                                        std::vector<std::string>& warnings);
void ReplaceReflectionProbeRecord(ASSETS::LIGHTING::LightingBakeManifest& manifest,
                                  const ASSETS::LIGHTING::ReflectionProbeBakeRecord& record);
void ReplaceLightProbeVolumeRecord(ASSETS::LIGHTING::LightingBakeManifest& manifest,
                                   const ASSETS::LIGHTING::LightProbeBakeRecord& record);
void ReplaceLightmapRecord(ASSETS::LIGHTING::LightingBakeManifest& manifest,
                           const ASSETS::LIGHTING::LightmapBakeRecord& record);

MATH::Vec3 GetLightProbePositionByIndex(const LightProbeVolumeSettings& settings, uint32_t probeIndex);
// 探針から当たらなかった方向の距離（可視性の距離の正規化の上限）。体積の大きさの 1.5 倍、4 m 以上。
float GetLightProbeVisibilityDistance(const LightProbeVolumeSettings& settings);
void SetBakeJobState(TOOLS::BAKING::LightingBakeReport& report, TOOLS::BAKING::LightingBakeJobState state);
Camera3D MakeProbeFaceCamera(const MATH::Vec3& position, uint32_t faceIndex, float farPlane);
SceneEnvironment MakeProbeCaptureEnvironment(const SceneEnvironment& source);

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
