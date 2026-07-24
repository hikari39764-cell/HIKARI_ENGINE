#pragma once

#include <cstdint>
#include <cstddef>
#include <dxgiformat.h>
#include <string>

namespace HIKARI {

    struct SceneEnvironment;

    namespace RENDER3D::DIAGNOSTICS {

        struct EnvironmentDiagnosticsSnapshot {
            bool skyRendererInitialized = false;
            bool skyLastRenderSubmitted = false;
            bool skyAssetFound = false;
            bool skyCubemapLoaded = false;
            bool skyUsingFallback = false;
            bool skyTextureValid = false;
            int skyCubemapHandle = -1;
            int skyTextureHandle = -1;
            size_t skyDrawCount = 0;
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

            size_t debugSubmittedLineCount = 0;
            size_t debugExpandedLineCount = 0;
            size_t debugDepthTestLineCount = 0;
            size_t debugXRayLineCount = 0;
            uint32_t debugLightProbeGizmoTotalPointCount = 0;
            uint32_t debugLightProbeGizmoDrawnPointCount = 0;
            uint32_t debugLightProbeGizmoMode = 0;
            bool debugLightProbeGizmoCapped = false;

            bool shadowEnabled = false;
            uint32_t shadowResolution = 0;
            size_t shadowSubmittedCasterCount = 0;
            size_t shadowStaticCasterDrawCount = 0;
            size_t shadowSkinnedCasterDrawCount = 0;
            size_t shadowAlphaMaskCasterDrawCount = 0;
            float shadowStrength = 0.0f;
            float shadowDepthBias = 0.0f;
            float shadowNormalBias = 0.0f;

            bool bloomEnabled = false;
            bool bloomInitialized = false;
            bool bloomFailed = false;
            uint32_t bloomPassCount = 0;
            int bloomTextureWidth = 0;
            int bloomTextureHeight = 0;
            float bloomThreshold = 0.0f;
            float bloomIntensity = 0.0f;
            float bloomRadius = 0.0f;

            bool toneMappingEnabled = false;
            std::string antiAliasingMode{};
            bool fxaaEnabled = false;
            float fxaaEdgeThreshold = 0.0f;
            float fxaaEdgeThresholdMin = 0.0f;
            float fxaaSubpixelQuality = 0.0f;

            uint32_t recentRenderErrorCount = 0;
        };

        EnvironmentDiagnosticsSnapshot CaptureEnvironmentSnapshot(const SceneEnvironment* environment = nullptr);

        void LogEnvironmentSnapshot(const char* reason, const SceneEnvironment* environment = nullptr);
        void LogEnvironmentSnapshotIfChanged(const char* reason, const SceneEnvironment* environment = nullptr);
        void ResetEnvironmentDiagnosticsChangeCache();

        const char* ResolveSkySummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot);
        const char* ResolveIblSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot);
        const char* ResolveReflectionProbeSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot);
        const char* ResolveSsaoSummaryLabel(const EnvironmentDiagnosticsSnapshot& snapshot);

    } // namespace RENDER3D::DIAGNOSTICS

} // namespace HIKARI
