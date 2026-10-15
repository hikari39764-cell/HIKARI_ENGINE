#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#include "imgui.h"
#include "Assets/Environment/HIKARI_EnvironmentFields.h"
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::PrepareBackgroundPreview(DocumentSceneBase& scene, EditorWorkspaceHost& views,
                                                                uint32_t width, uint32_t height, std::string& message) {
    (void)FlushMaterialPreview(scene, statusMessage_);
    if (!IsPreviewReady() && !RefreshMaterialPreviews(scene, message))
        return false;
    InitializePreviewLighting();
    auto& view = views.GetModelMaterialPreviewView();
    view.extent = {width, height};
    auto& camera = cameraController_.GetCamera();
    camera.SetPerspective(camera.GetFovYRad(), view.extent.Aspect(), camera.GetNearZ(), camera.GetFarZ());
    FitPreviewCamera();
    previewCompareEnabled_ = false;
    SubmitPreviewRequests(views, GetActivePreviewScene(), true);
    return IsPreviewReady();
}

namespace {

constexpr std::string_view kStudioProfileId = "engine.environment.studio";
// Studio の手前の点光源 [lm]。晴天相当の露出（EV100 14 前後）でも見える明るさ。
constexpr float kStudioKeyLumens = 200000.0f;

const char* EnvironmentModeLabel(ModelMaterialWorkspaceController::PreviewEnvironmentMode mode) noexcept {
    switch (mode) {
    case ModelMaterialWorkspaceController::PreviewEnvironmentMode::RuntimeMatch:
        return TEXT::Localize("editor.ui.runtime.asset.lighting.d00c8b").c_str();
    case ModelMaterialWorkspaceController::PreviewEnvironmentMode::SceneMatch:
        return TEXT::Localize("editor.ui.scene.match.61e8fc").c_str();
    case ModelMaterialWorkspaceController::PreviewEnvironmentMode::StudioInspect:
        return TEXT::Localize("editor.ui.studio.inspect.7ebcc0").c_str();
    }
    return TEXT::Localize("editor.ui.runtime.asset.lighting.d00c8b").c_str();
}

RENDER3D::EDITORVIEW::EditorInteractiveShadingMode ResolveShadingMode(
    ModelMaterialWorkspaceController::PreviewDebugMode mode) noexcept {
    using Mode = RENDER3D::EDITORVIEW::EditorInteractiveShadingMode;
    switch (mode) {
    case ModelMaterialWorkspaceController::PreviewDebugMode::Lit: return Mode::Lit;
    case ModelMaterialWorkspaceController::PreviewDebugMode::Neutral: return Mode::Neutral;
    case ModelMaterialWorkspaceController::PreviewDebugMode::Clay: return Mode::NeutralClay;
    case ModelMaterialWorkspaceController::PreviewDebugMode::Unlit: return Mode::Unlit;
    }
    return Mode::Lit;
}

} // namespace

void ModelMaterialWorkspaceController::SaveActiveCameraState() {
    if (authoringMode_ == AuthoringMode::ModelMaterials && modelGuid_.IsValid()) {
        (void)cameraStateStore_.Save(EditorWorkspaceId::ModelMaterial, modelGuid_, cameraController_);
    }
}

void ModelMaterialWorkspaceController::ClosePreviewRequest(EditorWorkspaceHost& workspaceHost) const {
    RENDER3D::EDITORVIEW::ClearRequest(workspaceHost.GetModelMaterialPreviewView().renderViewId);
    RENDER3D::EDITORVIEW::ClearRequest(workspaceHost.GetModelMaterialCompareView().renderViewId);
}

void ModelMaterialWorkspaceController::FitPreviewCamera() {
    const Bounds& bounds = GetActivePreviewScene().GetBounds();
    if (!BOUNDS::IsUsable(bounds)) {
        return;
    }
    const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
    const float radius = (std::max)(MATH::Length(bounds.max - bounds.min) * 0.5f, 0.5f);
    EditorDirectorCameraSettings& navigation = cameraController_.Settings();
    previewCameraBaseMoveSpeed_ = radius * 0.75f;
    navigation.moveSpeed = std::clamp(previewCameraBaseMoveSpeed_ * previewCameraSpeedScale_, radius * 0.05f, 1000.0f);
    navigation.fastMultiplier = 5.0f;
    previewCameraBaseWheelStep_ = radius * 0.12f;
    navigation.wheelMoveStep =
        std::clamp(previewCameraBaseWheelStep_ * previewCameraWheelStepScale_, radius * 0.01f, 200.0f);
    cameraController_.ApplyViewPreset(EditorDirectorCameraViewPreset::Perspective);
    cameraController_.Focus(center, radius * 2.4f);
    Camera3D& camera = cameraController_.GetCamera();
    const float fovYRad = previewCameraFovYRad_ > 0.01f ? previewCameraFovYRad_ : camera.GetFovYRad();
    cameraController_.SetFovYRad(fovYRad);
    camera.SetPerspective(fovYRad, camera.GetAspect(), (std::max)(0.001f, radius * 0.001f),
                          (std::max)(100.0f, radius * 20.0f));
    ++cameraRevision_;
    cameraCutPending_ = true;
    SaveActiveCameraState();
}

