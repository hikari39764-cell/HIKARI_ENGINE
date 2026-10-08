#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

namespace HIKARI {
namespace {
// 空の資源（読み込む資産）が変わったか。時刻や天気だけの変化では資源を読み直さない。
bool IsSkyRuntimeResourceBindingChanged(const SkySettings& before, const SkySettings& after) {
    return before.source != after.source || before.asset != after.asset ||
           before.panoramaCloudAsset != after.panoramaCloudAsset;
}
} // namespace

ENVIRONMENT::EnvironmentRuntime& DocumentSceneBase::GetEnvironmentRuntime() noexcept {
    return state_->runtime.environment;
}

void DocumentSceneBase::SetEnvironmentPreviewTimeScale(double timeScale) noexcept {
    state_->runtime.environmentPreviewTimeScale = timeScale;
}

RENDERSTYLE::RenderStyleAssetStore& DocumentSceneBase::GetRenderStyleAssets() noexcept {
    return state_->assets.RenderStyleAssets();
}

ENVIRONMENT::EnvironmentAssetStore& DocumentSceneBase::GetEnvironmentAssets() noexcept {
    return state_->assets.EnvironmentAssets();
}

void DocumentSceneBase::ResetEnvironmentRuntime() {
    state_->assets.EnvironmentAssets().Configure(&state_->assets.Database());
    state_->runtime.environment.Configure(&state_->assets.EnvironmentAssets());
    state_->runtime.environment.Reset(state_->identity.document.environment);
    if (!state_->runtime.environment.GetError().empty()) {
        HIKARI_LOG_WARN("[Environment] " + state_->runtime.environment.GetError());
    }
    state_->lighting.environment = state_->runtime.environment.ResolveFrame();
}

SceneDependencySet DocumentSceneBase::CollectRuntimeDependencies() const {
    SceneDependencySet dependencies =
        state_->runtime.builder.CollectDependencies(state_->identity.document, &state_->runtime.componentRegistry);
    AddEnvironmentSkyDependencies(state_->runtime.environment.GetProfile().sky, dependencies);
    return dependencies;
}

bool DocumentSceneBase::ApplyDocumentEnvironmentRuntimeChanges() {
    const SkySettings previousSky = state_->runtime.environment.GetProfile().sky;
    ResetEnvironmentRuntime();
    state_->identity.documentDirty = true;
    if (!IsSkyRuntimeResourceBindingChanged(previousSky, state_->runtime.environment.GetProfile().sky)) {
        return true;
    }
    return RefreshLightingRuntime();
}

bool DocumentSceneBase::RefreshEnvironmentProfile() {
    const SkySettings previousSky = state_->runtime.environment.GetProfile().sky;
    state_->runtime.environment.RefreshProfile();
    if (!state_->runtime.environment.GetError().empty()) {
        HIKARI_LOG_WARN("[Environment] " + state_->runtime.environment.GetError());
    }
    if (!IsSkyRuntimeResourceBindingChanged(previousSky, state_->runtime.environment.GetProfile().sky)) {
        return true;
    }
    return RefreshLightingRuntime();
}

bool DocumentSceneBase::RefreshLightingRuntime() {
    SceneDependencySet deps{};
    AddEnvironmentSkyDependencies(state_->runtime.environment.GetProfile().sky, deps);
    deps.lightingSceneHash = ASSETS::LIGHTING::BuildLightingSceneHash(state_->identity.document);
    deps.lightingBakeAuthoringHashes =
        ASSETS::LIGHTING::BuildLightingBakeRecordAuthoringHashes(state_->identity.document);
    deps.lighting = LIGHTING_AUTHORING::BuildSceneLightingSet(state_->identity.document);

    const bool ok = state_->runtime.builder.PreloadDependencies(
        deps, state_->assets.Registry(), state_->assets.Models(), state_->lighting.sky,
        state_->assets.Database().GetProjectRoot(), state_->identity.currentSceneAssetGuid.value, true,
        &state_->assets.SpriteAssets());

    SKYRENDERER::InvalidateSkyTextureCache();
    RENDER3D::PANORAMA_SKY::InvalidatePanoramaSkyLayerTextureCache();

    if (!ok) {
        HIKARI_LOG_WARN("[LightingRuntime] failed to preload lighting dependency.");
    }

    return ok;
}

} // namespace HIKARI
