#include "HIKARI_EnvironmentDiagnostics.h"

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironmentNames.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include <sstream>

namespace HIKARI::RENDER3D::DIAGNOSTICS {

    namespace {
        struct EnvironmentDiagnosticsChangeKey {
            bool skyRendererInitialized = false;
            bool skyAssetFound = false;
            bool skyCubemapLoaded = false;
            bool skyUsingFallback = false;
            bool skyTextureValid = false;
            int skyCubemapHandle = -1;
            int skyTextureHandle = -1;
            std::string activeSkyAsset{};
            std::string activeSkyTexturePath{};

            bool iblValid = false;
            bool iblHasIrradiance = false;
            bool iblHasPrefiltered = false;
            bool iblHasBrdfLut = false;
            int irradianceHandle = -1;
            int prefilteredHandle = -1;
            int brdfLutHandle = -1;
            uint32_t prefilteredMipCount = 1;
            uint32_t irradianceMipCount = 0;
            uint32_t prefilteredActualMipCount = 0;
            uint32_t brdfLutMipCount = 0;
            DXGI_FORMAT irradianceFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
            bool prefilteredMipMismatch = false;

            bool reflectionProbeEnabled = false;
            bool reflectionProbeValid = false;
            bool reflectionProbeHasPrefiltered = false;
            bool reflectionProbeHasBrdfLut = false;
            int reflectionProbePrefilteredHandle = -1;
            int reflectionProbeBrdfLutHandle = -1;
            uint32_t reflectionProbeMipCount = 1;
            uint32_t reflectionProbeActualMipCount = 0;
            uint32_t reflectionProbeBrdfLutMipCount = 0;
            DXGI_FORMAT reflectionProbePrefilteredFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT reflectionProbeBrdfLutFormat = DXGI_FORMAT_UNKNOWN;
            bool reflectionProbeMipMismatch = false;
            float reflectionProbeRadius = 0.0f;
            float reflectionProbeIntensity = 0.0f;
            std::string reflectionProbeSourceAssetId{};
            std::string reflectionProbePrefilteredPath{};
            float reflectionProbePositionX = 0.0f;
            float reflectionProbePositionY = 0.0f;
            float reflectionProbePositionZ = 0.0f;
            std::string reflectionProbeInfluenceShape{ "Sphere" };
            std::string reflectionProbeProjectionShape{ "Infinite" };
            bool reflectionProbeInfluenceBoxValid = true;
            bool reflectionProbeProjectionBoxValid = true;
            float reflectionProbeInfluenceBoxCenterX = 0.0f;
            float reflectionProbeInfluenceBoxCenterY = 0.0f;
            float reflectionProbeInfluenceBoxCenterZ = 0.0f;
            float reflectionProbeInfluenceBoxSizeX = 0.0f;
            float reflectionProbeInfluenceBoxSizeY = 0.0f;
            float reflectionProbeInfluenceBoxSizeZ = 0.0f;
            float reflectionProbeProjectionBoxCenterX = 0.0f;
            float reflectionProbeProjectionBoxCenterY = 0.0f;
            float reflectionProbeProjectionBoxCenterZ = 0.0f;
            float reflectionProbeProjectionBoxSizeX = 0.0f;
            float reflectionProbeProjectionBoxSizeY = 0.0f;
            float reflectionProbeProjectionBoxSizeZ = 0.0f;
            float reflectionProbeBlendDistance = 0.0f;
            int reflectionProbePriority = 0;

            bool lightingBakeManifestFound = false;
            std::string lightingBakeManifestPath{};
            uint32_t bakedReflectionProbeCount = 0;
            uint32_t bakedLightProbeCount = 0;
            uint32_t bakedLightmapCount = 0;
            std::string lightingRuntimeSource{};
            bool lightProbeVolumeValid = false;
            bool lightProbeVolumeSrvReady = false;
            bool lightProbeVolumeHasGpuBuffer = false;
            uint32_t lightProbeVolumeProbeCount = 0;
            uint32_t lightProbeVolumeCountX = 0;
            uint32_t lightProbeVolumeCountY = 0;
            uint32_t lightProbeVolumeCountZ = 0;
            uint64_t lightProbeVolumeSrvHeapPtr = 0;
            uint64_t lightProbeVolumeBufferPtr = 0;
            std::string lightProbeVolumePath{};

            bool ssaoEnabled = false;
            bool ssaoValid = false;
            bool ssaoSuppressed = false;
            std::string ssaoMode{};
            uint32_t ssaoWidth = 0;
            uint32_t ssaoHeight = 0;
            uint32_t ssaoInternalWidth = 0;
            uint32_t ssaoInternalHeight = 0;
            bool ssaoHalfResolution = false;
            uint32_t ssaoSampleCount = 0;
            uint32_t ssaoBlurIterations = 0;
            float ssaoRadius = 0.0f;
            float ssaoStrength = 0.0f;
            float ssaoPower = 0.0f;

            bool shadowEnabled = false;
            uint32_t shadowResolution = 0;

            bool bloomEnabled = false;
            bool bloomInitialized = false;
            bool bloomFailed = false;
            bool toneMappingEnabled = false;
            std::string antiAliasingMode{};
            bool fxaaEnabled = false;
            bool hasRecentRenderErrors = false;
        };

        bool gHasLastEnvironmentKey = false;
        EnvironmentDiagnosticsChangeKey gLastEnvironmentKey{};

