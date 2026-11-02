#include "Editor/Environment/HIKARI_RenderFeatureStatus.h"

#include <cstdio>
#include <optional>
#include <string>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Render3D/Lighting/HIKARI_DynamicDiffuseGi.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "imgui.h"

namespace HIKARI::EDITOR {

namespace {
using RENDER3D::RAYTRACING::RuntimeRayTracingStats;
using RENDER3D::RAYTRACING::RuntimeRayTracingStatus;

struct FeatureState {
    std::string text;
    EditorStatusTone tone = EditorStatusTone::Normal;
    // 補足（抜けている面の種類）。状態の下に小さく出す。
    std::string detail{};
};

FeatureState Localized(const char* key, EditorStatusTone tone) { return {TEXT::Localize(key), tone}; }

// 加速構造の状態。Ready 以外は光追を使う機能すべてに共通の理由。
std::optional<FeatureState> RaySceneProblem(const RuntimeRayTracingStats& stats) {
    switch (stats.status) {
    case RuntimeRayTracingStatus::Ready: return std::nullopt;
    case RuntimeRayTracingStatus::Building:
    case RuntimeRayTracingStatus::Disabled: return Localized("ui.renderStatus.building", EditorStatusTone::Normal);
    case RuntimeRayTracingStatus::UnsupportedDevice:
        return Localized("ui.renderStatus.unsupportedDevice", EditorStatusTone::Warning);
    case RuntimeRayTracingStatus::BudgetExceeded:
        return Localized("ui.renderStatus.budgetExceeded", EditorStatusTone::Warning);
    case RuntimeRayTracingStatus::InvalidInput:
    case RuntimeRayTracingStatus::BuildFailed: break;
    }
    return Localized("ui.renderStatus.failed", EditorStatusTone::Error);
}

// mask に含まれる種類を「透明 2、角色 1」の形で並べる。
std::string ListExclusions(const std::array<uint32_t, RENDER3D::RAYTRACING::kRuntimeRayTracingExclusionCount>& counts,
                           uint32_t mask) {
    std::string text;
    for (const auto& [exclusion, name] : RENDER3D::RAYTRACING::kRuntimeRayTracingExclusionNames) {
        if ((mask & RENDER3D::RAYTRACING::RuntimeRayTracingExclusionBit(exclusion)) == 0u) {
            continue;
        }
        if (!text.empty()) {
            text += TEXT::Localize("ui.renderStatus.listSeparator");
        }
        text += TEXT::Localize("ui.renderStatus.exclusion." + std::string(name));
        text += ' ';
        text += std::to_string(counts[static_cast<size_t>(exclusion)]);
    }
    return text;
}

// 光追を使う機能の状態。使えない時は妨げている種類を、使える時は含まれていない種類を添える。
FeatureState RayFeature(const RuntimeRayTracingStats& stats, bool usable, uint32_t blocking) {
    if (auto problem = RaySceneProblem(stats)) {
        return *problem;
    }
    if (!usable) {
        return {TEXT::Localize("ui.renderStatus.missingSurfaces"), EditorStatusTone::Warning,
                ListExclusions(stats.excluded.surfaces, blocking)};
    }
    FeatureState state = Localized("ui.renderStatus.active", EditorStatusTone::Ready);
    if (stats.deformedObjects != 0u) {
        char buffer[128]{};
        std::snprintf(buffer, sizeof(buffer), TEXT::Localize("ui.renderStatus.deformedFormat").c_str(),
                      stats.deformedObjects);
        state.detail = buffer;
    }
    if (stats.excluded.mask != 0u) {
        state.detail += (state.detail.empty() ? "" : "  ") + TEXT::Localize("ui.renderStatus.withoutPrefix") +
                        ListExclusions(stats.excluded.surfaces, stats.excluded.mask);
    }
    if (stats.excluded.approximatedMask != 0u) {
        state.detail += (state.detail.empty() ? "" : "  ") + TEXT::Localize("ui.renderStatus.approximatedPrefix") +
                        ListExclusions(stats.excluded.approximated, stats.excluded.approximatedMask);
    }
    return state;
}

void Row(const char* labelKey, const FeatureState& state) {
    InspectorPropertyRow row(TEXT::Localize(labelKey));
    if (!row.IsVisible()) {
        return;
    }
    StatusText(state.text.c_str(), state.tone);
    if (!state.detail.empty()) {
        ImGui::TextDisabled("%s", state.detail.c_str());
    }
}
} // namespace

void DrawRenderFeatureStatus(const SceneEnvironment& environment) {
    using namespace RENDER3D::RAYTRACING;
    const RENDER3D::RenderQualitySettings& quality = RENDER3D::GetRenderQualitySettings();
    const RuntimeRayTracingStats& rayScene = RENDER3D::PIPELINE::GetRuntimeRayTracingStats();

    const bool gi = quality.dynamicGiQuality != RENDER3D::DynamicGiQuality::Off &&
                    quality.lightProbeVolumeSampling != RENDER3D::LightProbeVolumeSamplingMode::Off;
    FeatureState giState = Localized("ui.renderStatus.offInQuality", EditorStatusTone::Normal);
    if (gi) {
        giState = RayFeature(rayScene, CanTraceDiffuse(rayScene.excluded.mask),
                             rayScene.excluded.mask & kRuntimeRayTracingDiffuseBlockingExclusions);
        if (giState.tone == EditorStatusTone::Ready &&
            RENDER3D::PIPELINE::GetDynamicDiffuseGiStats().status != RENDER3D::DYNAMICGI::Status::Ready) {
            giState = Localized("ui.renderStatus.building", EditorStatusTone::Normal);
        }
    }
    Row("ui.renderStatus.gi", giState);

    // この場面が読み込んだ焼き付け結果。光照贴图のある静的な面は動的 GI の代わりにそれを使う。
    const RENDER3D::LIGHTING::SceneLightingRuntimeData& baked = RENDER3D::LIGHTING::GetLastLightingRuntimeData();
    FeatureState bakedState = Localized("ui.renderStatus.bakedNone", EditorStatusTone::Normal);
    if (baked.loadedLightmapCount + baked.loadedIrradianceVolumeCount + baked.loadedReflectionProbeCount != 0u) {
        char buffer[160]{};
        std::snprintf(buffer, sizeof(buffer), TEXT::Localize("ui.renderStatus.bakedFormat").c_str(),
                      baked.loadedLightmapCount, baked.loadedIrradianceVolumeCount, baked.loadedReflectionProbeCount);
        bakedState = {buffer, EditorStatusTone::Ready};
        if (baked.loadedLightmapCount != 0u && gi)
            bakedState.detail = TEXT::Localize("ui.renderStatus.bakedReplacesGi");
    }
    if (baked.bakeManifestStale) {
        bakedState.tone = EditorStatusTone::Warning;
        bakedState.detail = TEXT::Localize("ui.renderStatus.bakedStale");
    }
    Row("ui.renderStatus.baked", bakedState);

    // 光追の反射は画面配置の反射の節（強さ・粗さの上限）で動くので、その節が有効である必要がある。
    const ScreenSpaceReflectionSettings& ssr = environment.screenSpaceReflections;
    FeatureState reflections = Localized("ui.renderStatus.offInQuality", EditorStatusTone::Normal);
    if (quality.rayTracedReflections) {
        reflections = ssr.enabled && ssr.quality != ScreenSpaceReflectionQuality::Off
                          ? RayFeature(rayScene, true, 0u)
                          : Localized("ui.renderStatus.needsReflections", EditorStatusTone::Warning);
    }
    Row("ui.renderStatus.rayReflections", reflections);

    const bool rayShadows = quality.directionalShadow.rayTraced && environment.shadows.enabled;
    FeatureState rayShadowState = Localized("ui.renderStatus.offInQuality", EditorStatusTone::Normal);
    if (rayShadows) {
        const uint32_t casters = rayScene.excluded.shadowCasterMask;
        rayShadowState =
            RayFeature(rayScene, CanTraceShadows(casters), casters & ~kRuntimeRayTracingShadowToleratedExclusions);
    }
    Row("ui.renderStatus.rayShadows", rayShadowState);

    // 接触影は光追の影が効いている間はそれに置き換わる。
    FeatureState contact = Localized("ui.renderStatus.offInProfile", EditorStatusTone::Normal);
    if (environment.shadows.enabled && environment.shadows.contactShadows) {
        contact = rayShadowState.tone == EditorStatusTone::Ready
                      ? Localized("ui.renderStatus.replacedByRayShadows", EditorStatusTone::Normal)
                      : Localized("ui.renderStatus.active", EditorStatusTone::Ready);
    }
    Row("ui.renderStatus.contactShadows", contact);

    // 環境光遮蔽は画面配置の「遮蔽と反射」で切り替える。
    const AmbientOcclusionSettings& ao = environment.ambientOcclusion;
    Row("ui.renderStatus.ambientOcclusion", ao.enabled && ao.mode != SsaoMode::Off
                                                ? Localized("ui.renderStatus.active", EditorStatusTone::Ready)
                                                : Localized("ui.renderStatus.offInProfile", EditorStatusTone::Normal));
}

} // namespace HIKARI::EDITOR
