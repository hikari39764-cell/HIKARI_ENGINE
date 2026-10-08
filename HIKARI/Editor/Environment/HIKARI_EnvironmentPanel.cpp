#include "Editor/Environment/HIKARI_EnvironmentPanel.h"

#include <string>

#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#include "imgui.h"
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
namespace {
constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.45f, 1.0f};

// 月末を越えた日付（2 月 30 日など）は、その月の最終日へ寄せる。
void ClampCalendarDay(ENVIRONMENT::CalendarTime& time) {
    while (time.day > 28u && !ENVIRONMENT::IsValidCalendarTime(time)) {
        --time.day;
    }
}

EDITOR::FieldEditResult DrawBinding(ENVIRONMENT::SceneEnvironmentBinding& binding, const AssetDatabase& database) {
    EDITOR::FieldEditResult result{};
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.profile"));
        if (row.IsVisible()) {
            result.changed |= EDITOR::DrawEnvironmentAssetPicker(AssetType::Environment, database, binding.profileId);
        }
    }
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.weather"));
        if (row.IsVisible()) {
            result.changed |= EDITOR::DrawEnvironmentAssetPicker(AssetType::Weather, database, binding.weatherId,
                                                                 "ui.environment.profileDefaultWeather");
        }
    }
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.renderStyle"));
        if (row.IsVisible()) {
            result.changed |= EDITOR::DrawRenderStylePicker(database, binding.renderStyleId);
        }
    }
    const auto merge = [&result](const EDITOR::FieldEditResult& fields) {
        result.changed |= fields.changed;
        result.active |= fields.active;
    };
    merge(EDITOR::DrawJsonFields("ui.environment.binding", binding, ENVIRONMENT::kBindingFields,
                                 [](std::string_view key, auto&, EDITOR::FieldEditResult&) {
                                     return key == "profile" || key == "weather" || key == "renderStyle";
                                 }));
    if (ImGui::TreeNodeEx(TEXT::Localize("ui.environment.startTime").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        merge(EDITOR::DrawJsonFields("ui.environment.time", binding.time, ENVIRONMENT::kTimeFields));
        ClampCalendarDay(binding.time);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode(TEXT::Localize("ui.environment.location").c_str())) {
        merge(EDITOR::DrawJsonFields("ui.environment.location", binding.location, ENVIRONMENT::kLocationFields));
        ImGui::TreePop();
    }
    return result;
}
} // namespace

void EnvironmentPanel::DrawPreview(const ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene) {
    ENVIRONMENT::EnvironmentRuntime& runtime = scene.GetEnvironmentRuntime();
    ENVIRONMENT::EnvironmentSimulation& simulation = runtime.Simulation();
    const ENVIRONMENT::EnvironmentFrameState state = simulation.GetFrameState();
    ImGui::BeginDisabled(scene.IsRuntimePlayActive());
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.previewTime"));
        float hours = static_cast<float>(state.time.localHours);
        if (row.IsVisible() && ImGui::SliderFloat("##previewTime", &hours, 0.0f, 23.99f, "%.2f h")) {
            ENVIRONMENT::CalendarTime time = state.time;
            time.localHours = hours;
            simulation.SetTime(time);
        }
    }
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.previewTimeFlow"));
        if (row.IsVisible()) {
            ImGui::Checkbox("##previewTimeFlow", &previewTimeFlow_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##previewSpeed", &previewHoursPerSecond_, 0.01f, 0.0f, 24.0f, "%.2f h/s");
        }
    }
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.previewWeather"));
        std::string weather = runtime.GetWeatherId();
        if (row.IsVisible() &&
            EDITOR::DrawEnvironmentAssetPicker(AssetType::Weather, scene.GetAssetDatabase(), weather)) {
            std::string error;
            if (!runtime.RequestWeather(weather, -1.0f, error)) {
                previewError_ = error;
            }
        }
    }
    if (ImGui::Button(TEXT::Localize("ui.environment.previewReset").c_str())) {
        previewTimeFlow_ = false;
        previewError_.clear();
        scene.ResetEnvironmentRuntime();
    }
    ImGui::EndDisabled();
    const bool moonIsPrimary = scene.GetSceneEnvironment().frame.moonIsPrimary;
    ImGui::TextDisabled(
        TEXT::Localize("ui.environment.previewStatusFormat").c_str(), state.time.year, state.time.month, state.time.day,
        state.time.localHours, state.celestial.sunAltitudeDegrees,
        TEXT::Localize(moonIsPrimary ? "ui.environment.primaryMoon" : "ui.environment.primarySun").c_str(),
        state.weatherTransitionProgress * 100.0f, state.surface.wetness, state.surface.snowCover);
    if (binding.timeScale > 0.0) {
        ImGui::TextDisabled(TEXT::Localize("ui.environment.playTimeScaleFormat").c_str(), binding.timeScale);
    }
    if (!previewError_.empty()) {
        ImGui::TextColored(kErrorColor, "%s", previewError_.c_str());
    }
}

