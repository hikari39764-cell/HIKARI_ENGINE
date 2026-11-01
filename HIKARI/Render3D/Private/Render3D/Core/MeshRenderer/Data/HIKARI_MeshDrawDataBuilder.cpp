#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::MESHRENDERER {

void FillCameraCB(const MATH::Mat4& viewProj, const MATH::Mat4& inverseViewProj, const MATH::Vec3& cameraPosition,
                  uint32_t width, uint32_t height, CameraCB& out) {
    const FrameContext& frame = TIME::GetFrameContext();
    const float screenWidth = static_cast<float>(std::max(1u, width));
    const float screenHeight = static_cast<float>(std::max(1u, height));
    out.viewProj = viewProj;
    out.invViewProj = inverseViewProj;
    out.cameraPos = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f};
    out.timeParams = {TIME::GetUnscaledElapsedSeconds(), frame.unscaledDt, frame.gameDt,
                      static_cast<float>(frame.frameIndex)};
    out.simulationTimeParams = {TIME::GetInterpolatedSimulationTimeSeconds(), frame.fixedDt,
                                frame.fixedInterpolationAlpha, static_cast<float>(frame.fixedTickIndex)};
    out.screenParams = {screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight};
}

namespace {
MATH::Vec3 ClampProbeBoxSize(const MATH::Vec3& size) {
    return {std::max(0.001f, size.x), std::max(0.001f, size.y), std::max(0.001f, size.z)};
}

void WriteProbeBoxMinMax(const MATH::Vec3& center, const MATH::Vec3& size, MATH::Vec4& outMin, MATH::Vec4& outMax) {
    // Box は shader 側で min/max として扱う。
    const MATH::Vec3 safeSize = ClampProbeBoxSize(size);
    const MATH::Vec3 halfSize = safeSize * 0.5f;
    outMin = {center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z, 0.0f};
    outMax = {center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z, 0.0f};
}

float InfluenceShapeValue(REFLECTION::RuntimeReflectionProbeInfluenceShape shape) {
    return shape == REFLECTION::RuntimeReflectionProbeInfluenceShape::Box ? 1.0f : 0.0f;
}

float ProjectionShapeValue(REFLECTION::RuntimeReflectionProbeProjectionShape shape) {
    return shape == REFLECTION::RuntimeReflectionProbeProjectionShape::Box ? 1.0f : 0.0f;
}
} // namespace