        bool operator==(const EnvironmentDiagnosticsChangeKey& lhs, const EnvironmentDiagnosticsChangeKey& rhs) {
            return lhs.skyRendererInitialized == rhs.skyRendererInitialized &&
                lhs.skyAssetFound == rhs.skyAssetFound &&
                lhs.skyCubemapLoaded == rhs.skyCubemapLoaded &&
                lhs.skyUsingFallback == rhs.skyUsingFallback &&
                lhs.skyTextureValid == rhs.skyTextureValid &&
                lhs.skyCubemapHandle == rhs.skyCubemapHandle &&
                lhs.skyTextureHandle == rhs.skyTextureHandle &&
                lhs.activeSkyAsset == rhs.activeSkyAsset &&
                lhs.activeSkyTexturePath == rhs.activeSkyTexturePath &&
                lhs.iblValid == rhs.iblValid &&
                lhs.iblHasIrradiance == rhs.iblHasIrradiance &&
                lhs.iblHasPrefiltered == rhs.iblHasPrefiltered &&
                lhs.iblHasBrdfLut == rhs.iblHasBrdfLut &&
                lhs.irradianceHandle == rhs.irradianceHandle &&
                lhs.prefilteredHandle == rhs.prefilteredHandle &&
                lhs.brdfLutHandle == rhs.brdfLutHandle &&
                lhs.prefilteredMipCount == rhs.prefilteredMipCount &&
                lhs.irradianceMipCount == rhs.irradianceMipCount &&
                lhs.prefilteredActualMipCount == rhs.prefilteredActualMipCount &&
                lhs.brdfLutMipCount == rhs.brdfLutMipCount &&
                lhs.irradianceFormat == rhs.irradianceFormat &&
                lhs.prefilteredFormat == rhs.prefilteredFormat &&
                lhs.brdfLutFormat == rhs.brdfLutFormat &&
                lhs.prefilteredMipMismatch == rhs.prefilteredMipMismatch &&
                lhs.reflectionProbeEnabled == rhs.reflectionProbeEnabled &&
                lhs.reflectionProbeValid == rhs.reflectionProbeValid &&
                lhs.reflectionProbeHasPrefiltered == rhs.reflectionProbeHasPrefiltered &&
                lhs.reflectionProbeHasBrdfLut == rhs.reflectionProbeHasBrdfLut &&
                lhs.reflectionProbePrefilteredHandle == rhs.reflectionProbePrefilteredHandle &&
                lhs.reflectionProbeBrdfLutHandle == rhs.reflectionProbeBrdfLutHandle &&
                lhs.reflectionProbeMipCount == rhs.reflectionProbeMipCount &&
                lhs.reflectionProbeActualMipCount == rhs.reflectionProbeActualMipCount &&
                lhs.reflectionProbeBrdfLutMipCount == rhs.reflectionProbeBrdfLutMipCount &&
                lhs.reflectionProbePrefilteredFormat == rhs.reflectionProbePrefilteredFormat &&
                lhs.reflectionProbeBrdfLutFormat == rhs.reflectionProbeBrdfLutFormat &&
                lhs.reflectionProbeMipMismatch == rhs.reflectionProbeMipMismatch &&
                lhs.reflectionProbeRadius == rhs.reflectionProbeRadius &&
                lhs.reflectionProbeIntensity == rhs.reflectionProbeIntensity &&
                lhs.reflectionProbeSourceAssetId == rhs.reflectionProbeSourceAssetId &&
                lhs.reflectionProbePrefilteredPath == rhs.reflectionProbePrefilteredPath &&
                lhs.reflectionProbePositionX == rhs.reflectionProbePositionX &&
                lhs.reflectionProbePositionY == rhs.reflectionProbePositionY &&
                lhs.reflectionProbePositionZ == rhs.reflectionProbePositionZ &&
                lhs.reflectionProbeInfluenceShape == rhs.reflectionProbeInfluenceShape &&
                lhs.reflectionProbeProjectionShape == rhs.reflectionProbeProjectionShape &&
                lhs.reflectionProbeInfluenceBoxValid == rhs.reflectionProbeInfluenceBoxValid &&
                lhs.reflectionProbeProjectionBoxValid == rhs.reflectionProbeProjectionBoxValid &&
                lhs.reflectionProbeInfluenceBoxCenterX == rhs.reflectionProbeInfluenceBoxCenterX &&
                lhs.reflectionProbeInfluenceBoxCenterY == rhs.reflectionProbeInfluenceBoxCenterY &&
                lhs.reflectionProbeInfluenceBoxCenterZ == rhs.reflectionProbeInfluenceBoxCenterZ &&
                lhs.reflectionProbeInfluenceBoxSizeX == rhs.reflectionProbeInfluenceBoxSizeX &&
                lhs.reflectionProbeInfluenceBoxSizeY == rhs.reflectionProbeInfluenceBoxSizeY &&
                lhs.reflectionProbeInfluenceBoxSizeZ == rhs.reflectionProbeInfluenceBoxSizeZ &&
                lhs.reflectionProbeProjectionBoxCenterX == rhs.reflectionProbeProjectionBoxCenterX &&
                lhs.reflectionProbeProjectionBoxCenterY == rhs.reflectionProbeProjectionBoxCenterY &&
                lhs.reflectionProbeProjectionBoxCenterZ == rhs.reflectionProbeProjectionBoxCenterZ &&
                lhs.reflectionProbeProjectionBoxSizeX == rhs.reflectionProbeProjectionBoxSizeX &&
                lhs.reflectionProbeProjectionBoxSizeY == rhs.reflectionProbeProjectionBoxSizeY &&
                lhs.reflectionProbeProjectionBoxSizeZ == rhs.reflectionProbeProjectionBoxSizeZ &&
                lhs.reflectionProbeBlendDistance == rhs.reflectionProbeBlendDistance &&
                lhs.reflectionProbePriority == rhs.reflectionProbePriority &&
                lhs.lightingBakeManifestFound == rhs.lightingBakeManifestFound &&
                lhs.lightingBakeManifestPath == rhs.lightingBakeManifestPath &&
                lhs.bakedReflectionProbeCount == rhs.bakedReflectionProbeCount &&
                lhs.bakedLightProbeCount == rhs.bakedLightProbeCount &&
                lhs.bakedLightmapCount == rhs.bakedLightmapCount &&
                lhs.lightingRuntimeSource == rhs.lightingRuntimeSource &&
                lhs.lightProbeVolumeValid == rhs.lightProbeVolumeValid &&
                lhs.lightProbeVolumeSrvReady == rhs.lightProbeVolumeSrvReady &&
                lhs.lightProbeVolumeHasGpuBuffer == rhs.lightProbeVolumeHasGpuBuffer &&
                lhs.lightProbeVolumeProbeCount == rhs.lightProbeVolumeProbeCount &&
                lhs.lightProbeVolumeCountX == rhs.lightProbeVolumeCountX &&
                lhs.lightProbeVolumeCountY == rhs.lightProbeVolumeCountY &&
                lhs.lightProbeVolumeCountZ == rhs.lightProbeVolumeCountZ &&
                lhs.lightProbeVolumeSrvHeapPtr == rhs.lightProbeVolumeSrvHeapPtr &&
                lhs.lightProbeVolumeBufferPtr == rhs.lightProbeVolumeBufferPtr &&
                lhs.lightProbeVolumePath == rhs.lightProbeVolumePath &&
                lhs.ssaoEnabled == rhs.ssaoEnabled &&
                lhs.ssaoValid == rhs.ssaoValid &&
                lhs.ssaoSuppressed == rhs.ssaoSuppressed &&
                lhs.ssaoMode == rhs.ssaoMode &&
                lhs.ssaoWidth == rhs.ssaoWidth &&
                lhs.ssaoHeight == rhs.ssaoHeight &&
                lhs.ssaoInternalWidth == rhs.ssaoInternalWidth &&
                lhs.ssaoInternalHeight == rhs.ssaoInternalHeight &&
                lhs.ssaoHalfResolution == rhs.ssaoHalfResolution &&
                lhs.ssaoSampleCount == rhs.ssaoSampleCount &&
                lhs.ssaoBlurIterations == rhs.ssaoBlurIterations &&
                lhs.ssaoRadius == rhs.ssaoRadius &&
                lhs.ssaoStrength == rhs.ssaoStrength &&
                lhs.ssaoPower == rhs.ssaoPower &&
                lhs.shadowEnabled == rhs.shadowEnabled &&
                lhs.shadowResolution == rhs.shadowResolution &&
                lhs.bloomEnabled == rhs.bloomEnabled &&
                lhs.bloomInitialized == rhs.bloomInitialized &&
                lhs.bloomFailed == rhs.bloomFailed &&
                lhs.toneMappingEnabled == rhs.toneMappingEnabled &&
                lhs.antiAliasingMode == rhs.antiAliasingMode &&
                lhs.fxaaEnabled == rhs.fxaaEnabled &&
                lhs.hasRecentRenderErrors == rhs.hasRecentRenderErrors;
        }

