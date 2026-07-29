#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

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

    bool DocumentSceneBase::ApplyDocumentEnvironmentRuntimeChanges()
    {
        const bool skyResourceBindingChanged =
            IsSkyRuntimeResourceBindingChanged(
                state_->lighting.environment.sky,
                state_->identity.document.environment.sky);
        const bool reflectionProbeBindingChanged =
            IsReflectionProbeRuntimeBindingChanged(
                state_->lighting.environment.reflectionProbe,
                state_->identity.document.environment.reflectionProbe);

        state_->lighting.environment =
            state_->identity.document.environment;
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
        deps.lightingSceneHash =
            ASSETS::LIGHTING::BuildLightingSceneHash(
                state_->identity.document);
        deps.lightingBakeAuthoringHashes =
            ASSETS::LIGHTING::
                BuildLightingBakeRecordAuthoringHashes(
                    state_->identity.document);
        deps.lighting = LIGHTING_AUTHORING::BuildSceneLightingSet(
            state_->identity.document);

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