void FillLightCB(const SceneEnvironment& environment, const RENDER3D::LIGHTING::LocalLightListView& localLightList,
                 RenderDebugView debugView, LightCB& out, MeshRendererDebugStats& stats) {
    out = {};
    out.viewExposure = RENDER3D::COLOR::GetActiveViewExposure();

    // 空・媒質・bake と同じ地表の太陽入力 [lux]。透過率はここで一度だけ適用する。
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    const auto dir = solar.sunDirection * -1.0f;
    const float halfAngleSine = std::sin(0.5f * solar.sunAngularRadius);
    out.directionalDir = {dir.x, dir.y, dir.z, halfAngleSine * halfAngleSine};
    out.directionalColor = {solar.sunColor.x, solar.sunColor.y, solar.sunColor.z, 1.0f};
    out.directionalIntensity = solar.sunRadiance;

    // 環境光は空（天空光）が持つ。一定色の環境光は使わない。
    out.ambientColor = {0.0f, 0.0f, 0.0f, 1.0f};
    out.ambientIntensity = 0.0f;
    // 体積霧が無い時の解析霧。散乱光 = 反照率 × (空の環境光 + 等方位相の太陽光)。
    const FogSettings& fog = environment.fog;
    const MATH::Vec3 skyAmbient = SKYRENDERER::EstimateSkyAmbientRadiance(environment);
    const float sunPhase = solar.sunRadiance / (4.0f * 3.14159265f);
    const MATH::Vec3 fogRadiance = {fog.albedo.x * (skyAmbient.x + solar.sunColor.x * sunPhase),
                                    fog.albedo.y * (skyAmbient.y + solar.sunColor.y * sunPhase),
                                    fog.albedo.z * (skyAmbient.z + solar.sunColor.z * sunPhase)};
    out.fogColorDensity = {fogRadiance.x, fogRadiance.y, fogRadiance.z, std::max(0.0f, fog.density)};
    out.fogParams = {fog.enabled ? 1.0f : 0.0f, std::max(0.0f, fog.startDistance), fog.baseHeight,
                     std::max(0.0f, fog.heightFalloff)};
    out.debugView = static_cast<uint32_t>(debugView);
    out.forwardCostMode =
        static_cast<float>(static_cast<uint8_t>(RENDER3D::GetRenderQualitySettings().forwardCostMode));
    const ScreenSpaceReflectionSettings& ssr = environment.screenSpaceReflections;
    const bool ssrEnabled = ssr.enabled && ssr.quality != ScreenSpaceReflectionQuality::Off;
    out.screenSpaceReflectionParams0 = {
        ssrEnabled ? std::clamp(ssr.intensity, 0.0f, 2.0f) : 0.0f, std::clamp(ssr.maxDistance, 1.0f, 200.0f),
        std::clamp(ssr.thickness, 0.0001f, 0.05f), std::clamp(ssr.maxRoughness, 0.05f, 1.0f)};
    out.screenSpaceReflectionParams1 = {std::clamp(ssr.stride, 0.02f, 2.0f), std::clamp(ssr.edgeFade, 0.01f, 0.35f),
                                        static_cast<float>(ResolveScreenSpaceReflectionMaxSteps(ssr.quality)), 0.0f};
    RENDERSTYLE::PackRenderStyleShading(environment.styleShading, out.styleShading0, out.styleShading1);

    // 記録の総数（光源とデカール）。シェーダーはクラスタを引くかどうかだけに使う。
    out.localLightCount = localLightList.lightCount + localLightList.decalCount;
    out.localLightClusterCountX = localLightList.clusterCountX;
    out.localLightClusterCountY = localLightList.clusterCountY;
    out.localLightClusterCountZ = localLightList.clusterCountZ;
    out.localLightTileSizePixels = localLightList.tileSizePixels;
    out.localLightClusterIndexCount = localLightList.clusterIndexCount;
    out.localLightNearDistance = localLightList.nearDistance;
    out.localLightFarDistance = localLightList.farDistance;
    out.localLightDepthScale = localLightList.depthScale;
    out.localLightDepthBias = localLightList.depthBias;

    stats.directionalEnabled = solar.valid;
    stats.directionalIntensity = out.directionalIntensity;
    stats.ambientIntensity = out.ambientIntensity;
    stats.localLightSourceCount = localLightList.sourceCount;
    stats.localLightUploadableCount = localLightList.uploadableCount;
    stats.localLightUploadedCount = localLightList.lightCount;
    stats.localLightClampedCount = localLightList.rejectedCount;
    stats.pointLightUploadedCount = localLightList.pointLightCount;
    stats.spotLightUploadedCount = localLightList.spotLightCount;
    stats.localLightClusterCount = localLightList.clusterCount;
    stats.localLightClusterIndexCount = localLightList.clusterIndexCount;
    stats.localLightClusterOverflowCount = localLightList.overflowCount;
}

void FillShadowCB(const SceneEnvironment& environment, uint64_t frameIndex, ShadowCB& out) {
    out = {};
    const RENDER3D::DirectionalShadowQualitySettings& quality = RENDER3D::GetRenderQualitySettings().directionalShadow;
    const auto& cascadeMatrices = SHADOW::GetDirectionalCascadeLightViewProj();
    for (uint32_t cascadeIndex = 0; cascadeIndex < SHADOW::kMaxDirectionalShadowCascades; ++cascadeIndex) {
        out.lightViewProj[cascadeIndex] = cascadeMatrices[cascadeIndex];
    }
    out.cascadeSplitDistances = SHADOW::GetDirectionalCascadeSplitDistances();
    out.cascadeDepthBiasScales = SHADOW::GetDirectionalCascadeDepthBiasScales();
    out.cascadeParams = {static_cast<float>(SHADOW::GetDirectionalCascadeCount()),
                         std::clamp(quality.cascadeBlend, 0.0f, 0.3f), 0.5f, 0.5f};
    out.enabled = (SHADOW::IsDirectionalShadowEnabled() && environment.shadows.enabled) ? 1u : 0u;
    out.depthBias = std::max(0.0f, quality.depthBias);
    out.normalBias = std::max(0.0f, quality.normalBias);
    out.strength = std::clamp(environment.shadows.strength, 0.0f, 1.0f);
    out.pcfEnabled = quality.pcfEnabled ? 1u : 0u;
    out.pcfRadius = std::clamp(quality.pcfRadius, 0.0f, 4.0f);
    out.texelSizeX = SHADOW::GetShadowAtlasTexelSize();
    out.texelSizeY = out.texelSizeX;
    out.edgeFade = 0.04f;
    out.frameIndex = static_cast<float>(frameIndex & 0xffffu);
    // 半影の幅は太陽の見かけの大きさから決まる。空・雲と同じ太陽の状態を使う。
    const float sunAngularRadius =
        std::clamp(ATMOSPHERE::BuildAtmosphereSolarState(environment).sunAngularRadius, 0.0f, 0.5f);
    out.lightTangent = quality.penumbraScale > 0.0f ? quality.penumbraScale * std::tan(sunAngularRadius) : 0.0f;
    const ShadowSettings& shadows = environment.shadows;
    out.contactLength = shadows.contactShadows ? std::max(0.0f, shadows.contactShadowLength) : 0.0f;
    out.contactStrength = std::clamp(shadows.contactShadowStrength, 0.0f, 1.0f);
    out.contactThickness = std::max(0.01f, shadows.contactShadowThickness);
}

