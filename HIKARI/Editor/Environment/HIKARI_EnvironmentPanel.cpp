#include "Editor/Environment/HIKARI_EnvironmentPanel.h"

#include <optional>
#include <string>
#include <utility>

#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Documents/HIKARI_ValidatedAssetDocumentUi.h"
#include "Editor/Environment/HIKARI_RenderFeatureStatus.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
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
    // 美術布景（太陽の向きが固定）では時刻が光を変えないので、試し見の時刻は出さない。
    if (runtime.GetProfile().sun.mode != SunMode::Fixed) {
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
            // 試し見は選んだ天気へすぐ切り替える（天気資産の遷移は数十秒あり、選んでも変わらないように見える）。
            if (!runtime.RequestWeather(weather, 0.0f, error)) {
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

void EnvironmentPanel::DrawLightingMode(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene,
                                        EnvironmentPanelResult& result) {
    const bool fixedSun = scene.GetEnvironmentRuntime().GetProfile().sun.mode == SunMode::Fixed;
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    std::optional<SunMode> requested{};
    if (EDITOR::ToggleButton(TEXT::Localize("ui.environment.mode.artDirected").c_str(), "##artDirected", fixedSun,
                             ImVec2(half, 0.0f), TEXT::Localize("ui.environment.mode.artDirectedHint").c_str()) &&
        !fixedSun) {
        requested = SunMode::Fixed;
    }
    ImGui::SameLine();
    if (EDITOR::ToggleButton(TEXT::Localize("ui.environment.mode.dayCycle").c_str(), "##dayCycle", !fixedSun,
                             ImVec2(half, 0.0f), TEXT::Localize("ui.environment.mode.dayCycleHint").c_str()) &&
        fixedSun) {
        requested = SunMode::Celestial;
    }
    if (requested) {
        if (const std::string copied = assets_.SetSunMode(binding.profileId, *requested, scene); !copied.empty()) {
            binding.profileId = copied;
            result.bindingChanged = true;
        }
    }
    if (fixedSun) {
        return;
    }
    // 昼夜：場面の開始時刻（保存される）を時刻で動かす。日付と地点は高级。
    {
        EDITOR::InspectorPropertyRow row(TEXT::Localize("ui.environment.timeOfDay"));
        float hours = static_cast<float>(binding.time.localHours);
        if (row.IsVisible() && ImGui::SliderFloat("##timeOfDay", &hours, 0.0f, 23.99f, "%.2f h")) {
            binding.time.localHours = hours;
            result.bindingChanged = true;
        }
    }
    if (EDITOR::BeginAdvancedSection("dayCycleAdvanced")) {
        const EDITOR::FieldEditResult time =
            EDITOR::DrawJsonFields("ui.environment.time", binding.time, ENVIRONMENT::kTimeFields);
        ClampCalendarDay(binding.time);
        const EDITOR::FieldEditResult location =
            EDITOR::DrawJsonFields("ui.environment.location", binding.location, ENVIRONMENT::kLocationFields);
        result.bindingChanged |= time.changed || location.changed;
        EDITOR::EndAdvancedSection();
    }
}

void EnvironmentPanel::DrawLightingControls(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene,
                                            EnvironmentPanelResult& result) {
    DrawLightingMode(binding, scene, result);
    if (const std::string copied = assets_.DrawLightingControls(binding.profileId, scene); !copied.empty()) {
        binding.profileId = copied;
        result.bindingChanged = true;
    }
}

void EnvironmentPanel::DrawPresets(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene,
                                   EnvironmentPanelResult& result) {
    ImGui::SeparatorText(TEXT::Localize("ui.lookDev.presets").c_str());
    // 预设はプロファイルとその既定の天気の組。選ぶ時は場面の天気の固定を外し、预设の天気（阴天の雲など）を出す。
    const std::string before = binding.profileId;
    if (EDITOR::DrawEnvironmentProfilePresets(scene.GetAssetDatabase(), binding.profileId)) {
        compareProfileId_ = before;
        binding.weatherId.clear();
        result.bindingChanged = true;
    }
    // A/B：直前のプロファイルと入れ替えて見比べる。
    ImGui::BeginDisabled(compareProfileId_.empty() || compareProfileId_ == binding.profileId);
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.compareAB").c_str(), "##compareAB",
                             EDITOR::EditorButtonTone::Neutral, ImVec2(0.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.compareABHint").c_str())) {
        std::swap(binding.profileId, compareProfileId_);
        binding.weatherId.clear();
        result.bindingChanged = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // 存为预设：今の画をプロジェクトのプロファイルとして残し、以後の編集はその複製へ。
    if (EDITOR::ActionButton(TEXT::Localize("ui.lookDev.savePreset").c_str(), "##savePreset",
                             EDITOR::EditorButtonTone::Neutral, ImVec2(0.0f, 0.0f),
                             TEXT::Localize("ui.lookDev.savePresetHint").c_str())) {
        std::string error;
        if (const auto current = scene.GetEnvironmentAssets().LoadProfile(binding.profileId, error)) {
            ENVIRONMENT::EnvironmentProfileAsset copy = *current;
            copy.name += TEXT::Localize("ui.lookDev.presetCopySuffix");
            std::string message;
            if (assets_.Profile().SaveAsNew(std::move(copy), scene.GetAssetDatabase(), message)) {
                compareProfileId_ = binding.profileId;
                binding.profileId = assets_.Profile().Guid().value;
                result.bindingChanged = true;
            } else {
                previewError_ = message;
            }
        } else {
            previewError_ = error;
        }
    }
    // 場面が天気を固定している時は预设の天気が出ないので、そう示して外せるようにする。
    if (!binding.weatherId.empty()) {
        const std::string weather =
            EDITOR::ResolveEnvironmentAssetLabel(AssetType::Weather, binding.weatherId, scene.GetAssetDatabase());
        ImGui::TextColored(EDITOR::kAssetDocumentWarningColor, TEXT::Localize("ui.lookDev.weatherPinnedFormat").c_str(),
                           weather.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT::Localize("ui.lookDev.followPresetWeather").c_str())) {
            binding.weatherId.clear();
            result.bindingChanged = true;
        }
    }
}

void EnvironmentPanel::FinishWindow(DocumentSceneBase& scene, EnvironmentPanelResult& result, bool bindingActive) {
    assets_.PublishDrafts(scene);
    style_.PublishDrafts(scene);
    focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    result.editActive = bindingActive || (focused_ && ImGui::IsAnyItemActive());
    TrackEditedDocuments();
    if (result.bindingChanged) {
        lastEdited_ = nullptr;
    }
}

EnvironmentPanelResult EnvironmentPanel::DrawLookDev(ENVIRONMENT::SceneEnvironmentBinding& binding,
                                                     DocumentSceneBase& scene) {
    scene.SetEnvironmentPreviewTimeScale(0.0);
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.environment.lightingControls", "LookDev/Lighting").c_str())) {
        ImGui::End();
        assets_.PublishDrafts(scene);
        style_.PublishDrafts(scene);
        focused_ = false;
        return {};
    }
    EnvironmentPanelResult result{};
    // 効いていない機能があれば、窓の上で理由が分かるようにする。
    if (ImGui::TreeNodeEx(TEXT::Localize("ui.renderStatus.title").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        EDITOR::DrawRenderFeatureStatus(scene.GetSceneEnvironment());
        ImGui::TreePop();
    }
    DrawPresets(binding, scene, result);
    ImGui::SeparatorText(TEXT::Localize("ui.environment.mode").c_str());
    DrawLightingControls(binding, scene, result);
    if (!previewError_.empty()) {
        ImGui::TextColored(kErrorColor, "%s", previewError_.c_str());
    }
    FinishWindow(scene, result, false);
    ImGui::End();
    return result;
}

EnvironmentPanelResult EnvironmentPanel::Draw(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene) {
    // 編集中の環境はシーンが毎フレーム進める。パネルは試し再生の時刻の速さだけを渡す。
    scene.SetEnvironmentPreviewTimeScale(previewTimeFlow_ ? previewHoursPerSecond_ * 3600.0 : 0.0);
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.environment", "Environment").c_str())) {
        ImGui::End();
        assets_.PublishDrafts(scene);
        style_.PublishDrafts(scene);
        focused_ = false;
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

    // 布景でよく触る光と大気の項目を最初に置く（全項目はプロファイル資産の節）。
    if (ImGui::CollapsingHeader(TEXT::Localize("ui.environment.lightingControls").c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawLightingControls(binding, scene, result);
    }

    ImGui::SeparatorText(TEXT::Localize("ui.environment.sceneEnvironment").c_str());
    const EDITOR::FieldEditResult bindingResult = DrawBinding(binding, scene.GetAssetDatabase());
    result.bindingChanged |= bindingResult.changed;

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

    FinishWindow(scene, result, bindingResult.active);
    ImGui::End();
    return result;
}
#else
EnvironmentPanelResult EnvironmentPanel::Draw(ENVIRONMENT::SceneEnvironmentBinding&, DocumentSceneBase&) { return {}; }
EnvironmentPanelResult EnvironmentPanel::DrawLookDev(ENVIRONMENT::SceneEnvironmentBinding&, DocumentSceneBase&) {
    return {};
}
void EnvironmentPanel::DrawPreview(const ENVIRONMENT::SceneEnvironmentBinding&, DocumentSceneBase&) {}
#endif

std::array<EDITOR::EditorAssetDocument*, 4> EnvironmentPanel::Documents() {
    return {&assets_.Profile(), &assets_.Weather(), &style_.Document(), &style_.GlobalPost().Document()};
}

bool EnvironmentPanel::HasUnsavedDocuments() const {
    return assets_.Profile().IsDirty() || assets_.Weather().IsDirty() || style_.Document().IsDirty() ||
           style_.GlobalPost().Document().IsDirty();
}

// 資産文書の版が変わったら、それを最後に編集した文書として覚える（元に戻すの振り分け先）。
void EnvironmentPanel::TrackEditedDocuments() {
    const std::array<uint64_t, 4> revisions{assets_.Profile().Revision(), assets_.Weather().Revision(),
                                            style_.Document().Revision(), style_.GlobalPost().Document().Revision()};
    const std::array<EDITOR::EditorAssetDocument*, 4> documents = Documents();
    for (size_t index = 0; index < revisions.size(); ++index) {
        if (revisions[index] != seenRevisions_[index]) {
            seenRevisions_[index] = revisions[index];
            lastEdited_ = documents[index];
        }
    }
}

} // namespace HIKARI