void ModelMaterialWorkspaceController::ResetPreviewCamera() {
    if (authoringMode_ == AuthoringMode::ModelMaterials && modelGuid_.IsValid()) {
        cameraStateStore_.Erase(EditorWorkspaceId::ModelMaterial, modelGuid_);
    }
    FitPreviewCamera();
}

void ModelMaterialWorkspaceController::ResetPreviewKeyLight() {
    if (previewLighting_.lights.empty()) {
        return;
    }

    MATH::Vec3 center{};
    float radius = 3.5f;
    const Bounds& bounds = GetActivePreviewScene().GetBounds();
    if (BOUNDS::IsUsable(bounds)) {
        center = (bounds.min + bounds.max) * 0.5f;
        radius = (std::max)(MATH::Length(bounds.max - bounds.min) * 0.5f, 0.5f);
    }

    const size_t lightIndex =
        selectedPreviewLightIndex_.has_value() && *selectedPreviewLightIndex_ < previewLighting_.lights.size()
            ? *selectedPreviewLightIndex_
            : 0u;
    auto& key = previewLighting_.lights[lightIndex];
    key.enabled = true;
    key.position = center + MATH::Vec3{radius * 0.8f, radius * 1.15f, -radius * 0.8f};
    key.color = {1.0f, 0.93f, 0.82f};
    key.intensity = kStudioKeyLumens;
    key.range = (std::max)(radius * 4.0f, 14.0f);
    MarkPreviewLightingChanged();
}

void ModelMaterialWorkspaceController::AddPreviewLight() {
    previewLightRangeDragActive_ = false;
    InitializePreviewLighting();
    RENDER3D::LIGHTING::SceneLightProvider light{};
    light.id = "ModelMaterialStudioInspect.Light." + std::to_string(previewLightIdCounter_++);
    light.name = "Point Light";
    light.enabled = true;
    light.type = RENDER3D::LIGHTING::SceneLightType::Point;
    light.color = {1.0f, 0.96f, 0.90f};
    light.priority = 100;
    previewLighting_.lights.push_back(std::move(light));
    selectedPreviewLightIndex_ = previewLighting_.lights.size() - 1u;
    ResetPreviewKeyLight();
    previewEnvironmentMode_ = PreviewEnvironmentMode::StudioInspect;
}

void ModelMaterialWorkspaceController::DeleteSelectedPreviewLight() {
    previewLightRangeDragActive_ = false;
    if (!selectedPreviewLightIndex_.has_value() || *selectedPreviewLightIndex_ >= previewLighting_.lights.size()) {
        return;
    }
    previewLighting_.lights.erase(previewLighting_.lights.begin() +
                                  static_cast<std::ptrdiff_t>(*selectedPreviewLightIndex_));
    if (previewLighting_.lights.empty()) {
        selectedPreviewLightIndex_.reset();
    } else {
        selectedPreviewLightIndex_ = (std::min)(*selectedPreviewLightIndex_, previewLighting_.lights.size() - 1u);
    }
    MarkPreviewLightingChanged();
}

void ModelMaterialWorkspaceController::FocusSelectedSlot() {
    const ModelAsset* model = previewScene_.GetModel();
    if (model == nullptr || !selectedMaterialIndex_) {
        return;
    }
    const std::vector<MATH::Mat4> globals = BOUNDS::BuildModelNodeGlobals(*model);
    Bounds selectedBounds = BOUNDS::EmptyBounds();
    for (size_t nodeIndex = 0u; nodeIndex < model->nodes.size(); ++nodeIndex) {
        const ModelNode& node = model->nodes[nodeIndex];
        if (node.meshIndex < 0 || static_cast<size_t>(node.meshIndex) >= model->meshes.size()) {
            continue;
        }
        for (const MeshPrimitive& primitive : model->meshes[static_cast<size_t>(node.meshIndex)].primitives) {
            if (primitive.materialIndex == *selectedMaterialIndex_) {
                BOUNDS::Encapsulate(selectedBounds, BOUNDS::TransformBounds(primitive.bounds, globals[nodeIndex]));
            }
        }
    }
    if (!BOUNDS::IsUsable(selectedBounds)) {
        FitPreviewCamera();
        return;
    }
    const MATH::Vec3 center = (selectedBounds.min + selectedBounds.max) * 0.5f;
    const float radius = (std::max)(MATH::Length(selectedBounds.max - selectedBounds.min) * 0.5f, 0.25f);
    cameraController_.Focus(center, radius * 2.4f);
    ++cameraRevision_;
    cameraCutPending_ = true;
    SaveActiveCameraState();
}