void FillSelectedIrradianceVolumes(SkyEnvironmentCB& out) {
    for (auto& volume : out.irradianceVolumes)
        volume = {};
    for (auto& volume : out.dynamicGiVolumes)
        volume = {};
    const RENDER3D::LightProbeVolumeSamplingMode lightProbeSampling =
        RENDER3D::GetRenderQualitySettings().lightProbeVolumeSampling;
    float lightProbeSamplingMode = 0.0f;
    switch (lightProbeSampling) {
    case RENDER3D::LightProbeVolumeSamplingMode::FullTrilinear: lightProbeSamplingMode = 1.0f; break;
    case RENDER3D::LightProbeVolumeSamplingMode::FastSmooth: lightProbeSamplingMode = 2.0f; break;
    case RENDER3D::LightProbeVolumeSamplingMode::Off:
    default: lightProbeSamplingMode = 0.0f; break;
    }
    for (size_t slot = 0; slot < RENDER3D::LIGHTPROBE::kLightProbeVolumeViewSlotCount; ++slot) {
        const auto* volume = RENDER3D::LIGHTPROBE::GetViewVolume(slot);
        if (volume == nullptr || !volume->valid) {
            continue;
        }
        out.irradianceVolumes[slot] =
            RENDER3D::LIGHTPROBE::BuildIrradianceVolumeGpuData(*volume, lightProbeSamplingMode);
    }
    const auto dynamicVolumes = RENDER3D::LIGHTPROBE::GetDynamicGiVolumes();
    for (size_t index = 0; index < dynamicVolumes.size(); ++index) {
        if (dynamicVolumes[index].valid)
            out.dynamicGiVolumes[index] =
                RENDER3D::LIGHTPROBE::BuildIrradianceVolumeGpuData(dynamicVolumes[index], lightProbeSamplingMode);
    }
}

void FillSkyEnvironmentCB(const SceneEnvironment& environment, const MATH::Vec3& cameraPosition,
                          const MATH::Vec3& viewFocusPosition, SkyEnvironmentCB& out) {
    out = {};
    const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
    const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
    const bool skyEnabled = skyData.valid && skyData.mode != SkyMode::None;
    out.iblParams = {skyEnabled && iblData.hasIrradiance ? 1.0f : 0.0f,
                     skyEnabled && iblData.hasPrefiltered ? 1.0f : 0.0f, iblData.hasBrdfLut ? 1.0f : 0.0f,
                     static_cast<float>(std::max<uint32_t>(1u, iblData.prefilteredMipCount))};
    REFLECTION::PrepareView(cameraPosition, viewFocusPosition);
    for (size_t slot = 0; slot < REFLECTION::kReflectionProbeViewSlotCount; ++slot) {
        const REFLECTION::ReflectionProbeRuntimeData* probe = REFLECTION::GetViewProbe(slot);
        if (probe == nullptr) {
            continue;
        }
        ReflectionProbeGpuData& gpu = out.reflectionProbes[slot];
        gpu.positionRadius = {probe->position.x, probe->position.y, probe->position.z, std::max(0.001f, probe->radius)};
        gpu.params = {probe->valid ? 1.0f : 0.0f, probe->hasPrefiltered ? 1.0f : 0.0f,
                      (probe->hasBrdfLut || iblData.hasBrdfLut) ? 1.0f : 0.0f,
                      static_cast<float>(std::max<uint32_t>(1u, probe->prefilteredMipCount))};
        gpu.intensity = {std::max(0.0f, probe->intensity), 0.0f, 0.0f, 0.0f};
        WriteProbeBoxMinMax(probe->influenceBoxCenter, probe->influenceBoxSize, gpu.influenceBoxMin,
                            gpu.influenceBoxMax);
        WriteProbeBoxMinMax(probe->projectionBoxCenter, probe->projectionBoxSize, gpu.projectionBoxMin,
                            gpu.projectionBoxMax);
        gpu.shapeParams = {InfluenceShapeValue(probe->influenceShape), ProjectionShapeValue(probe->projectionShape),
                           std::max(0.0f, probe->blendDistance), static_cast<float>(probe->priority)};
    }
    const bool ssaoConfiguredEnabled =
        environment.ambientOcclusion.enabled && environment.ambientOcclusion.mode != SsaoMode::Off;
    out.ambientSamplingParams = {ssaoConfiguredEnabled ? 1.0f : 0.0f,
                                 std::clamp(environment.ambientOcclusion.diffuseStrength, 0.0f, 1.0f),
                                 std::clamp(environment.ambientOcclusion.specularStrength, 0.0f, 1.0f), 1.0f};
    RENDER3D::LIGHTPROBE::PrepareView(cameraPosition, viewFocusPosition);
    FillSelectedIrradianceVolumes(out);

    if (!skyEnabled) {
        return;
    }

    out.skyScales = {skyData.exposure, skyData.reflectionIntensity, skyData.ambientFromSky, 0.0f};
    out.skyParams = {static_cast<float>(skyData.mode), skyData.hasCubemap ? 1.0f : 0.0f, 0.0f, skyData.yaw};
    out.skyTintLighting = {skyData.tint.x, skyData.tint.y, skyData.tint.z,
                           skyData.mode == SkyMode::Texture2D && skyData.textureHandle >= 0
                               ? static_cast<float>(skyData.textureHandle + 1)
                               : 0.0f};

    out.atmospherePlanetParams = skyData.atmosphereConstants.planetParams;
    out.atmosphereRayleigh = skyData.atmosphereConstants.rayleighScattering;
    out.atmosphereMie = skyData.atmosphereConstants.mieScattering;
    out.atmosphereOzone = skyData.atmosphereConstants.ozoneAbsorption;
    out.atmosphereOzoneParams = skyData.atmosphereConstants.ozoneParams;
    out.atmosphereSunDirection = skyData.atmosphereConstants.sunDirection;
    out.atmosphereSunColorRadiance = skyData.atmosphereConstants.sunColorRadiance;
}