        bool operator!=(const EnvironmentDiagnosticsChangeKey& lhs, const EnvironmentDiagnosticsChangeKey& rhs) {
            return !(lhs == rhs);
        }

        const char* BoolText(bool value) {
            return value ? "true" : "false";
        }

        const char* ReflectionProbeInfluenceShapeText(REFLECTION::RuntimeReflectionProbeInfluenceShape shape) {
            return shape == REFLECTION::RuntimeReflectionProbeInfluenceShape::Box ? "Box" : "Sphere";
        }

        const char* ReflectionProbeProjectionShapeText(REFLECTION::RuntimeReflectionProbeProjectionShape shape) {
            return shape == REFLECTION::RuntimeReflectionProbeProjectionShape::Box ? "Box" : "Infinite";
        }

        bool HasUsableBoxSize(float x, float y, float z) {
            return x > 0.001f && y > 0.001f && z > 0.001f;
        }

        const char* ReasonText(const char* reason) {
            return (reason && reason[0] != '\0') ? reason : "Manual";
        }

        const char* LightProbeGizmoModeText(uint32_t mode) {
            switch (mode) {
            case 1u:
                return "BoundsOnly";
            case 2u:
                return "SampledPoints";
            case 3u:
                return "AllPoints";
            case 0u:
            default:
                return "Off";
            }
        }

        void LogInfoLine(const std::string& message) {
            HIKARI_LOG_INFO(message);
        }

        void LogWarnLine(const std::string& message) {
            HIKARI_LOG_WARN(message);
        }