void ModelMaterialWorkspaceController::InitializePreviewLighting() {
    if (previewLightingInitialized_) {
        return;
    }
    // エンジン内蔵の Studio 環境（固定の太陽と HDR の空）から始める。
    ENVIRONMENT::EnvironmentProfileAsset studio{};
    std::string error;
    if (!ENVIRONMENT::LoadEnvironmentProfileAsset(ENVIRONMENT::ResolveBuiltinEnvironmentProfilePath(kStudioProfileId),
                                                  studio, error)) {
        statusMessage_ = error;
    }
    previewProfile_ = studio.profile;
    previewProfile_.screenSpaceReflections.enabled = false;
    RebuildPreviewEnvironment();

    previewLighting_ = {};
    RENDER3D::LIGHTING::SceneLightProvider key{};
    key.id = "ModelMaterialStudioInspect.Key";
    key.name = "Studio Movable Key";
    key.enabled = true;
    key.type = RENDER3D::LIGHTING::SceneLightType::Point;
    key.position = {2.5f, 3.5f, -2.5f};
    key.color = {1.0f, 0.96f, 0.9f};
    key.intensity = kStudioKeyLumens;
    key.range = 14.0f;
    key.priority = 100;
    previewLighting_.lights.push_back(std::move(key));
    previewLightingInitialized_ = true;
}

void ModelMaterialWorkspaceController::RebuildPreviewEnvironment() {
    static_cast<EnvironmentProfile&>(previewEnvironment_) = previewProfile_;
    previewEnvironment_.frame = ENVIRONMENT::BuildLightingFrame(previewProfile_, ENVIRONMENT::EnvironmentFrameState{},
                                                                ENVIRONMENT::GeoLocation{});
    MarkPreviewLightingChanged();
}

void ModelMaterialWorkspaceController::DrawPreviewCameraControls() {
    if (ImGui::DragFloat(TEXT::LocalizeWindow("ui.debugCamera.moveSpeed", "Move Speed").c_str(),
                         &previewCameraSpeedScale_, 0.05f, 0.05f, 10.0f)) {
        EditorDirectorCameraSettings& navigation = cameraController_.Settings();
        navigation.moveSpeed = std::clamp(previewCameraBaseMoveSpeed_ * previewCameraSpeedScale_, 0.01f, 1000.0f);
    }
    if (ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.wheel.zoom.step.bfd10b", "Wheel Zoom Step").c_str(),
                         &previewCameraWheelStepScale_, 0.05f, 0.05f, 10.0f)) {
        EditorDirectorCameraSettings& navigation = cameraController_.Settings();
        navigation.wheelMoveStep =
            std::clamp(previewCameraBaseWheelStep_ * previewCameraWheelStepScale_, 0.01f, 200.0f);
    }
    float fovDegrees = previewCameraFovYRad_ > 0.01f ? previewCameraFovYRad_ * 57.2957795131f : 60.0f;
    if (ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.fov.deg.dedfeb", "FOV (deg)").c_str(), &fovDegrees, 0.5f,
                         20.0f, 110.0f)) {
        previewCameraFovYRad_ = fovDegrees * 0.0174532925f;
        cameraController_.SetFovYRad(previewCameraFovYRad_);
        ++cameraRevision_;
    }
}

