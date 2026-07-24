#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include "Core/HIKARI_Logger.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntimeConversion.h"

namespace HIKARI {
    namespace {
        bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool IsSkyRuntimeResourceBindingChanged(
             const SkySettings& before,
             const SkySettings& after)
        {
            return before.skyAsset != after.skyAsset ||
                   before.mode != after.mode;
        }

        bool IsReflectionProbeRuntimeBindingChanged(
            const ReflectionProbeSettings& before,
            const ReflectionProbeSettings& after)
        {
            return before.enabled != after.enabled ||
                before.sourceCubemapAsset != after.sourceCubemapAsset ||
                !EqualVec3(before.position, after.position) ||
                before.radius != after.radius ||
                before.intensity != after.intensity ||
                before.influenceShape != after.influenceShape ||
                before.projectionShape != after.projectionShape ||
                !EqualVec3(before.influenceBoxCenter, after.influenceBoxCenter) ||
                !EqualVec3(before.influenceBoxSize, after.influenceBoxSize) ||
                !EqualVec3(before.projectionBoxCenter, after.projectionBoxCenter) ||
                !EqualVec3(before.projectionBoxSize, after.projectionBoxSize) ||
                before.blendDistance != after.blendDistance ||
                before.priority != after.priority;
        }

    }

    void DocumentSceneBase::SyncReflectionProbeRuntimeFromAuthoring() {
        const ReflectionProbeSettings& probe = state_->lighting.environment.reflectionProbe;
        const REFLECTION::ReflectionProbeRuntimeData runtime = REFLECTION::GetActiveProbe();

        // Viewport 謫堺ｽ懊〒縺ｯ迴ｾ蝨ｨ縺ｮ texture resource 繧剃ｿ晄戟縺励∥uthoring 蛟､縺縺大酔譛溘☆繧九・
        REFLECTION::SetActiveProbeResources(
            probe.enabled,
            runtime.prefilteredResource,
            runtime.brdfLutResource,
            runtime.prefilteredMipCount,
            probe.position,
            probe.radius,
            probe.intensity,
            REFLECTION::ToRuntimeInfluenceShape(probe.influenceShape),
            REFLECTION::ToRuntimeProjectionShape(probe.projectionShape),
            probe.influenceBoxCenter,
            probe.influenceBoxSize,
            probe.projectionBoxCenter,
            probe.projectionBoxSize,
            probe.blendDistance,
            probe.priority,
            runtime.sourceAssetId,
            runtime.prefilteredPath,
            runtime.brdfLutPath);
    }
    bool DocumentSceneBase::ApplyEnvironmentRuntimeChanges()
    {
        const bool skyResourceBindingChanged =
            IsSkyRuntimeResourceBindingChanged(
                state_->identity.document.environment.sky,
                state_->lighting.environment.sky);
        const bool reflectionProbeBindingChanged =
            IsReflectionProbeRuntimeBindingChanged(
                state_->identity.document.environment.reflectionProbe,
                state_->lighting.environment.reflectionProbe);

        state_->identity.document.environment = state_->lighting.environment;
        state_->identity.documentDirty = true;

        if (!skyResourceBindingChanged && !reflectionProbeBindingChanged) {
            return true;
        }

        return RefreshLightingRuntime();
    }

    bool DocumentSceneBase::RefreshLightingRuntime() {
        SceneDependencySet deps{};
        if (!state_->lighting.environment.sky.skyAsset.empty()) {
            deps.skyAssetIds.insert(state_->lighting.environment.sky.skyAsset);
        }
        if (state_->lighting.environment.reflectionProbe.enabled &&
            !state_->lighting.environment.reflectionProbe.sourceCubemapAsset.empty()) {
            deps.reflectionProbeCubemapAssetIds.insert(state_->lighting.environment.reflectionProbe.sourceCubemapAsset);
            deps.reflectionProbeEnabled = true;
            deps.reflectionProbePosition = state_->lighting.environment.reflectionProbe.position;
            deps.reflectionProbeRadius = state_->lighting.environment.reflectionProbe.radius;
            deps.reflectionProbeIntensity = state_->lighting.environment.reflectionProbe.intensity;
            deps.reflectionProbeInfluenceShape = state_->lighting.environment.reflectionProbe.influenceShape;
            deps.reflectionProbeProjectionShape = state_->lighting.environment.reflectionProbe.projectionShape;
            deps.reflectionProbeInfluenceBoxCenter = state_->lighting.environment.reflectionProbe.influenceBoxCenter;
            deps.reflectionProbeInfluenceBoxSize = state_->lighting.environment.reflectionProbe.influenceBoxSize;
            deps.reflectionProbeProjectionBoxCenter = state_->lighting.environment.reflectionProbe.projectionBoxCenter;
            deps.reflectionProbeProjectionBoxSize = state_->lighting.environment.reflectionProbe.projectionBoxSize;
            deps.reflectionProbeBlendDistance = state_->lighting.environment.reflectionProbe.blendDistance;
            deps.reflectionProbePriority = state_->lighting.environment.reflectionProbe.priority;
        }
        LightProbeVolumeSettings lightProbe = state_->identity.document.lightingBake.lightProbeVolume;
        ClampLightProbeVolumeSettings(lightProbe);
        deps.lightProbeVolumeEnabled = lightProbe.enabled;
        deps.lightProbeVolumeIntensity = lightProbe.intensity;

        const bool ok = state_->runtime.builder.PreloadDependencies(
            deps,
            state_->assets.Registry(),
            state_->assets.Models(),
            state_->lighting.sky,
            state_->assets.Database().GetProjectRoot(),
            state_->identity.currentSceneAssetGuid.value);

        SKYRENDERER::InvalidateSkyTextureCache();

        if (!ok) {
            HIKARI_LOG_WARN("[LightingRuntime] failed to preload lighting dependency.");
        }

        return ok;
    }

    bool DocumentSceneBase::RefreshSkyRuntime() {
        return RefreshLightingRuntime();
    }

    bool DocumentSceneBase::RefreshCurrentSkyRuntime() {
        return RefreshLightingRuntime();
    }


} // namespace HIKARI