void FillMaterialFactorValues(ObjectCB& obj, const MaterialAsset* modelMaterial, const Material* runtimeMaterial) {
    const auto projection = RENDER3D::MATERIAL::BuildMaterialFactorProjection(modelMaterial, runtimeMaterial);
    obj.baseColor = projection.baseColor;
    obj.emissiveFactor = projection.emissiveFactor;
    obj.materialFlags = projection.materialFlags;
    obj.metallicFactor = projection.pbrParams.x;
    obj.roughnessFactor = projection.pbrParams.y;
    obj.occlusionStrength = projection.pbrParams.z;
    obj.alphaCutoff = projection.pbrParams.w;
    obj.normalScale = projection.normalScale;
}

void FillMaterialValues(ObjectCB& obj, const MaterialAsset* materialAsset, int normalTextureHandle,
                        int emissiveTextureHandle, int metallicRoughnessTextureHandle, int occlusionTextureHandle,
                        const MeshMaterialFillContext& ctx) {
    FillMaterialFactorValues(obj, materialAsset, nullptr);
    obj.hasNormalTexture = 0;
    obj.hasEmissiveTexture = 0;
    obj.hasMetallicRoughnessTexture = 0;
    obj.hasOcclusionTexture = 0;

    if (materialAsset == nullptr) {
        return;
    }

    if (normalTextureHandle >= 0 && normalTextureHandle != ctx.fallbackNormalTextureHandle) {
        obj.hasNormalTexture = 1;
    }
    if (emissiveTextureHandle >= 0 && emissiveTextureHandle != ctx.fallbackBlackTextureHandle) {
        obj.hasEmissiveTexture = 1;
    }
    if (metallicRoughnessTextureHandle >= 0 && metallicRoughnessTextureHandle != ctx.fallbackTextureHandle) {
        obj.hasMetallicRoughnessTexture = 1;
    }
    if (occlusionTextureHandle >= 0 && occlusionTextureHandle != ctx.fallbackTextureHandle) {
        obj.hasOcclusionTexture = 1;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS UploadJointPalettes(const GFX::Context& graphics, DX::DynamicUploadBuffer& uploads,
                                              const std::vector<MATH::Mat4>* palettes, size_t paletteCount) {
    if ((paletteCount != 0u && palettes == nullptr) ||
        paletteCount > (std::numeric_limits<size_t>::max)() / sizeof(JointPaletteCB)) {
        return 0u;
    }
    uploads.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const size_t count = (std::max)(size_t(1), paletteCount);
    const auto upload = uploads.Allocate(count * sizeof(JointPaletteCB));
    if (!upload)
        return 0u;
    auto* destination = static_cast<JointPaletteCB*>(upload.cpuAddress);
    for (size_t index = 0; index < count; ++index) {
        auto& matrices = destination[index].jointMatrices;
        for (auto& matrix : matrices)
            matrix = MATH::Mat4::Identity();
        if (index < paletteCount) {
            const auto& source = palettes[index];
            std::copy_n(source.data(), (std::min)(source.size(), kMaxJointPaletteMatrices), matrices);
        }
    }
    return upload.gpuAddress;
}

} // namespace HIKARI::MESHRENDERER