        EnvironmentDiagnosticsChangeKey MakeChangeKey(const EnvironmentDiagnosticsSnapshot& snapshot) {
            EnvironmentDiagnosticsChangeKey key{};
            key.skyRendererInitialized = snapshot.skyRendererInitialized;
            key.skyAssetFound = snapshot.skyAssetFound;
            key.skyCubemapLoaded = snapshot.skyCubemapLoaded;
            key.skyUsingFallback = snapshot.skyUsingFallback;
            key.skyTextureValid = snapshot.skyTextureValid;
            key.skyCubemapHandle = snapshot.skyCubemapHandle;
            key.skyTextureHandle = snapshot.skyTextureHandle;
            key.activeSkyAsset = snapshot.activeSkyAsset;
            key.activeSkyTexturePath = snapshot.activeSkyTexturePath;

            key.iblValid = snapshot.iblValid;
            key.iblHasIrradiance = snapshot.iblHasIrradiance;
            key.iblHasPrefiltered = snapshot.iblHasPrefiltered;
            key.iblHasBrdfLut = snapshot.iblHasBrdfLut;
            key.irradianceHandle = snapshot.irradianceHandle;
            key.prefilteredHandle = snapshot.prefilteredHandle;
            key.brdfLutHandle = snapshot.brdfLutHandle;
            key.prefilteredMipCount = snapshot.prefilteredMipCount;
            key.irradianceMipCount = snapshot.irradianceMipCount;
            key.prefilteredActualMipCount = snapshot.prefilteredActualMipCount;
            key.brdfLutMipCount = snapshot.brdfLutMipCount;
            key.irradianceFormat = snapshot.irradianceFormat;
            key.prefilteredFormat = snapshot.prefilteredFormat;
            key.brdfLutFormat = snapshot.brdfLutFormat;
            key.prefilteredMipMismatch = snapshot.prefilteredMipMismatch;

            key.reflectionProbeEnabled = snapshot.reflectionProbeEnabled;
            key.reflectionProbeValid = snapshot.reflectionProbeValid;
            key.reflectionProbeHasPrefiltered = snapshot.reflectionProbeHasPrefiltered;
            key.reflectionProbeHasBrdfLut = snapshot.reflectionProbeHasBrdfLut;
            key.reflectionProbePrefilteredHandle = snapshot.reflectionProbePrefilteredHandle;
            key.reflectionProbeBrdfLutHandle = snapshot.reflectionProbeBrdfLutHandle;
            key.reflectionProbeMipCount = snapshot.reflectionProbeMipCount;
            key.reflectionProbeActualMipCount = snapshot.reflectionProbeActualMipCount;
            key.reflectionProbeBrdfLutMipCount = snapshot.reflectionProbeBrdfLutMipCount;
            key.reflectionProbePrefilteredFormat = snapshot.reflectionProbePrefilteredFormat;
            key.reflectionProbeBrdfLutFormat = snapshot.reflectionProbeBrdfLutFormat;
            key.reflectionProbeMipMismatch = snapshot.reflectionProbeMipMismatch;
            key.reflectionProbeRadius = snapshot.reflectionProbeRadius;
            key.reflectionProbeIntensity = snapshot.reflectionProbeIntensity;
            key.reflectionProbeSourceAssetId = snapshot.reflectionProbeSourceAssetId;
            key.reflectionProbePrefilteredPath = snapshot.reflectionProbePrefilteredPath;
            key.reflectionProbePositionX = snapshot.reflectionProbePositionX;
            key.reflectionProbePositionY = snapshot.reflectionProbePositionY;
            key.reflectionProbePositionZ = snapshot.reflectionProbePositionZ;
            key.reflectionProbeInfluenceShape = snapshot.reflectionProbeInfluenceShape;
            key.reflectionProbeProjectionShape = snapshot.reflectionProbeProjectionShape;
            key.reflectionProbeInfluenceBoxValid = snapshot.reflectionProbeInfluenceBoxValid;
            key.reflectionProbeProjectionBoxValid = snapshot.reflectionProbeProjectionBoxValid;
            key.reflectionProbeInfluenceBoxCenterX = snapshot.reflectionProbeInfluenceBoxCenterX;
            key.reflectionProbeInfluenceBoxCenterY = snapshot.reflectionProbeInfluenceBoxCenterY;
            key.reflectionProbeInfluenceBoxCenterZ = snapshot.reflectionProbeInfluenceBoxCenterZ;
            key.reflectionProbeInfluenceBoxSizeX = snapshot.reflectionProbeInfluenceBoxSizeX;
            key.reflectionProbeInfluenceBoxSizeY = snapshot.reflectionProbeInfluenceBoxSizeY;
            key.reflectionProbeInfluenceBoxSizeZ = snapshot.reflectionProbeInfluenceBoxSizeZ;
            key.reflectionProbeProjectionBoxCenterX = snapshot.reflectionProbeProjectionBoxCenterX;
            key.reflectionProbeProjectionBoxCenterY = snapshot.reflectionProbeProjectionBoxCenterY;
            key.reflectionProbeProjectionBoxCenterZ = snapshot.reflectionProbeProjectionBoxCenterZ;
            key.reflectionProbeProjectionBoxSizeX = snapshot.reflectionProbeProjectionBoxSizeX;
            key.reflectionProbeProjectionBoxSizeY = snapshot.reflectionProbeProjectionBoxSizeY;
            key.reflectionProbeProjectionBoxSizeZ = snapshot.reflectionProbeProjectionBoxSizeZ;
            key.reflectionProbeBlendDistance = snapshot.reflectionProbeBlendDistance;
            key.reflectionProbePriority = snapshot.reflectionProbePriority;

            key.lightingBakeManifestFound = snapshot.lightingBakeManifestFound;
            key.lightingBakeManifestPath = snapshot.lightingBakeManifestPath;
            key.bakedReflectionProbeCount = snapshot.bakedReflectionProbeCount;
            key.bakedLightProbeCount = snapshot.bakedLightProbeCount;
            key.bakedLightmapCount = snapshot.bakedLightmapCount;
            key.lightingRuntimeSource = snapshot.lightingRuntimeSource;
            key.lightProbeVolumeValid = snapshot.lightProbeVolumeValid;
            key.lightProbeVolumeSrvReady = snapshot.lightProbeVolumeSrvReady;
            key.lightProbeVolumeHasGpuBuffer = snapshot.lightProbeVolumeHasGpuBuffer;
            key.lightProbeVolumeProbeCount = snapshot.lightProbeVolumeProbeCount;
            key.lightProbeVolumeCountX = snapshot.lightProbeVolumeCountX;
            key.lightProbeVolumeCountY = snapshot.lightProbeVolumeCountY;
            key.lightProbeVolumeCountZ = snapshot.lightProbeVolumeCountZ;
            key.lightProbeVolumeSrvHeapPtr = snapshot.lightProbeVolumeSrvHeapPtr;
            key.lightProbeVolumeBufferPtr = snapshot.lightProbeVolumeBufferPtr;
            key.lightProbeVolumePath = snapshot.lightProbeVolumePath;

            key.ssaoEnabled = snapshot.ssaoEnabled;
            key.ssaoValid = snapshot.ssaoValid;
            key.ssaoSuppressed = snapshot.ssaoSuppressed;
            key.ssaoMode = snapshot.ssaoMode;
            key.ssaoWidth = snapshot.ssaoWidth;
            key.ssaoHeight = snapshot.ssaoHeight;
            key.ssaoInternalWidth = snapshot.ssaoInternalWidth;
            key.ssaoInternalHeight = snapshot.ssaoInternalHeight;
            key.ssaoHalfResolution = snapshot.ssaoHalfResolution;
            key.ssaoSampleCount = snapshot.ssaoSampleCount;
            key.ssaoBlurIterations = snapshot.ssaoBlurIterations;
            key.ssaoRadius = snapshot.ssaoRadius;
            key.ssaoStrength = snapshot.ssaoStrength;
            key.ssaoPower = snapshot.ssaoPower;

            key.shadowEnabled = snapshot.shadowEnabled;
            key.shadowResolution = snapshot.shadowResolution;

            key.bloomEnabled = snapshot.bloomEnabled;
            key.bloomInitialized = snapshot.bloomInitialized;
            key.bloomFailed = snapshot.bloomFailed;
            key.toneMappingEnabled = snapshot.toneMappingEnabled;
            key.antiAliasingMode = snapshot.antiAliasingMode;
            key.fxaaEnabled = snapshot.fxaaEnabled;
            key.hasRecentRenderErrors = snapshot.recentRenderErrorCount > 0;
            return key;
        }
    }

