#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
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
        RenderDebugView debugView,
        LightCB& out,
        MeshRendererDebugStats& stats) {
        out = {};
        const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();

        MATH::Vec3 dir = MATH::Normalize(environment.directional.direction);
        if (MATH::Length(dir) <= 1e-6f) {
            dir = { 0.0f, -1.0f, 0.0f };
        }

        out.directionalDir = { dir.x, dir.y, dir.z, 0.0f };
        out.directionalColor = { environment.directional.color.x, environment.directional.color.y, environment.directional.color.z, 1.0f };
        out.directionalIntensity = environment.directional.enabled ? std::max(0.0f, environment.directional.intensity) : 0.0f;

        MATH::Vec3 ambientColor = environment.ambient.color;
        if (environment.ambient.useSkyColor && skyData.valid) {
            const MATH::Vec3 skyAmbient = {
                (skyData.horizonColor.x * 0.65f + skyData.groundColor.x * 0.35f) * skyData.exposure * skyData.ambientFromSky,
                (skyData.horizonColor.y * 0.65f + skyData.groundColor.y * 0.35f) * skyData.exposure * skyData.ambientFromSky,
                (skyData.horizonColor.z * 0.65f + skyData.groundColor.z * 0.35f) * skyData.exposure * skyData.ambientFromSky
            };
            const float blend = std::clamp(environment.ambient.skyBlend, 0.0f, 1.0f);
            ambientColor = {
                ambientColor.x * (1.0f - blend) + skyAmbient.x * blend,
                ambientColor.y * (1.0f - blend) + skyAmbient.y * blend,
                ambientColor.z * (1.0f - blend) + skyAmbient.z * blend
            };
        }

        MATH::Vec3 fogColor = environment.fog.color;
        if (environment.fog.useSkyHorizonColor && skyData.valid) {
            fogColor = {
                skyData.horizonColor.x * skyData.exposure,
                skyData.horizonColor.y * skyData.exposure,
                skyData.horizonColor.z * skyData.exposure
            };
        }

        out.ambientColor = { ambientColor.x, ambientColor.y, ambientColor.z, 1.0f };
        out.ambientIntensity = std::max(0.0f, environment.ambient.intensity);
        out.specularParams = {
            std::max(0.0f, environment.specularIntensity),
            std::max(1.0f, environment.specularPower),
            0.0f,
            0.0f
        };
        out.fogColorDensity = {
            fogColor.x,
            fogColor.y,
            fogColor.z,
            std::max(0.0f, environment.fog.density)
        };
        out.fogParams = {
            (environment.fog.enabled &&
                !RENDER3D::VOLUMETRIC::IsVolumetricLightingActive()) ? 1.0f : 0.0f,
            std::max(0.0f, environment.fog.startDistance),
            std::max(0.1f, environment.fog.endDistance),
            std::max(0.0f, environment.fog.heightFalloff)
        };
        out.debugView = static_cast<uint32_t>(debugView);
        out.forwardCostMode =
            static_cast<float>(
                static_cast<uint8_t>(
                    RENDER3D::GetRenderQualitySettings().forwardCostMode));

        constexpr uint32_t kMaxPointLights = 8;
        uint32_t uploadedCount = 0;
        uint32_t uploadableCount = 0;
        for (const PointLight& pointLight : environment.pointLights) {
            if (!pointLight.enabled || pointLight.range <= 0.0f) {
                continue;
            }

            ++uploadableCount;
            if (uploadedCount >= kMaxPointLights) {
                continue;
            }

            out.pointLightPosRange[uploadedCount] = {
                pointLight.position.x,
                pointLight.position.y,
                pointLight.position.z,
                pointLight.range
            };
            out.pointLightColorIntensity[uploadedCount] = {
                pointLight.color.x,
                pointLight.color.y,
                pointLight.color.z,
                std::max(0.0f, pointLight.intensity)
            };
            ++uploadedCount;
        }
        out.pointLightCount = uploadedCount;

        stats.directionalEnabled = environment.directional.enabled;
        stats.directionalIntensity = out.directionalIntensity;
        stats.ambientIntensity = out.ambientIntensity;
        stats.pointLightTotalCount = environment.pointLights.size();
        stats.pointLightUploadedCount = uploadedCount;
        stats.pointLightClampedCount = (uploadableCount > uploadedCount) ? (uploadableCount - uploadedCount) : 0u;
        stats.specularIntensity = out.specularParams.x;
        stats.specularPower = out.specularParams.y;
    }

    void FillShadowCB(const SceneEnvironment& environment, ShadowCB& out) {
        out = {};
        out.lightViewProj = SHADOW::GetDirectionalLightViewProj();
        out.enabled = (SHADOW::IsDirectionalShadowEnabled() && environment.directionalShadow.enabled) ? 1u : 0u;
        out.depthBias = std::max(0.0f, environment.directionalShadow.depthBias);
        out.normalBias = std::max(0.0f, environment.directionalShadow.normalBias);
        out.strength = std::clamp(environment.directionalShadow.strength, 0.0f, 1.0f);
        out.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        out.pcfRadius = std::clamp(environment.directionalShadow.pcfRadius, 0.0f, 4.0f);
        const float resolution = static_cast<float>(std::max(1u, SHADOW::GetShadowResolution()));
        out.texelSizeX = 1.0f / resolution;
        out.texelSizeY = 1.0f / resolution;
        out.edgeFade = 0.04f;
    }

    void FillSkyEnvironmentCB(const SceneEnvironment& environment, SkyEnvironmentCB& out) {
        out = {};
        const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
        out.iblParams = {
            iblData.hasIrradiance ? 1.0f : 0.0f,
            iblData.hasPrefiltered ? 1.0f : 0.0f,
            iblData.hasBrdfLut ? 1.0f : 0.0f,
            static_cast<float>(std::max<uint32_t>(1u, iblData.prefilteredMipCount))
        };
        const bool probeSuppressed = REFLECTION::IsReflectionProbeSamplingSuppressed();
        const REFLECTION::ReflectionProbeRuntimeData probeData =
            probeSuppressed ? REFLECTION::ReflectionProbeRuntimeData{} : REFLECTION::GetActiveProbe();
        const bool hasSharedBrdf = (!probeSuppressed && probeData.hasBrdfLut) || iblData.hasBrdfLut;
        out.reflectionProbePositionRadius = {
            probeData.position.x,
            probeData.position.y,
            probeData.position.z,
            std::max(0.001f, probeData.radius)
        };
        out.reflectionProbeParams = {
            probeData.valid ? 1.0f : 0.0f,
            probeData.hasPrefiltered ? 1.0f : 0.0f,
            hasSharedBrdf ? 1.0f : 0.0f,
            static_cast<float>(std::max<uint32_t>(1u, probeData.prefilteredMipCount))
        };
        out.reflectionProbeIntensity = {
            std::max(0.0f, probeData.intensity),
            0.0f,
            0.0f,
            0.0f
        };
        WriteProbeBoxMinMax(
            probeData.influenceBoxCenter,
            probeData.influenceBoxSize,
            out.reflectionProbeInfluenceBoxMin,
            out.reflectionProbeInfluenceBoxMax);
        WriteProbeBoxMinMax(
            probeData.projectionBoxCenter,
            probeData.projectionBoxSize,
            out.reflectionProbeProjectionBoxMin,
            out.reflectionProbeProjectionBoxMax);
        out.reflectionProbeShapeParams = {
            InfluenceShapeValue(probeData.influenceShape),
            ProjectionShapeValue(probeData.projectionShape),
            std::max(0.0f, probeData.blendDistance),
            static_cast<float>(probeData.priority)
        };
        const bool ssaoConfiguredEnabled =
            environment.ambientOcclusion.enabled &&
            environment.ambientOcclusion.mode != SsaoMode::Off;
        out.aoParams = {
            ssaoConfiguredEnabled ? 1.0f : 0.0f,
            std::clamp(environment.ambientOcclusion.diffuseStrength, 0.0f, 1.0f),
            std::clamp(environment.ambientOcclusion.specularStrength, 0.0f, 1.0f),
            0.0f
        };
        const bool lightProbeValid = RENDER3D::LIGHTPROBE::IsValid();
        const RENDER3D::LightProbeVolumeSamplingMode lightProbeSampling =
            RENDER3D::GetRenderQualitySettings().lightProbeVolumeSampling;
        float lightProbeSamplingMode = 0.0f;
        if (lightProbeValid) {
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
        }
        const RENDER3D::LIGHTPROBE::LightProbeVolumeRuntimeData& lightProbe =
            RENDER3D::LIGHTPROBE::GetRuntimeData();
        out.lightProbeVolumeOrigin = {
            lightProbe.origin.x,
            lightProbe.origin.y,
            lightProbe.origin.z,
            lightProbeSamplingMode
        };
        out.lightProbeVolumeSpacing = {
            std::max(0.0001f, lightProbe.spacing.x),
            std::max(0.0001f, lightProbe.spacing.y),
            std::max(0.0001f, lightProbe.spacing.z),
            lightProbeValid ? std::max(0.0f, lightProbe.intensity) : 0.0f
        };
        out.lightProbeVolumeCounts = {
            lightProbeValid ? static_cast<float>(lightProbe.countX) : 0.0f,
            lightProbeValid ? static_cast<float>(lightProbe.countY) : 0.0f,
            lightProbeValid ? static_cast<float>(lightProbe.countZ) : 0.0f,
            lightProbeValid ? static_cast<float>(lightProbe.probeCount) : 0.0f
        };

        const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
        if (!skyData.valid) {
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

    void FillMaterialValues(
        ObjectCB& obj,
        const MaterialAsset* materialAsset,
        int normalTextureHandle,
        int emissiveTextureHandle,
        int metallicRoughnessTextureHandle,
        int occlusionTextureHandle,
        const MeshMaterialFillContext& ctx) {
        obj.baseColor = materialAsset ? materialAsset->baseColorFactor : MATH::Vec4{ 1, 1, 1, 1 };
        obj.materialFlags = 0;
        obj.alphaCutoff = materialAsset ? materialAsset->alphaCutoff : 0.5f;
        obj.emissiveFactor = { 0.0f, 0.0f, 0.0f, 1.0f };
        obj.hasNormalTexture = 0;
        obj.hasEmissiveTexture = 0;
        obj.normalScale = materialAsset ? materialAsset->normalTexture.scale : 1.0f;
        obj.metallicFactor = materialAsset ? materialAsset->metallicFactor : 0.0f;
        obj.roughnessFactor = materialAsset ? materialAsset->roughnessFactor : 1.0f;
        obj.hasMetallicRoughnessTexture = 0;
        obj.hasOcclusionTexture = 0;
        obj.occlusionStrength = materialAsset ? materialAsset->occlusionTexture.strength : 1.0f;

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
            if (stats) {
                ++stats->metallicRoughnessMappedPrimitiveCount;
            }
        }
        if (occlusionTextureHandle >= 0 && occlusionTextureHandle != ctx.fallbackTextureHandle) {
            obj.hasOcclusionTexture = 1;
            if (stats) {
                ++stats->occlusionMappedPrimitiveCount;
            }
        }

        if (materialAsset->alphaMode == AlphaMode::Mask) {
            obj.materialFlags |= MATERIAL_FEATURES::AlphaMask;
        }
        const bool isUnlit = (materialAsset->featureBits & MATERIAL_FEATURES::Unlit) != 0;
        if (isUnlit) {
            obj.materialFlags |= MATERIAL_FEATURES::Unlit;
            if (stats) {
                ++stats->unlitPrimitiveCount;
            }
        } else if (stats) {
            ++stats->pbrPrimitiveCount;
        }
        if ((materialAsset->featureBits & MATERIAL_FEATURES::Emissive) != 0) {
            obj.materialFlags |= MATERIAL_FEATURES::Emissive;
        }
        if ((materialAsset->featureBits & MATERIAL_FEATURES::SpecularGlossCompatibility) != 0) {
            obj.materialFlags |= MATERIAL_FEATURES::SpecularGlossCompatibility;
        }
        obj.emissiveFactor = {
            materialAsset->emissiveFactor.x,
            materialAsset->emissiveFactor.y,
            materialAsset->emissiveFactor.z,
            materialAsset->emissiveStrength
        };
    }

    size_t UploadJointPalette(
        JointPaletteCB* jointPaletteMapped,
        size_t objectIndex,
        const std::vector<MATH::Mat4>& jointPalette) {
        if (jointPaletteMapped == nullptr) {
            return 0;
        }

        constexpr UINT kJointPaletteStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB));
        uint8_t* dst = reinterpret_cast<uint8_t*>(jointPaletteMapped) + static_cast<size_t>(kJointPaletteStride) * objectIndex;
        JointPaletteCB paletteCb{};
        for (MATH::Mat4& jointMatrix : paletteCb.jointMatrices) {
            jointMatrix = MATH::Mat4::Identity();
        }

        const size_t uploadedCount = std::min(jointPalette.size(), kMaxJointPaletteMatrices);
        for (size_t i = 0; i < uploadedCount; ++i) {
            paletteCb.jointMatrices[i] = jointPalette[i];
        }
        std::memcpy(dst, &paletteCb, sizeof(JointPaletteCB));
        return uploadedCount;
    }

} // namespace HIKARI::MESHRENDERER