EnvironmentPanelResult EnvironmentPanel::Draw(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene) {
    // 編集中の環境はシーンが毎フレーム進める。パネルは試し再生の時刻の速さだけを渡す。
    scene.SetEnvironmentPreviewTimeScale(previewTimeFlow_ ? previewHoursPerSecond_ * 3600.0 : 0.0);
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.environment", "Environment").c_str())) {
        ImGui::End();
        assets_.PublishDrafts(scene);
        style_.PublishDrafts(scene);
        return {};
    }

    EnvironmentPanelResult result{};
    const ENVIRONMENT::EnvironmentRuntime& runtime = scene.GetEnvironmentRuntime();
    if (!runtime.GetError().empty()) {
        ImGui::TextColored(kErrorColor, "%s", runtime.GetError().c_str());
    }
    const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot snapshot =
        RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&scene.GetSceneEnvironment());
    ImGui::TextDisabled(TEXT::Localize("ui.environment.runtimeFormat").c_str(),
                        RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(snapshot),
                        RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(snapshot), snapshot.recentRenderErrorCount);

    ImGui::SeparatorText(TEXT::Localize("ui.environment.sceneEnvironment").c_str());
    const EDITOR::FieldEditResult bindingResult = DrawBinding(binding, scene.GetAssetDatabase());
    result.bindingChanged = bindingResult.changed;

    if (ImGui::CollapsingHeader(TEXT::Localize("ui.environment.preview").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawPreview(binding, scene);
    }
    if (ImGui::CollapsingHeader(TEXT::Localize("ui.environment.profileAsset").c_str())) {
        ImGui::PushID("profile");
        if (const std::string copied = assets_.DrawProfile(binding.profileId, scene); !copied.empty()) {
            binding.profileId = copied;
            result.bindingChanged = true;
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader(TEXT::Localize("ui.environment.weatherAsset").c_str())) {
        ImGui::PushID("weather");
        const std::string weatherId =
            binding.weatherId.empty() ? runtime.GetProfile().defaultWeather : binding.weatherId;
        if (const std::string copied = assets_.DrawWeather(weatherId, scene); !copied.empty()) {
            binding.weatherId = copied;
            result.bindingChanged = true;
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader(TEXT::Localize("ui.environment.renderStyleAsset").c_str())) {
        ImGui::PushID("renderStyle");
        if (const std::string copied = style_.DrawStyle(binding.renderStyleId, scene); !copied.empty()) {
            binding.renderStyleId = copied;
            result.bindingChanged = true;
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader(TEXT::Localize("ui.environment.lightingObjects").c_str())) {
        ImGui::TextWrapped("%s", TEXT::Localize("ui.environment.lightingObjectsHint").c_str());
        result.openLightingBakeRequested = ImGui::Button(TEXT::Localize("ui.environment.openLightingBakeTool").c_str());
        ImGui::TextDisabled(TEXT::Localize("ui.environment.primaryProbeFormat").c_str(),
                            RENDER3D::DIAGNOSTICS::ResolveReflectionProbeSummaryLabel(snapshot));
        if (ImGui::Button(TEXT::Localize("ui.environment.dumpEnvironmentDiagnostics").c_str())) {
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshot("EnvironmentPanel", &scene.GetSceneEnvironment());
        }
    }

    assets_.PublishDrafts(scene);
    style_.PublishDrafts(scene);
    result.editActive = bindingResult.active ||
                        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsAnyItemActive());
    ImGui::End();
    return result;
}
#else
EnvironmentPanelResult EnvironmentPanel::Draw(ENVIRONMENT::SceneEnvironmentBinding&, DocumentSceneBase&) { return {}; }
void EnvironmentPanel::DrawPreview(const ENVIRONMENT::SceneEnvironmentBinding&, DocumentSceneBase&) {}
#endif

} // namespace HIKARI