void ModelMaterialWorkspaceController::DrawPreviewEnvironmentControls() {
    const bool studioInspect = previewEnvironmentMode_ == PreviewEnvironmentMode::StudioInspect;
    ImGui::Text(TEXT::Localize("editor.ui.mode.s.2fc24d").c_str(), EnvironmentModeLabel(previewEnvironmentMode_));
    ImGui::TextWrapped(previewEnvironmentMode_ == PreviewEnvironmentMode::RuntimeMatch
                           ? "Uses the active scene environment and lights passed to the "
                             "same runtime renderer."
                           : "Uses the material workspace sky and movable inspection light.");
    ImGui::BeginDisabled(!studioInspect);
    ImGui::SeparatorText(TEXT::Localize("editor.ui.studio.inspect.environment.f9eb92").c_str());
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.create.lights.with.the.icon.then.select.their.viewport.glyph.e5730c").c_str());
    // Studio 環境の項目のうち、確認でよく触るものだけを環境資産と同じ表で描く。
    const auto hidden = [](std::initializer_list<std::string_view> shown, std::string_view key) {
        return std::find(shown.begin(), shown.end(), key) == shown.end();
    };
    bool changed =
        DrawJsonFields("ui.environment.sun", previewProfile_.sun, ENVIRONMENT::FIELDS::kSunFields,
                       [&](std::string_view key, auto&, FieldEditResult&) {
                           return hidden({"fixedAzimuthDegrees", "fixedElevationDegrees", "illuminance"}, key);
                       })
            .changed;
    changed |= DrawJsonFields("ui.environment.sky", previewProfile_.sky, ENVIRONMENT::FIELDS::kSkyFields,
                              [&](std::string_view key, auto&, FieldEditResult&) {
                                  return hidden({"yaw", "lightIntensity", "reflectionIntensity"}, key);
                              })
                   .changed;
    changed |= DrawJsonFields("ui.environment.exposure", previewProfile_.exposure, ENVIRONMENT::FIELDS::kExposureFields,
                              [&](std::string_view key, auto&, FieldEditResult&) {
                                  return hidden({"manualEv100", "compensationEv"}, key);
                              })
                   .changed;
    if (changed) {
        RebuildPreviewEnvironment();
    }
    ImGui::EndDisabled();
}

void ModelMaterialWorkspaceController::SubmitPreviewRequests(EditorWorkspaceHost& workspaceHost,
                                                             const ModelPreviewScene& activePreview,
                                                             bool canvasVisible) {
    EditorViewInstance& view = workspaceHost.GetModelMaterialPreviewView();
    EditorViewInstance& compareView = workspaceHost.GetModelMaterialCompareView();
    if (!IsPreviewReady()) {
        RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
        RENDER3D::EDITORVIEW::ClearRequest(compareView.renderViewId);
        previewRequestSubmitted_ = false;
        return;
    }
    const bool studioInspect = previewEnvironmentMode_ == PreviewEnvironmentMode::StudioInspect;
    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
    request.viewId = view.renderViewId;
    request.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    request.cameraFrame.camera = cameraController_.GetCamera();
    request.cameraFrame.revision = cameraRevision_;
    request.cameraFrame.cameraCut = cameraCutPending_;
    request.cameraFrame.valid = true;
    request.width = view.extent.width;
    request.height = view.extent.height;
    request.sceneRevision = activePreview.GetRevision();
    request.contentRevision = previewContentRevision_;
    request.visible = canvasVisible;
    request.drawDebug = false;
    request.schedulingPriority = 100u;
    request.shadingMode = ResolveShadingMode(previewDebugMode_);
    request.displayExposure = view.visualization.displayExposure;
    request.sceneSourceOverride = activePreview.GetSceneSource();
    request.environmentOverride = studioInspect ? &previewEnvironment_ : nullptr;
    request.lightingOverride = studioInspect ? &previewLighting_ : nullptr;
    request.useDefaultSkyIbl = studioInspect && previewEnvironment_.sky.reflectionIntensity > 0.0f;
    request.suppressSpatialLightingProviders = previewEnvironmentMode_ != PreviewEnvironmentMode::SceneMatch;
    request.spatialAntiAliasing = previewSpatialAaEnabled_;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    previewRequestSubmitted_ = true;
    if (previewCompareEnabled_) {
        RENDER3D::EDITORVIEW::EditorInteractiveViewRequest compareRequest = request;
        compareRequest.viewId = compareView.renderViewId;
        const bool compareStudio = previewEnvironmentMode_ != PreviewEnvironmentMode::StudioInspect;
        compareRequest.environmentOverride = compareStudio ? &previewEnvironment_ : nullptr;
        compareRequest.lightingOverride = compareStudio ? &previewLighting_ : nullptr;
        compareRequest.useDefaultSkyIbl = compareStudio && previewEnvironment_.sky.reflectionIntensity > 0.0f;
        compareRequest.suppressSpatialLightingProviders = compareStudio;
        compareRequest.schedulingPriority = 90u;
        RENDER3D::EDITORVIEW::SubmitRequest(compareRequest);
    } else {
        RENDER3D::EDITORVIEW::ClearRequest(compareView.renderViewId);
    }
}

} // namespace HIKARI::EDITOR
