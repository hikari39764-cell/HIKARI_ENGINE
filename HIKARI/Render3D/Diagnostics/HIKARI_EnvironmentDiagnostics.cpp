#include "HIKARI_EnvironmentDiagnostics.h"

#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_BoolText.h"
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
bool gHasLastEnvironmentKey = false;
EnvironmentDiagnosticsSnapshot gLastEnvironmentKey{};

const char* BoolText(bool value) { return TEXT::BoolText(value); }

const char* ReflectionProbeInfluenceShapeText(REFLECTION::RuntimeReflectionProbeInfluenceShape shape) {
    return shape == REFLECTION::RuntimeReflectionProbeInfluenceShape::Box ? "Box" : "Sphere";
}

const char* ReflectionProbeProjectionShapeText(REFLECTION::RuntimeReflectionProbeProjectionShape shape) {
    return shape == REFLECTION::RuntimeReflectionProbeProjectionShape::Box ? "Box" : "Infinite";
}

bool HasUsableBoxSize(float x, float y, float z) { return x > 0.001f && y > 0.001f && z > 0.001f; }

const char* ReasonText(const char* reason) { return (reason && reason[0] != '\0') ? reason : "Manual"; }

const char* LightProbeGizmoModeText(uint32_t mode) {
    switch (mode) {
    case 1u: return "BoundsOnly";
    case 2u: return "SampledPoints";
    case 3u: return "AllPoints";
    case 0u:
    default: return "Off";
    }
}

void LogInfoLine(const std::string& message) { HIKARI_LOG_INFO(message); }

void LogWarnLine(const std::string& message) { HIKARI_LOG_WARN(message); }