    EnvironmentDiagnosticsSnapshot CaptureEnvironmentSnapshot(const SceneEnvironment* environment) {
        EnvironmentDiagnosticsSnapshot snapshot{};

        // Sky/IBL の runtime 状態を 1 回分の snapshot として集約する。
        const SKYRENDERER::SkyRendererDebugState& sky = SKYRENDERER::GetDebugState();
        snapshot.skyRendererInitialized = sky.initialized;
        snapshot.skyLastRenderSubmitted = sky.lastRenderSubmitted;
        snapshot.skyAssetFound = sky.skyAssetFound;
        snapshot.skyCubemapLoaded = sky.cubemapLoaded;
        snapshot.skyUsingFallback = sky.usingFallback;
        snapshot.skyTextureValid = sky.textureValid;
        snapshot.skyCubemapHandle = sky.cubemapHandle;
        snapshot.skyTextureHandle = sky.textureHandle;
        snapshot.skyDrawCount = sky.drawCount;
        snapshot.activeSkyAsset = sky.activeSkyAsset;
        snapshot.activeSkyTexturePath = sky.activeTexturePath;

        const IBL::IblEnvironmentData& ibl = IBL::GetEnvironmentData();
        snapshot.iblValid = ibl.valid;
        snapshot.iblHasIrradiance = ibl.hasIrradiance;
        snapshot.iblHasPrefiltered = ibl.hasPrefiltered;
        snapshot.iblHasBrdfLut = ibl.hasBrdfLut;
        snapshot.irradianceHandle = ibl.irradianceHandle;
        snapshot.prefilteredHandle = ibl.prefilteredHandle;
        snapshot.brdfLutHandle = ibl.brdfLutHandle;
        snapshot.prefilteredMipCount = ibl.prefilteredMipCount;
        snapshot.irradianceMipCount = ibl.irradianceMipCount;
        snapshot.prefilteredActualMipCount = ibl.prefilteredActualMipCount;
        snapshot.brdfLutMipCount = ibl.brdfLutMipCount;
        snapshot.irradianceFormat = ibl.irradianceFormat;
        snapshot.prefilteredFormat = ibl.prefilteredFormat;
        snapshot.brdfLutFormat = ibl.brdfLutFormat;
        snapshot.prefilteredMipMismatch = ibl.prefilteredMipMismatch;

        const REFLECTION::ReflectionProbeRuntimeData& probe = REFLECTION::GetActiveProbe();
        snapshot.reflectionProbeEnabled = probe.enabled;
        snapshot.reflectionProbeValid = probe.valid;
        snapshot.reflectionProbeHasPrefiltered = probe.hasPrefiltered;
        snapshot.reflectionProbeHasBrdfLut = probe.hasBrdfLut;
        snapshot.reflectionProbePrefilteredHandle = probe.prefilteredHandle;
        snapshot.reflectionProbeBrdfLutHandle = probe.brdfLutHandle;
        snapshot.reflectionProbeMipCount = probe.prefilteredMipCount;
        snapshot.reflectionProbeActualMipCount = probe.prefilteredActualMipCount;
        snapshot.reflectionProbeBrdfLutMipCount = probe.brdfLutMipCount;
        snapshot.reflectionProbePrefilteredFormat = probe.prefilteredFormat;
        snapshot.reflectionProbeBrdfLutFormat = probe.brdfLutFormat;
        snapshot.reflectionProbeMipMismatch = probe.prefilteredMipMismatch;
        snapshot.reflectionProbeRadius = probe.radius;
        snapshot.reflectionProbeIntensity = probe.intensity;
        snapshot.reflectionProbeSourceAssetId = probe.sourceAssetId;
        snapshot.reflectionProbePrefilteredPath = probe.prefilteredPath;
        snapshot.reflectionProbePositionX = probe.position.x;
        snapshot.reflectionProbePositionY = probe.position.y;
        snapshot.reflectionProbePositionZ = probe.position.z;
        snapshot.reflectionProbeInfluenceShape = ReflectionProbeInfluenceShapeText(probe.influenceShape);
        snapshot.reflectionProbeProjectionShape = ReflectionProbeProjectionShapeText(probe.projectionShape);
        snapshot.reflectionProbeInfluenceBoxValid =
            probe.influenceShape != REFLECTION::RuntimeReflectionProbeInfluenceShape::Box ||
            HasUsableBoxSize(probe.influenceBoxSize.x, probe.influenceBoxSize.y, probe.influenceBoxSize.z);
        snapshot.reflectionProbeProjectionBoxValid =
            probe.projectionShape != REFLECTION::RuntimeReflectionProbeProjectionShape::Box ||
            HasUsableBoxSize(probe.projectionBoxSize.x, probe.projectionBoxSize.y, probe.projectionBoxSize.z);
        snapshot.reflectionProbeInfluenceBoxCenterX = probe.influenceBoxCenter.x;
        snapshot.reflectionProbeInfluenceBoxCenterY = probe.influenceBoxCenter.y;
        snapshot.reflectionProbeInfluenceBoxCenterZ = probe.influenceBoxCenter.z;
        snapshot.reflectionProbeInfluenceBoxSizeX = probe.influenceBoxSize.x;
        snapshot.reflectionProbeInfluenceBoxSizeY = probe.influenceBoxSize.y;
        snapshot.reflectionProbeInfluenceBoxSizeZ = probe.influenceBoxSize.z;
        snapshot.reflectionProbeProjectionBoxCenterX = probe.projectionBoxCenter.x;
        snapshot.reflectionProbeProjectionBoxCenterY = probe.projectionBoxCenter.y;
        snapshot.reflectionProbeProjectionBoxCenterZ = probe.projectionBoxCenter.z;
        snapshot.reflectionProbeProjectionBoxSizeX = probe.projectionBoxSize.x;
        snapshot.reflectionProbeProjectionBoxSizeY = probe.projectionBoxSize.y;
        snapshot.reflectionProbeProjectionBoxSizeZ = probe.projectionBoxSize.z;
        snapshot.reflectionProbeBlendDistance = probe.blendDistance;
        snapshot.reflectionProbePriority = probe.priority;

        const LIGHTING::SceneLightingRuntimeData& lighting = LIGHTING::GetLastLightingRuntimeData();
        snapshot.lightingBakeManifestFound = lighting.bakeManifestLoaded;
        snapshot.lightingBakeManifestPath = lighting.bakeManifestPath;
        snapshot.bakedReflectionProbeCount = lighting.bakedReflectionProbeCount;
        snapshot.bakedLightProbeCount = lighting.bakedLightProbeCount;
        snapshot.bakedLightmapCount = lighting.bakedLightmapCount;
        snapshot.lightingRuntimeSource = LIGHTING::ToString(lighting.source);
        const LIGHTPROBE::LightProbeVolumeDebugState lightProbe = LIGHTPROBE::GetDebugState();
        snapshot.lightProbeVolumeValid = lightProbe.valid;
        snapshot.lightProbeVolumeSrvReady = lightProbe.srvReady;
        snapshot.lightProbeVolumeHasGpuBuffer = lightProbe.hasBuffer;
        snapshot.lightProbeVolumeProbeCount = lightProbe.probeCount;
        snapshot.lightProbeVolumeCountX = lightProbe.countX;
        snapshot.lightProbeVolumeCountY = lightProbe.countY;
        snapshot.lightProbeVolumeCountZ = lightProbe.countZ;
        snapshot.lightProbeVolumeSrvHeapPtr = lightProbe.srvHeapPtr;
        snapshot.lightProbeVolumeBufferPtr = lightProbe.bufferPtr;
        snapshot.lightProbeVolumePath = lightProbe.sourcePath;

        const SCREENSPACE::SsaoDebugState& ssao = SCREENSPACE::GetSsaoDebugState();
        snapshot.ssaoEnabled = ssao.enabled;
        snapshot.ssaoValid = ssao.valid;
        snapshot.ssaoSuppressed = ssao.suppressed;
        snapshot.ssaoMode = GetSsaoModeName(ssao.mode);
        snapshot.ssaoWidth = ssao.width;
        snapshot.ssaoHeight = ssao.height;
        snapshot.ssaoInternalWidth = ssao.internalWidth;
        snapshot.ssaoInternalHeight = ssao.internalHeight;
        snapshot.ssaoHalfResolution = ssao.halfResolution;
        snapshot.ssaoSampleCount = ssao.sampleCount;
        snapshot.ssaoBlurIterations = ssao.blurIterations;
        snapshot.ssaoRadius = ssao.radius;
        snapshot.ssaoStrength = ssao.strength;
        snapshot.ssaoPower = ssao.power;

        const RENDERER3D::DEBUG::DebugRendererFrameStats& debugStats = RENDERER3D::DEBUG::GetDebugRendererFrameStats();
        snapshot.debugSubmittedLineCount = debugStats.submittedLineCount;
        snapshot.debugExpandedLineCount = debugStats.expandedLineCount;
        snapshot.debugDepthTestLineCount = debugStats.depthTestLineCount;
        snapshot.debugXRayLineCount = debugStats.xrayLineCount;
        snapshot.debugLightProbeGizmoTotalPointCount = debugStats.lightProbeGizmoTotalPointCount;
        snapshot.debugLightProbeGizmoDrawnPointCount = debugStats.lightProbeGizmoDrawnPointCount;
        snapshot.debugLightProbeGizmoMode = debugStats.lightProbeGizmoMode;
        snapshot.debugLightProbeGizmoCapped = debugStats.lightProbeGizmoCapped;

        const SHADOW::ShadowMapDebugStats& shadow = SHADOW::GetDebugStats();
        snapshot.shadowEnabled = shadow.enabled && SHADOW::IsDirectionalShadowEnabled();
        snapshot.shadowResolution = shadow.resolution;
        snapshot.shadowSubmittedCasterCount = shadow.submittedCasterCount;
        snapshot.shadowStaticCasterDrawCount = shadow.staticCasterDrawCount;
        snapshot.shadowSkinnedCasterDrawCount = shadow.skinnedCasterDrawCount;
        snapshot.shadowAlphaMaskCasterDrawCount = shadow.alphaMaskCasterDrawCount;
        snapshot.shadowStrength = shadow.strength;
        snapshot.shadowDepthBias = shadow.depthBias;
        snapshot.shadowNormalBias = shadow.normalBias;

        const POST::PostSystem::BloomDebugStats& bloom = POST::PostSystem::GetBloomDebugStats();
        snapshot.bloomEnabled = bloom.enabled;
        snapshot.bloomInitialized = bloom.initialized;
        snapshot.bloomFailed = bloom.failed;
        snapshot.bloomPassCount = bloom.passCount;
        snapshot.bloomTextureWidth = bloom.textureWidth;
        snapshot.bloomTextureHeight = bloom.textureHeight;
        snapshot.bloomThreshold = bloom.threshold;
        snapshot.bloomIntensity = bloom.intensity;
        snapshot.bloomRadius = bloom.radius;

        if (environment) {
            snapshot.toneMappingEnabled = environment->toneMapping.enabled;
        }
        const RenderQualitySettings& renderQuality = GetRenderQualitySettings();
        snapshot.antiAliasingMode =
            RenderAntiAliasingModeLabel(renderQuality.antiAliasingMode);
        const POST::PostSystem::FxaaSettings& fxaa = POST::PostSystem::GetFxaaSettings();
        snapshot.fxaaEnabled =
            IsFxaaAntiAliasingMode(renderQuality.antiAliasingMode);
        snapshot.fxaaEdgeThreshold = fxaa.edgeThreshold;
        snapshot.fxaaEdgeThresholdMin = fxaa.edgeThresholdMin;
        snapshot.fxaaSubpixelQuality = fxaa.subpixelQuality;

        snapshot.recentRenderErrorCount = static_cast<uint32_t>(DEBUGLOG::GetRecentRenderErrors(64).size());
        return snapshot;
    }

