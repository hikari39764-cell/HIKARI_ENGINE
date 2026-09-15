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
#include "Assets/Models/HIKARI_ModelAsset.h"
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

    void FillCameraCB(
        const MATH::Mat4& viewProj,
        const MATH::Mat4& inverseViewProj,
        const MATH::Vec3& cameraPosition,
        uint32_t width,
        uint32_t height,
        CameraCB& out) {
        const FrameContext& frame = TIME::GetFrameContext();
        const float screenWidth = static_cast<float>(std::max(1u, width));
        const float screenHeight = static_cast<float>(std::max(1u, height));
        out.viewProj = viewProj;
        out.invViewProj = inverseViewProj;
        out.cameraPos = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f};
        out.timeParams = {TIME::GetUnscaledElapsedSeconds(), frame.unscaledDt,
            frame.gameDt, static_cast<float>(frame.frameIndex)};
        out.simulationTimeParams = {TIME::GetInterpolatedSimulationTimeSeconds(),
            frame.fixedDt, frame.fixedInterpolationAlpha, static_cast<float>(frame.fixedTickIndex)};
        out.screenParams = {screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight};
    }

    namespace {
        MATH::Vec3 ClampProbeBoxSize(const MATH::Vec3& size) {
            return {
                std::max(0.001f, size.x),
                std::max(0.001f, size.y),
                std::max(0.001f, size.z)
            };
        }

        void WriteProbeBoxMinMax(
            const MATH::Vec3& center,
            const MATH::Vec3& size,
            MATH::Vec4& outMin,
            MATH::Vec4& outMax) {

            // Box は shader 側で min/max として扱う。
            const MATH::Vec3 safeSize = ClampProbeBoxSize(size);
            const MATH::Vec3 halfSize = safeSize * 0.5f;
            outMin = {
                center.x - halfSize.x,
                center.y - halfSize.y,
                center.z - halfSize.z,
                0.0f
            };
            outMax = {
                center.x + halfSize.x,
                center.y + halfSize.y,
                center.z + halfSize.z,
                0.0f
            };
        }

        float InfluenceShapeValue(REFLECTION::RuntimeReflectionProbeInfluenceShape shape) {
            return shape == REFLECTION::RuntimeReflectionProbeInfluenceShape::Box ? 1.0f : 0.0f;
        }

        float ProjectionShapeValue(REFLECTION::RuntimeReflectionProbeProjectionShape shape) {
            return shape == REFLECTION::RuntimeReflectionProbeProjectionShape::Box ? 1.0f : 0.0f;
        }
    }

    void FillLightCB(
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::LocalLightListView& localLightList,
        RenderDebugView debugView,
        LightCB& out,
        MeshRendererDebugStats& stats) {
        out = {};
        const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();

        // 空・媒質・bake と同じ地表の太陽入力。透過率はここで一度だけ適用する。
        const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
        const auto dir = solar.sunDirection * -1.0f;
        const float halfAngleSine = std::sin(0.5f * solar.sunAngularRadius);
        out.directionalDir = { dir.x, dir.y, dir.z, halfAngleSine * halfAngleSine };
        out.directionalColor = { solar.sunColor.x, solar.sunColor.y, solar.sunColor.z, 1.0f };
        out.directionalIntensity = solar.sunRadiance;

        const auto ambientColor = SKYRENDERER::ResolveAmbientLightColor(environment);
        const bool skyEnabled = environment.sky.enabled && environment.sky.mode != SkyMode::None;

        MATH::Vec3 fogColor = environment.fog.color;
        if (environment.fog.useSkyHorizonColor && skyEnabled && skyData.valid) {
            fogColor = {
                skyData.horizonColor.x * skyData.exposure,
                skyData.horizonColor.y * skyData.exposure,
                skyData.horizonColor.z * skyData.exposure
            };
        }

        out.ambientColor = { ambientColor.x, ambientColor.y, ambientColor.z, 1.0f };
        out.ambientIntensity = std::max(0.0f, environment.ambient.intensity);
        out.fogColorDensity = {
            fogColor.x,
            fogColor.y,
            fogColor.z,
            std::max(0.0f, environment.fog.density)
        };
        out.fogParams = {
            environment.fog.enabled ? 1.0f : 0.0f,
            std::max(0.0f, environment.fog.startDistance),
            std::max(0.1f, environment.fog.endDistance),
            std::max(0.0f, environment.fog.heightFalloff)
        };
        out.debugView = static_cast<uint32_t>(debugView);
        out.forwardCostMode =
            static_cast<float>(
                static_cast<uint8_t>(
                    RENDER3D::GetRenderQualitySettings().forwardCostMode));
        const ScreenSpaceReflectionSettings& ssr =
            environment.screenSpaceReflections;
        const bool ssrEnabled =
            ssr.enabled &&
            ssr.quality != ScreenSpaceReflectionQuality::Off;
        out.screenSpaceReflectionParams0 = {
            ssrEnabled
                ? std::clamp(ssr.intensity, 0.0f, 2.0f)
                : 0.0f,
            std::clamp(ssr.maxDistance, 1.0f, 200.0f),
            std::clamp(ssr.thickness, 0.0001f, 0.05f),
            std::clamp(ssr.maxRoughness, 0.05f, 1.0f)
        };
        out.screenSpaceReflectionParams1 = {
            std::clamp(ssr.stride, 0.02f, 2.0f),
            std::clamp(ssr.edgeFade, 0.01f, 0.35f),
            static_cast<float>(
                ResolveScreenSpaceReflectionMaxSteps(ssr.quality)),
            0.0f
        };

        out.localLightCount = localLightList.lightCount;
        out.localLightClusterCountX =
            localLightList.clusterCountX;
        out.localLightClusterCountY =
            localLightList.clusterCountY;
        out.localLightClusterCountZ =
            localLightList.clusterCountZ;
        out.localLightTileSizePixels =
            localLightList.tileSizePixels;
        out.localLightClusterIndexCount =
            localLightList.clusterIndexCount;
        out.localLightNearDistance =
            localLightList.nearDistance;
        out.localLightFarDistance =
            localLightList.farDistance;
        out.localLightDepthScale =
            localLightList.depthScale;
        out.localLightDepthBias =
            localLightList.depthBias;

        stats.directionalEnabled = environment.directional.enabled;
        stats.directionalIntensity = out.directionalIntensity;
        stats.ambientIntensity = out.ambientIntensity;
        stats.localLightSourceCount = localLightList.sourceCount;
        stats.localLightUploadableCount =
            localLightList.uploadableCount;
        stats.localLightUploadedCount =
            localLightList.lightCount;
        stats.localLightClampedCount =
            localLightList.rejectedCount;
        stats.pointLightUploadedCount =
            localLightList.pointLightCount;
        stats.spotLightUploadedCount =
            localLightList.spotLightCount;
        stats.localLightClusterCount =
            localLightList.clusterCount;
        stats.localLightClusterIndexCount =
            localLightList.clusterIndexCount;
        stats.localLightClusterOverflowCount =
            localLightList.overflowCount;
    }

    void FillShadowCB(
        const SceneEnvironment& environment,
        uint64_t frameIndex,
        ShadowCB& out) {
        out = {};
        const auto& cascadeMatrices =
            SHADOW::GetDirectionalCascadeLightViewProj();
        for (uint32_t cascadeIndex = 0;
             cascadeIndex < SHADOW::kMaxDirectionalShadowCascades;
             ++cascadeIndex) {
            out.lightViewProj[cascadeIndex] =
                cascadeMatrices[cascadeIndex];
        }
        out.cascadeSplitDistances =
            SHADOW::GetDirectionalCascadeSplitDistances();
        out.cascadeDepthBiasScales = SHADOW::GetDirectionalCascadeDepthBiasScales();
        out.cascadeParams = {
            static_cast<float>(SHADOW::GetDirectionalCascadeCount()),
            std::clamp(
                environment.directionalShadow.cascadeBlend,
                0.0f,
                0.3f),
            0.5f,
            0.5f
        };
        out.enabled = (SHADOW::IsDirectionalShadowEnabled() && environment.directionalShadow.enabled) ? 1u : 0u;
        out.depthBias = std::max(0.0f, environment.directionalShadow.depthBias);
        out.normalBias = std::max(0.0f, environment.directionalShadow.normalBias);
        out.strength = std::clamp(environment.directionalShadow.strength, 0.0f, 1.0f);
        out.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        out.pcfRadius = std::clamp(environment.directionalShadow.pcfRadius, 0.0f, 4.0f);
        out.texelSizeX = SHADOW::GetShadowAtlasTexelSize();
        out.texelSizeY = out.texelSizeX;
        out.edgeFade = 0.04f;
        // ShadowCB.padding[0] carries the frame index for rotated PCF taps.
        out.padding[0] =
            static_cast<float>(frameIndex & 0xffffu);
    }

    void FillSelectedIrradianceVolumes(SkyEnvironmentCB& out) {
        for (auto& volume : out.irradianceVolumes) volume = {};
        const RENDER3D::LightProbeVolumeSamplingMode lightProbeSampling =
            RENDER3D::GetRenderQualitySettings().lightProbeVolumeSampling;
        float lightProbeSamplingMode = 0.0f;
        switch (lightProbeSampling) {
        case RENDER3D::LightProbeVolumeSamplingMode::FullTrilinear:
            lightProbeSamplingMode = 1.0f;
            break;
        case RENDER3D::LightProbeVolumeSamplingMode::FastSmooth:
            lightProbeSamplingMode = 2.0f;
            break;
        case RENDER3D::LightProbeVolumeSamplingMode::Off:
        default:
            lightProbeSamplingMode = 0.0f;
            break;
        }
        for (size_t slot = 0;
             slot <
                 RENDER3D::LIGHTPROBE::
                     kLightProbeVolumeViewSlotCount;
             ++slot) {
            const auto* volume =
                RENDER3D::LIGHTPROBE::GetViewVolume(slot);
            if (volume == nullptr || !volume->valid) {
                continue;
            }
            out.irradianceVolumes[slot] = RENDER3D::LIGHTPROBE::BuildIrradianceVolumeGpuData(
                *volume, lightProbeSamplingMode);
        }

    }

    void FillSkyEnvironmentCB(
        const SceneEnvironment& environment,
        const MATH::Vec3& cameraPosition,
        const MATH::Vec3& viewFocusPosition,
        SkyEnvironmentCB& out) {
        out = {};
        const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
        const bool skyEnabled = environment.sky.enabled && environment.sky.mode != SkyMode::None;
        out.iblParams = {
            skyEnabled && iblData.hasIrradiance ? 1.0f : 0.0f,
            skyEnabled && iblData.hasPrefiltered ? 1.0f : 0.0f,
            iblData.hasBrdfLut ? 1.0f : 0.0f,
            static_cast<float>(std::max<uint32_t>(1u, iblData.prefilteredMipCount))
        };
        REFLECTION::PrepareView(
            cameraPosition,
            viewFocusPosition);
        for (size_t slot = 0;
             slot < REFLECTION::kReflectionProbeViewSlotCount;
             ++slot) {
            const REFLECTION::ReflectionProbeRuntimeData* probe =
                REFLECTION::GetViewProbe(slot);
            if (probe == nullptr) {
                continue;
            }
            ReflectionProbeGpuData& gpu =
                out.reflectionProbes[slot];
            gpu.positionRadius = {
                probe->position.x,
                probe->position.y,
                probe->position.z,
                std::max(0.001f, probe->radius)
            };
            gpu.params = {
                probe->valid ? 1.0f : 0.0f,
                probe->hasPrefiltered ? 1.0f : 0.0f,
                (probe->hasBrdfLut ||
                    iblData.hasBrdfLut) ? 1.0f : 0.0f,
                static_cast<float>(
                    std::max<uint32_t>(
                        1u,
                        probe->prefilteredMipCount))
            };
            gpu.intensity = {
                std::max(0.0f, probe->intensity),
                0.0f,
                0.0f,
                0.0f
            };
            WriteProbeBoxMinMax(
                probe->influenceBoxCenter,
                probe->influenceBoxSize,
                gpu.influenceBoxMin,
                gpu.influenceBoxMax);
            WriteProbeBoxMinMax(
                probe->projectionBoxCenter,
                probe->projectionBoxSize,
                gpu.projectionBoxMin,
                gpu.projectionBoxMax);
            gpu.shapeParams = {
                InfluenceShapeValue(probe->influenceShape),
                ProjectionShapeValue(probe->projectionShape),
                std::max(0.0f, probe->blendDistance),
                static_cast<float>(probe->priority)
            };
        }
        const bool ssaoConfiguredEnabled =
            environment.ambientOcclusion.enabled &&
            environment.ambientOcclusion.mode != SsaoMode::Off;
        out.ambientSamplingParams = {
            ssaoConfiguredEnabled ? 1.0f : 0.0f,
            std::clamp(environment.ambientOcclusion.diffuseStrength, 0.0f, 1.0f),
            std::clamp(environment.ambientOcclusion.specularStrength, 0.0f, 1.0f),
            environment.surfaceLightmaps.enabled ? 1.0f : 0.0f
        };
        RENDER3D::LIGHTPROBE::PrepareView(
            cameraPosition,
            viewFocusPosition);
        FillSelectedIrradianceVolumes(out);

        const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
        if (!skyEnabled || !skyData.valid) {
            return;
        }

        out.skyZenithExposure = {
            skyData.zenithColor.x,
            skyData.zenithColor.y,
            skyData.zenithColor.z,
            skyData.exposure
        };
        out.skyHorizonReflection = {
            skyData.horizonColor.x,
            skyData.horizonColor.y,
            skyData.horizonColor.z,
            skyData.reflectionIntensity
        };
        out.skyGroundAmbient = {
            skyData.groundColor.x,
            skyData.groundColor.y,
            skyData.groundColor.z,
            skyData.ambientFromSky
        };
        out.skyParams = {
            static_cast<float>(skyData.mode),
            skyData.hasCubemap ? 1.0f : 0.0f,
            skyData.horizonPower,
            skyData.yaw
        };
        out.skyTintLighting = {
            skyData.tint.x,
            skyData.tint.y,
            skyData.tint.z,
            skyData.mode == SkyMode::Texture2D &&
                    skyData.textureHandle >= 0
                ? static_cast<float>(skyData.textureHandle + 1)
                : 0.0f
        };

        const bool atmosphereEnabled =
            skyData.atmosphereEnabled &&
            skyData.mode == SkyMode::Atmosphere;
        if (atmosphereEnabled) {
            const auto ground = skyData.atmosphereConstants.groundColor;
            out.skyGroundAmbient = {ground.x, ground.y, ground.z, skyData.ambientFromSky};
            // A stale authored prefiltered/irradiance cubemap is not the
            // same environment as the authoritative atmosphere sun. Keep
            // the BRDF LUT, but force diffuse/specular sky sampling through
            // the shared atmosphere evaluator until dynamic atmosphere IBL
            // baking is available.
            out.iblParams.x = 0.0f;
            out.iblParams.y = 0.0f;
        }
        if (skyData.mode == SkyMode::Texture2D) {
            // A custom panorama without its own convolved artifacts must not
            // reflect a stale/default cubemap. The shared shader sky provider
            // supplies the authored panorama and analytic BRDF fallback.
            out.iblParams.x = 0.0f;
            out.iblParams.y = 0.0f;
        }
        out.atmospherePlanetParams = skyData.atmosphereConstants.planetParams;
        out.atmosphereRayleigh = skyData.atmosphereConstants.rayleighScattering;
        out.atmosphereMie = skyData.atmosphereConstants.mieScattering;
        out.atmosphereOzone = skyData.atmosphereConstants.ozoneAbsorption;
        out.atmosphereOzoneParams = skyData.atmosphereConstants.ozoneParams;
        out.atmosphereSunDirection = skyData.atmosphereConstants.sunDirection;
        out.atmosphereSunColorRadiance = skyData.atmosphereConstants.sunColorRadiance;
        std::copy(std::begin(skyData.gradientIrradiance.coefficients), std::end(skyData.gradientIrradiance.coefficients),
            std::begin(out.gradientSkyIrradiance));

    }

    namespace {
    }

    MATH::Mat4 BuildNormalMatrix(const Transform3D& transform) {
        if (transform.useExplicitMatrix) {
            return MATH::BuildNormalMatrixFromWorld(transform.GetWorldMatrix());
        }

        MATH::Mat4 normalMatrix = MATH::Mat4::Rotate(MATH::NormalizeQ(transform.rotation));
        const MATH::Vec3 s = transform.scale;
        const float invScaleX = (std::abs(s.x) > 1e-6f) ? (1.0f / s.x) : 0.0f;
        const float invScaleY = (std::abs(s.y) > 1e-6f) ? (1.0f / s.y) : 0.0f;
        const float invScaleZ = (std::abs(s.z) > 1e-6f) ? (1.0f / s.z) : 0.0f;
        normalMatrix.m[0][0] *= invScaleX; normalMatrix.m[0][1] *= invScaleX; normalMatrix.m[0][2] *= invScaleX;
        normalMatrix.m[1][0] *= invScaleY; normalMatrix.m[1][1] *= invScaleY; normalMatrix.m[1][2] *= invScaleY;
        normalMatrix.m[2][0] *= invScaleZ; normalMatrix.m[2][1] *= invScaleZ; normalMatrix.m[2][2] *= invScaleZ;
        return normalMatrix;
    }

    void FillFxValues(ObjectCB& obj, const DrawItem& item) {
        obj.fxFlags = item.fxFlags;
        for (size_t i = 0; i < VFX::kMaterialFxUserCount; i++) {
            obj.fxUser[i] = item.fxValues[i];
        }
    }

    void FillMaterialFactorValues(ObjectCB& obj,
        const MaterialAsset* modelMaterial, const Material* runtimeMaterial) {
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

    void FillMaterialValues(
        ObjectCB& obj,
        const MaterialAsset* materialAsset,
        int normalTextureHandle,
        int emissiveTextureHandle,
        int metallicRoughnessTextureHandle,
        int occlusionTextureHandle,
        const MeshMaterialFillContext& ctx) {
        FillMaterialFactorValues(obj, materialAsset, nullptr);
        obj.hasNormalTexture = 0;
        obj.hasEmissiveTexture = 0;
        obj.hasMetallicRoughnessTexture = 0;
        obj.hasOcclusionTexture = 0;

        if (materialAsset == nullptr) {
            return;
        }

        MeshRendererDebugStats* stats = ctx.stats;
        if (normalTextureHandle >= 0 && normalTextureHandle != ctx.fallbackNormalTextureHandle) {
            obj.hasNormalTexture = 1;
            if (stats) {
                ++stats->normalMappedPrimitiveCount;
            }
        }
        if (emissiveTextureHandle >= 0 && emissiveTextureHandle != ctx.fallbackBlackTextureHandle) {
            obj.hasEmissiveTexture = 1;
            if (stats) {
                ++stats->emissiveMappedPrimitiveCount;
            }
        }
        if (metallicRoughnessTextureHandle >= 0 && metallicRoughnessTextureHandle != ctx.fallbackTextureHandle) {
            obj.hasMetallicRoughnessTexture = 1;
        }
        if (occlusionTextureHandle >= 0 && occlusionTextureHandle != ctx.fallbackTextureHandle) {
            obj.hasOcclusionTexture = 1;
        }

    }

    D3D12_GPU_VIRTUAL_ADDRESS UploadJointPalettes(
        const GFX::Context& graphics,
        DX::DynamicUploadBuffer& uploads,
        const std::vector<MATH::Mat4>* palettes,
        size_t paletteCount) {

        if ((paletteCount != 0u && palettes == nullptr) ||
            paletteCount > (std::numeric_limits<size_t>::max)() / sizeof(JointPaletteCB)) {
            return 0u;
        }
        uploads.ResetForFrame(graphics.currentFrameRetireFenceValue);
        const size_t count = (std::max)(size_t(1), paletteCount);
        const auto upload = uploads.Allocate(count * sizeof(JointPaletteCB));
        if (!upload) return 0u;
        auto* destination = static_cast<JointPaletteCB*>(upload.cpuAddress);
        for (size_t index = 0; index < count; ++index) {
            auto& matrices = destination[index].jointMatrices;
            for (auto& matrix : matrices) matrix = MATH::Mat4::Identity();
            if (index < paletteCount) {
                const auto& source = palettes[index];
                std::copy_n(source.data(), (std::min)(source.size(), kMaxJointPaletteMatrices), matrices);
            }
        }
        return upload.gpuAddress;
    }

} // namespace HIKARI::MESHRENDERER