// 変化検出にはリソース状態だけを使い、フレームごとの統計値と調整値は除く。
EnvironmentDiagnosticsSnapshot MakeChangeKey(EnvironmentDiagnosticsSnapshot key) {
    const EnvironmentDiagnosticsSnapshot counters{};
    key.skyLastRenderSubmitted = counters.skyLastRenderSubmitted;
    key.skyDrawCount = counters.skyDrawCount;
    key.debugSubmittedStrokeCount = counters.debugSubmittedStrokeCount;
    key.debugExpandedStrokeCount = counters.debugExpandedStrokeCount;
    key.debugDepthTestStrokeCount = counters.debugDepthTestStrokeCount;
    key.debugXRayStrokeCount = counters.debugXRayStrokeCount;
    key.debugLightProbeGizmoTotalPointCount = counters.debugLightProbeGizmoTotalPointCount;
    key.debugLightProbeGizmoDrawnPointCount = counters.debugLightProbeGizmoDrawnPointCount;
    key.debugLightProbeGizmoMode = counters.debugLightProbeGizmoMode;
    key.debugLightProbeGizmoCapped = counters.debugLightProbeGizmoCapped;
    key.shadowSubmittedCasterCount = counters.shadowSubmittedCasterCount;
    key.shadowStaticCasterDrawCount = counters.shadowStaticCasterDrawCount;
    key.shadowSkinnedCasterDrawCount = counters.shadowSkinnedCasterDrawCount;
    key.shadowAlphaMaskCasterDrawCount = counters.shadowAlphaMaskCasterDrawCount;
    key.shadowStrength = counters.shadowStrength;
    key.shadowDepthBias = counters.shadowDepthBias;
    key.shadowNormalBias = counters.shadowNormalBias;
    key.bloomPassCount = counters.bloomPassCount;
    key.bloomTextureWidth = counters.bloomTextureWidth;
    key.bloomTextureHeight = counters.bloomTextureHeight;
    key.bloomThreshold = counters.bloomThreshold;
    key.bloomIntensity = counters.bloomIntensity;
    key.bloomRadius = counters.bloomRadius;
    key.fxaaEdgeThreshold = counters.fxaaEdgeThreshold;
    key.fxaaEdgeThresholdMin = counters.fxaaEdgeThresholdMin;
    key.fxaaSubpixelQuality = counters.fxaaSubpixelQuality;
    key.recentRenderErrorCount = key.recentRenderErrorCount > 0 ? 1u : 0u;
    return key;
}
} // namespace

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

    const auto& reflectionProbeSet = REFLECTION::GetProbeSet();
    const REFLECTION::ReflectionProbeRuntimeData* selectedProbe = REFLECTION::GetViewProbe(0u);
    if (selectedProbe == nullptr) {
        const auto firstValid =
            std::find_if(reflectionProbeSet.begin(), reflectionProbeSet.end(),
                         [](const REFLECTION::ReflectionProbeRuntimeData& candidate) { return candidate.valid; });
        selectedProbe = firstValid == reflectionProbeSet.end() ? nullptr : &*firstValid;
    }
    const REFLECTION::ReflectionProbeRuntimeData probe =
        selectedProbe != nullptr ? *selectedProbe : REFLECTION::ReflectionProbeRuntimeData{};
    snapshot.reflectionProbeResidentCount = static_cast<uint32_t>(reflectionProbeSet.size());
    snapshot.reflectionProbeSelectedCount = static_cast<uint32_t>(REFLECTION::GetViewSelection().count);
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
    snapshot.lightingBakeManifestStale = lighting.bakeManifestStale;
    snapshot.lightingBakeManifestStaleReason = lighting.bakeManifestStaleReason;
    snapshot.lightingBakeManifestPath = lighting.bakeManifestPath;
    snapshot.bakedReflectionProbeCount = lighting.bakedReflectionProbeCount;
    snapshot.bakedLightProbeCount = lighting.bakedLightProbeCount;
    snapshot.bakedLightmapCount = lighting.bakedLightmapCount;
    snapshot.lightingRuntimeSource = LIGHTING::ToString(lighting.source);
    const LIGHTPROBE::LightProbeVolumeDebugState lightProbe = LIGHTPROBE::GetDebugState();
    snapshot.lightProbeVolumeValid = lightProbe.valid;
    snapshot.lightProbeVolumeSrvReady = lightProbe.srvReady;
    snapshot.lightProbeVolumeHasGpuBuffer = lightProbe.hasBuffer;
    snapshot.lightProbeVolumeResidentCount = lightProbe.residentCount;
    snapshot.lightProbeVolumeSelectedCount = lightProbe.selectedCount;
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
    snapshot.debugSubmittedStrokeCount = debugStats.submittedStrokeCount;
    snapshot.debugExpandedStrokeCount = debugStats.expandedStrokeCount;
    snapshot.debugDepthTestStrokeCount = debugStats.depthTestStrokeCount;
    snapshot.debugXRayStrokeCount = debugStats.xrayStrokeCount;
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
        snapshot.toneCurve = static_cast<int>(environment->toneMapping.curve);
    }
    const RenderQualitySettings& renderQuality = GetRenderQualitySettings();
    snapshot.antiAliasingMode = RenderAntiAliasingModeLabel(renderQuality.antiAliasingMode);
    const POST::PostSystem::FxaaSettings& fxaa = POST::PostSystem::GetFxaaSettings();
    snapshot.fxaaEnabled = IsFxaaAntiAliasingMode(renderQuality.antiAliasingMode);
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
            << " fallback=" << BoolText(snapshot.skyUsingFallback) << " cubemapHandle=" << snapshot.skyCubemapHandle
            << " textureHandle=" << snapshot.skyTextureHandle << " draws=" << snapshot.skyDrawCount
            << " asset=" << snapshot.activeSkyAsset << " texture=" << snapshot.activeSkyTexturePath;
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][IBL]"
            << " valid=" << BoolText(snapshot.iblValid) << " irradiance=" << BoolText(snapshot.iblHasIrradiance)
            << " prefiltered=" << BoolText(snapshot.iblHasPrefiltered) << " brdf=" << BoolText(snapshot.iblHasBrdfLut)
            << " irradianceHandle=" << snapshot.irradianceHandle << " prefilteredHandle=" << snapshot.prefilteredHandle
            << " brdfHandle=" << snapshot.brdfLutHandle << " mips=" << snapshot.prefilteredMipCount
            << " actualMips=" << snapshot.irradianceMipCount << "/" << snapshot.prefilteredActualMipCount << "/"
            << snapshot.brdfLutMipCount << " formats=" << GFX::FormatToString(snapshot.irradianceFormat) << "/"
            << GFX::FormatToString(snapshot.prefilteredFormat) << "/" << GFX::FormatToString(snapshot.brdfLutFormat)
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
            << " brdfHandle=" << snapshot.reflectionProbeBrdfLutHandle << " mips=" << snapshot.reflectionProbeMipCount
            << " actualMips=" << snapshot.reflectionProbeActualMipCount << "/"
            << snapshot.reflectionProbeBrdfLutMipCount
            << " formats=" << GFX::FormatToString(snapshot.reflectionProbePrefilteredFormat) << "/"
            << GFX::FormatToString(snapshot.reflectionProbeBrdfLutFormat)
            << " mipMismatch=" << BoolText(snapshot.reflectionProbeMipMismatch)
            << " radius=" << snapshot.reflectionProbeRadius << " intensity=" << snapshot.reflectionProbeIntensity
            << " position=" << snapshot.reflectionProbePositionX << "," << snapshot.reflectionProbePositionY << ","
            << snapshot.reflectionProbePositionZ << " influenceShape=" << snapshot.reflectionProbeInfluenceShape
            << " projectionShape=" << snapshot.reflectionProbeProjectionShape
            << " influenceBoxValid=" << BoolText(snapshot.reflectionProbeInfluenceBoxValid)
            << " projectionBoxValid=" << BoolText(snapshot.reflectionProbeProjectionBoxValid)
            << " influenceBox=" << snapshot.reflectionProbeInfluenceBoxCenterX << ","
            << snapshot.reflectionProbeInfluenceBoxCenterY << "," << snapshot.reflectionProbeInfluenceBoxCenterZ << "/"
            << snapshot.reflectionProbeInfluenceBoxSizeX << "," << snapshot.reflectionProbeInfluenceBoxSizeY << ","
            << snapshot.reflectionProbeInfluenceBoxSizeZ
            << " projectionBox=" << snapshot.reflectionProbeProjectionBoxCenterX << ","
            << snapshot.reflectionProbeProjectionBoxCenterY << "," << snapshot.reflectionProbeProjectionBoxCenterZ
            << "/" << snapshot.reflectionProbeProjectionBoxSizeX << "," << snapshot.reflectionProbeProjectionBoxSizeY
            << "," << snapshot.reflectionProbeProjectionBoxSizeZ
            << " blendDistance=" << snapshot.reflectionProbeBlendDistance
            << " priority=" << snapshot.reflectionProbePriority << " resident=" << snapshot.reflectionProbeResidentCount
            << " selected=" << snapshot.reflectionProbeSelectedCount
            << " source=" << snapshot.reflectionProbeSourceAssetId
            << " prefilteredPath=" << snapshot.reflectionProbePrefilteredPath;
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][LightingRuntime]"
            << " source=" << snapshot.lightingRuntimeSource
            << " manifestFound=" << BoolText(snapshot.lightingBakeManifestFound)
            << " manifestStale=" << BoolText(snapshot.lightingBakeManifestStale)
            << " staleReason=" << snapshot.lightingBakeManifestStaleReason
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
            << " resident=" << snapshot.lightProbeVolumeResidentCount
            << " selected=" << snapshot.lightProbeVolumeSelectedCount
            << " probes=" << snapshot.lightProbeVolumeProbeCount << " grid=" << snapshot.lightProbeVolumeCountX << "x"
            << snapshot.lightProbeVolumeCountY << "x" << snapshot.lightProbeVolumeCountZ << " srvHeap=0x" << std::hex
            << snapshot.lightProbeVolumeSrvHeapPtr << " buffer=0x" << snapshot.lightProbeVolumeBufferPtr << std::dec
            << " path=" << snapshot.lightProbeVolumePath;
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][SSAO]"
            << " enabled=" << BoolText(snapshot.ssaoEnabled) << " valid=" << BoolText(snapshot.ssaoValid)
            << " suppressed=" << BoolText(snapshot.ssaoSuppressed) << " mode=" << snapshot.ssaoMode
            << " screen=" << snapshot.ssaoWidth << "x" << snapshot.ssaoHeight << " work=" << snapshot.ssaoInternalWidth
            << "x" << snapshot.ssaoInternalHeight << " half=" << BoolText(snapshot.ssaoHalfResolution)
            << " samples=" << snapshot.ssaoSampleCount << " blur=" << snapshot.ssaoBlurIterations
            << " radius=" << snapshot.ssaoRadius << " strength=" << snapshot.ssaoStrength
            << " power=" << snapshot.ssaoPower;
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][DebugOverlay]"
            << " submittedStrokes=" << snapshot.debugSubmittedStrokeCount
            << " expandedStrokes=" << snapshot.debugExpandedStrokeCount
            << " depthStrokes=" << snapshot.debugDepthTestStrokeCount
            << " xrayStrokes=" << snapshot.debugXRayStrokeCount
            << " lightProbePoints=" << snapshot.debugLightProbeGizmoDrawnPointCount << "/"
            << snapshot.debugLightProbeGizmoTotalPointCount
            << " lightProbeMode=" << LightProbeGizmoModeText(snapshot.debugLightProbeGizmoMode)
            << " lightProbeCapped=" << BoolText(snapshot.debugLightProbeGizmoCapped);
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][Shadow]"
            << " enabled=" << BoolText(snapshot.shadowEnabled) << " resolution=" << snapshot.shadowResolution
            << " casters=" << snapshot.shadowSubmittedCasterCount
            << " staticDraws=" << snapshot.shadowStaticCasterDrawCount
            << " skinnedDraws=" << snapshot.shadowSkinnedCasterDrawCount
            << " alphaMaskDraws=" << snapshot.shadowAlphaMaskCasterDrawCount << " strength=" << snapshot.shadowStrength
            << " depthBias=" << snapshot.shadowDepthBias << " normalBias=" << snapshot.shadowNormalBias;
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][Bloom]"
            << " enabled=" << BoolText(snapshot.bloomEnabled) << " initialized=" << BoolText(snapshot.bloomInitialized)
            << " failed=" << BoolText(snapshot.bloomFailed) << " passes=" << snapshot.bloomPassCount
            << " size=" << snapshot.bloomTextureWidth << "x" << snapshot.bloomTextureHeight
            << " threshold=" << snapshot.bloomThreshold << " intensity=" << snapshot.bloomIntensity
            << " radius=" << snapshot.bloomRadius;
        LogInfoLine(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "[EnvironmentDiagnostics][Post]"
            << " toneCurve=" << snapshot.toneCurve << " aaMode=" << snapshot.antiAliasingMode
            << " fxaa=" << BoolText(snapshot.fxaaEnabled) << " fxaaEdge=" << snapshot.fxaaEdgeThreshold
            << " fxaaEdgeMin=" << snapshot.fxaaEdgeThresholdMin << " fxaaSubpixel=" << snapshot.fxaaSubpixelQuality;
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
    const EnvironmentDiagnosticsSnapshot key = MakeChangeKey(snapshot);
    if (gHasLastEnvironmentKey && key == gLastEnvironmentKey) {
        return;
    }

    // counter 系は変化判定に含めず、resource 状態だけを見る。
    gLastEnvironmentKey = key;
    gHasLastEnvironmentKey = true;
    LogEnvironmentSnapshot((reason && reason[0] != '\0') ? reason : "EnvironmentChanged", environment);
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

} // namespace HIKARI::RENDER3D::DIAGNOSTICS