    void LogEnvironmentSnapshot(const char* reason, const SceneEnvironment* environment) {
        const EnvironmentDiagnosticsSnapshot snapshot = CaptureEnvironmentSnapshot(environment);
        const char* safeReason = ReasonText(reason);

        // 手動 dump 時は runtime 状態をまとめて log に出す。
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics] reason=" << safeReason;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Sky]"
                << " initialized=" << BoolText(snapshot.skyRendererInitialized)
                << " submitted=" << BoolText(snapshot.skyLastRenderSubmitted)
                << " assetFound=" << BoolText(snapshot.skyAssetFound)
                << " cubemapLoaded=" << BoolText(snapshot.skyCubemapLoaded)
                << " textureValid=" << BoolText(snapshot.skyTextureValid)
                << " fallback=" << BoolText(snapshot.skyUsingFallback)
                << " cubemapHandle=" << snapshot.skyCubemapHandle
                << " textureHandle=" << snapshot.skyTextureHandle
                << " draws=" << snapshot.skyDrawCount
                << " asset=" << snapshot.activeSkyAsset
                << " texture=" << snapshot.activeSkyTexturePath;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][IBL]"
                << " valid=" << BoolText(snapshot.iblValid)
                << " irradiance=" << BoolText(snapshot.iblHasIrradiance)
                << " prefiltered=" << BoolText(snapshot.iblHasPrefiltered)
                << " brdf=" << BoolText(snapshot.iblHasBrdfLut)
                << " irradianceHandle=" << snapshot.irradianceHandle
                << " prefilteredHandle=" << snapshot.prefilteredHandle
                << " brdfHandle=" << snapshot.brdfLutHandle
                << " mips=" << snapshot.prefilteredMipCount
                << " actualMips=" << snapshot.irradianceMipCount << "/"
                << snapshot.prefilteredActualMipCount << "/"
                << snapshot.brdfLutMipCount
                << " formats=" << GFX::FormatToString(snapshot.irradianceFormat) << "/"
                << GFX::FormatToString(snapshot.prefilteredFormat) << "/"
                << GFX::FormatToString(snapshot.brdfLutFormat)
                << " mipMismatch=" << BoolText(snapshot.prefilteredMipMismatch);
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][ReflectionProbe]"
                << " enabled=" << BoolText(snapshot.reflectionProbeEnabled)
                << " valid=" << BoolText(snapshot.reflectionProbeValid)
                << " prefiltered=" << BoolText(snapshot.reflectionProbeHasPrefiltered)
                << " brdf=" << BoolText(snapshot.reflectionProbeHasBrdfLut)
                << " prefilteredHandle=" << snapshot.reflectionProbePrefilteredHandle
                << " brdfHandle=" << snapshot.reflectionProbeBrdfLutHandle
                << " mips=" << snapshot.reflectionProbeMipCount
                << " actualMips=" << snapshot.reflectionProbeActualMipCount << "/"
                << snapshot.reflectionProbeBrdfLutMipCount
                << " formats=" << GFX::FormatToString(snapshot.reflectionProbePrefilteredFormat) << "/"
                << GFX::FormatToString(snapshot.reflectionProbeBrdfLutFormat)
                << " mipMismatch=" << BoolText(snapshot.reflectionProbeMipMismatch)
                << " radius=" << snapshot.reflectionProbeRadius
                << " intensity=" << snapshot.reflectionProbeIntensity
                << " position=" << snapshot.reflectionProbePositionX << ","
                << snapshot.reflectionProbePositionY << ","
                << snapshot.reflectionProbePositionZ
                << " influenceShape=" << snapshot.reflectionProbeInfluenceShape
                << " projectionShape=" << snapshot.reflectionProbeProjectionShape
                << " influenceBoxValid=" << BoolText(snapshot.reflectionProbeInfluenceBoxValid)
                << " projectionBoxValid=" << BoolText(snapshot.reflectionProbeProjectionBoxValid)
                << " influenceBox=" << snapshot.reflectionProbeInfluenceBoxCenterX << ","
                << snapshot.reflectionProbeInfluenceBoxCenterY << ","
                << snapshot.reflectionProbeInfluenceBoxCenterZ << "/"
                << snapshot.reflectionProbeInfluenceBoxSizeX << ","
                << snapshot.reflectionProbeInfluenceBoxSizeY << ","
                << snapshot.reflectionProbeInfluenceBoxSizeZ
                << " projectionBox=" << snapshot.reflectionProbeProjectionBoxCenterX << ","
                << snapshot.reflectionProbeProjectionBoxCenterY << ","
                << snapshot.reflectionProbeProjectionBoxCenterZ << "/"
                << snapshot.reflectionProbeProjectionBoxSizeX << ","
                << snapshot.reflectionProbeProjectionBoxSizeY << ","
                << snapshot.reflectionProbeProjectionBoxSizeZ
                << " blendDistance=" << snapshot.reflectionProbeBlendDistance
                << " priority=" << snapshot.reflectionProbePriority
                << " source=" << snapshot.reflectionProbeSourceAssetId
                << " prefilteredPath=" << snapshot.reflectionProbePrefilteredPath;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][LightingRuntime]"
                << " source=" << snapshot.lightingRuntimeSource
                << " manifestFound=" << BoolText(snapshot.lightingBakeManifestFound)
                << " manifest=" << snapshot.lightingBakeManifestPath
                << " bakedProbes=" << snapshot.bakedReflectionProbeCount
                << " bakedLightProbes=" << snapshot.bakedLightProbeCount
                << " bakedLightmaps=" << snapshot.bakedLightmapCount;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][LightProbe]"
                << " valid=" << BoolText(snapshot.lightProbeVolumeValid)
                << " srvReady=" << BoolText(snapshot.lightProbeVolumeSrvReady)
                << " hasBuffer=" << BoolText(snapshot.lightProbeVolumeHasGpuBuffer)
                << " probes=" << snapshot.lightProbeVolumeProbeCount
                << " grid=" << snapshot.lightProbeVolumeCountX << "x"
                << snapshot.lightProbeVolumeCountY << "x"
                << snapshot.lightProbeVolumeCountZ
                << " srvHeap=0x" << std::hex << snapshot.lightProbeVolumeSrvHeapPtr
                << " buffer=0x" << snapshot.lightProbeVolumeBufferPtr << std::dec
                << " path=" << snapshot.lightProbeVolumePath;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][SSAO]"
                << " enabled=" << BoolText(snapshot.ssaoEnabled)
                << " valid=" << BoolText(snapshot.ssaoValid)
                << " suppressed=" << BoolText(snapshot.ssaoSuppressed)
                << " mode=" << snapshot.ssaoMode
                << " screen=" << snapshot.ssaoWidth << "x" << snapshot.ssaoHeight
                << " work=" << snapshot.ssaoInternalWidth << "x" << snapshot.ssaoInternalHeight
                << " half=" << BoolText(snapshot.ssaoHalfResolution)
                << " samples=" << snapshot.ssaoSampleCount
                << " blur=" << snapshot.ssaoBlurIterations
                << " radius=" << snapshot.ssaoRadius
                << " strength=" << snapshot.ssaoStrength
                << " power=" << snapshot.ssaoPower;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][DebugOverlay]"
                << " submittedLines=" << snapshot.debugSubmittedLineCount
                << " expandedLines=" << snapshot.debugExpandedLineCount
                << " depthLines=" << snapshot.debugDepthTestLineCount
                << " xrayLines=" << snapshot.debugXRayLineCount
                << " lightProbePoints=" << snapshot.debugLightProbeGizmoDrawnPointCount
                << "/" << snapshot.debugLightProbeGizmoTotalPointCount
                << " lightProbeMode=" << LightProbeGizmoModeText(snapshot.debugLightProbeGizmoMode)
                << " lightProbeCapped=" << BoolText(snapshot.debugLightProbeGizmoCapped);
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Shadow]"
                << " enabled=" << BoolText(snapshot.shadowEnabled)
                << " resolution=" << snapshot.shadowResolution
                << " casters=" << snapshot.shadowSubmittedCasterCount
                << " staticDraws=" << snapshot.shadowStaticCasterDrawCount
                << " skinnedDraws=" << snapshot.shadowSkinnedCasterDrawCount
                << " alphaMaskDraws=" << snapshot.shadowAlphaMaskCasterDrawCount
                << " strength=" << snapshot.shadowStrength
                << " depthBias=" << snapshot.shadowDepthBias
                << " normalBias=" << snapshot.shadowNormalBias;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Bloom]"
                << " enabled=" << BoolText(snapshot.bloomEnabled)
                << " initialized=" << BoolText(snapshot.bloomInitialized)
                << " failed=" << BoolText(snapshot.bloomFailed)
                << " passes=" << snapshot.bloomPassCount
                << " size=" << snapshot.bloomTextureWidth << "x" << snapshot.bloomTextureHeight
                << " threshold=" << snapshot.bloomThreshold
                << " intensity=" << snapshot.bloomIntensity
                << " radius=" << snapshot.bloomRadius;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Post]"
                << " toneMapping=" << BoolText(snapshot.toneMappingEnabled)
                << " aaMode=" << snapshot.antiAliasingMode
                << " fxaa=" << BoolText(snapshot.fxaaEnabled)
                << " fxaaEdge=" << snapshot.fxaaEdgeThreshold
                << " fxaaEdgeMin=" << snapshot.fxaaEdgeThresholdMin
                << " fxaaSubpixel=" << snapshot.fxaaSubpixelQuality;
            LogInfoLine(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "[EnvironmentDiagnostics][Errors]"
                << " recentRenderErrors=" << snapshot.recentRenderErrorCount;
            LogInfoLine(oss.str());
        }

        if (!snapshot.skyAssetFound && !snapshot.activeSkyAsset.empty()) {
            LogWarnLine("[EnvironmentDiagnostics][Sky] sky asset missing: " + snapshot.activeSkyAsset);
        }
        if (snapshot.skyUsingFallback) {
            LogWarnLine("[EnvironmentDiagnostics][Sky] sky renderer is using fallback.");
        }
        if (!snapshot.iblValid) {
            LogWarnLine("[EnvironmentDiagnostics][IBL] IBL is invalid or incomplete.");
        }
        if (snapshot.prefilteredMipMismatch) {
            LogWarnLine("[EnvironmentDiagnostics][IBL] prefiltered mip count mismatch.");
        }
        if (snapshot.reflectionProbeEnabled && !snapshot.reflectionProbeValid) {
            LogWarnLine("[EnvironmentDiagnostics][ReflectionProbe] reflection probe is enabled but invalid.");
        }
        if (!snapshot.reflectionProbeInfluenceBoxValid) {
            LogWarnLine("[EnvironmentDiagnostics][ReflectionProbe] influence box size is invalid.");
        }
        if (!snapshot.reflectionProbeProjectionBoxValid) {
            LogWarnLine("[EnvironmentDiagnostics][ReflectionProbe] projection box size is invalid.");
        }
        if (snapshot.reflectionProbeMipMismatch) {
            LogWarnLine("[EnvironmentDiagnostics][ReflectionProbe] prefiltered mip count mismatch.");
        }
        if (snapshot.ssaoEnabled && !snapshot.ssaoSuppressed && !snapshot.ssaoValid) {
            LogWarnLine("[EnvironmentDiagnostics][SSAO] SSAO is enabled but the runtime texture is invalid.");
        }
        if (snapshot.bloomFailed) {
            LogWarnLine("[EnvironmentDiagnostics][Bloom] bloom is in failed state.");
        }
        if (snapshot.recentRenderErrorCount > 0) {
            LogWarnLine("[EnvironmentDiagnostics][Errors] recent render errors exist.");
        }
    }

    void LogEnvironmentSnapshotIfChanged(const char* reason, const SceneEnvironment* environment) {
        const EnvironmentDiagnosticsSnapshot snapshot = CaptureEnvironmentSnapshot(environment);
        const EnvironmentDiagnosticsChangeKey key = MakeChangeKey(snapshot);
        if (gHasLastEnvironmentKey && key == gLastEnvironmentKey) {
            return;
        }

        // counter 系は変化判定に含めず、resource 状態だけを見る。
        gLastEnvironmentKey = key;
        gHasLastEnvironmentKey = true;
        LogEnvironmentSnapshot((reason && reason[0] != '\0') ? reason : "EnvironmentChanged", environment);
    }

    void ResetEnvironmentDiagnosticsChangeCache() {
        gLastEnvironmentKey = {};
        gHasLastEnvironmentKey = false;
    }

    const char* ResolveSkySummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot) {
        if (!snapshot.skyRendererInitialized) {
            return "Sky Missing";
        }
        if (!snapshot.skyAssetFound && !snapshot.activeSkyAsset.empty()) {
            return "Sky Missing";
        }
        if (snapshot.skyUsingFallback) {
            return "Sky Fallback";
        }
        if (snapshot.skyCubemapLoaded || snapshot.skyTextureValid) {
            return "Sky Ready";
        }
        return "Sky Pending";
    }

    const char* ResolveIblSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot) {
        if (snapshot.prefilteredMipMismatch) {
            return "IBL Mip Mismatch";
        }
        if (snapshot.iblValid && snapshot.iblHasPrefiltered && snapshot.iblHasBrdfLut) {
            return "IBL Ready";
        }
        if (snapshot.iblValid || snapshot.iblHasIrradiance || snapshot.iblHasPrefiltered || snapshot.iblHasBrdfLut) {
            return "IBL Partial";
        }
        return "IBL Missing";
    }

    const char* ResolveReflectionProbeSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot) {
        if (!snapshot.reflectionProbeEnabled) {
            return "Probe Off";
        }
        if (snapshot.reflectionProbeMipMismatch) {
            return "Probe Mip Mismatch";
        }
        if (!snapshot.reflectionProbeInfluenceBoxValid || !snapshot.reflectionProbeProjectionBoxValid) {
            return "Probe Box Invalid";
        }
        if (snapshot.reflectionProbeValid) {
            return "Probe Ready";
        }
        if (snapshot.reflectionProbeHasPrefiltered || snapshot.reflectionProbeHasBrdfLut) {
            return "Probe Partial";
        }
        return "Probe Missing";
    }

    const char* ResolveSsaoSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot) {
        if (!snapshot.ssaoEnabled) {
            return "AO Off";
        }
        if (snapshot.ssaoSuppressed) {
            return "AO Suppressed";
        }
        if (snapshot.ssaoValid) {
            return snapshot.ssaoHalfResolution ? "AO Half Ready" : "AO Ready";
        }
        return "AO Missing";
    }

} // namespace HIKARI::RENDER3D::DIAGNOSTICS
