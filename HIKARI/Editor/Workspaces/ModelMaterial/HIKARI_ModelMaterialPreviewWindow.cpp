#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include <algorithm>
#include "Core/HIKARI_TimeService.h"
#include "Editor/Preview/HIKARI_ModelPreviewPicking.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif
namespace HIKARI::EDITOR {
namespace {
RENDER3D::EDITORVIEW::EditorInteractiveShadingMode ResolveShadingMode(
    ModelMaterialWorkspaceController::PreviewDebugMode mode) noexcept {
  using Mode = RENDER3D::EDITORVIEW::EditorInteractiveShadingMode;
  switch (mode) {
  case ModelMaterialWorkspaceController::PreviewDebugMode::Lit:
    return Mode::Lit;
  case ModelMaterialWorkspaceController::PreviewDebugMode::Neutral:
    return Mode::Neutral;
  case ModelMaterialWorkspaceController::PreviewDebugMode::Clay:
    return Mode::NeutralClay;
  case ModelMaterialWorkspaceController::PreviewDebugMode::Unlit:
    return Mode::Unlit;
  }
  return Mode::Lit;
}
const char *DebugModeLabel(
    ModelMaterialWorkspaceController::PreviewDebugMode mode) noexcept {
  switch (mode) {
  case ModelMaterialWorkspaceController::PreviewDebugMode::Lit:
    return "Lit";
  case ModelMaterialWorkspaceController::PreviewDebugMode::Neutral:
    return "Neutral";
  case ModelMaterialWorkspaceController::PreviewDebugMode::Clay:
    return "Clay";
  case ModelMaterialWorkspaceController::PreviewDebugMode::Unlit:
    return "Unlit";
  }
  return "Lit";
}

} // namespace

void ModelMaterialWorkspaceController::DrawPreviewToolbar(
    ModelMaterialWorkspaceResult &result,
    EditorCommandRouter &commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
  constexpr ImVec2 kIconSize{28.0f, 28.0f};
  ToolbarLabel(authoringMode_ == AuthoringMode::MaterialAsset
                   ? "Material Sphere"
               : IsEditingModel() ? modelDisplayName_.c_str()
                                  : "No model open");
  ImGui::SameLine();
  if (authoringMode_ == AuthoringMode::MaterialAsset) {
    StatusBadge("Standalone Material", EditorStatusTone::Ready);
  } else {
    const size_t overrideCount = static_cast<size_t>(std::count_if(
        slots_.begin(), slots_.end(), [](const SlotState &slot) {
          return slot.view.overrideMaterialGuid.IsValid();
        }));
    const std::string bindingLabel =
        overrideCount == 0u ? "Imported materials"
                            : std::to_string(overrideCount) + " override(s)";
    StatusBadge(bindingLabel.c_str(), overrideCount == 0u
                                          ? EditorStatusTone::Normal
                                          : EditorStatusTone::Ready);
    if (const ModelMaterialSlotView *selected = GetSelectedSlot()) {
      ImGui::SameLine();
      const std::string selectedLabel = "Part: " + selected->displayName;
      StatusBadge(selectedLabel.c_str(), EditorStatusTone::Ready);
    }
  }
  ImGui::SameLine(0.0f, 12.0f);
  if (ImGui::RadioButton(
          "Runtime Asset Lighting",
          previewEnvironmentMode_ == PreviewEnvironmentMode::RuntimeMatch)) {
    previewEnvironmentMode_ = PreviewEnvironmentMode::RuntimeMatch;
    previewDebugMode_ = PreviewDebugMode::Lit;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton(
          "Studio Inspect",
          previewEnvironmentMode_ == PreviewEnvironmentMode::StudioInspect)) {
    previewEnvironmentMode_ = PreviewEnvironmentMode::StudioInspect;
  }

  const bool studioInspect = previewEnvironmentMode_ ==
      PreviewEnvironmentMode::StudioInspect;
  ImGui::SameLine(0.0f, 6.0f);
  if (IconButton(EditorGlyph::Add, "ModelMaterialAddStudioLight",
                 studioInspect ? EditorButtonTone::Primary
                               : EditorButtonTone::Quiet,
                 kIconSize,
                 "Create a Studio point or directional light")) {
    previewEnvironmentMode_ = PreviewEnvironmentMode::StudioInspect;
    ImGui::OpenPopup("ModelMaterialAddStudioLightPopup");
  }
  if (ImGui::BeginPopup("ModelMaterialAddStudioLightPopup")) {
    if (ImGui::MenuItem("Point Light")) {
      AddPreviewLight(RENDER3D::LIGHTING::SceneLightType::Point);
    }
    if (ImGui::MenuItem("Directional Light")) {
      AddPreviewLight(RENDER3D::LIGHTING::SceneLightType::Directional);
    }
    ImGui::EndPopup();
  }
  if (selectedPreviewLightIndex_.has_value()) {
    const bool selectedDirectional =
        *selectedPreviewLightIndex_ < previewLighting_.lights.size() &&
        previewLighting_.lights[*selectedPreviewLightIndex_].type ==
            RENDER3D::LIGHTING::SceneLightType::Directional;
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Translate, "ModelMaterialLightTranslate",
                   previewLightGizmoState_.operation ==
                           EditorTransformGizmoOperation::Translate
                       ? EditorButtonTone::Primary
                       : EditorButtonTone::Quiet,
                   kIconSize, "Move the selected light in the viewport")) {
      previewLightGizmoState_.operation =
          EditorTransformGizmoOperation::Translate;
    }
    if (selectedDirectional) {
      ImGui::SameLine();
      if (IconButton(EditorGlyph::Rotate, "ModelMaterialLightRotate",
                     previewLightGizmoState_.operation ==
                             EditorTransformGizmoOperation::Rotate
                         ? EditorButtonTone::Primary
                         : EditorButtonTone::Quiet,
                     kIconSize,
                     "Rotate the selected directional light")) {
        previewLightGizmoState_.operation =
            EditorTransformGizmoOperation::Rotate;
      }
    }
  }

  ImGui::SameLine(0.0f, 12.0f);
  ImGui::BeginDisabled(!IsPreviewReady());
  if (IconButton(EditorGlyph::Focus, "ModelMaterialFit",
                 EditorButtonTone::Quiet, kIconSize,
                 "Frame the complete model (F)")) {
    if (commandRouter.CanExecute(EditorCommandId::FocusSelection)) {
      (void)commandRouter.Execute(EditorCommandId::FocusSelection);
    } else {
      FitPreviewCamera();
    }
  }
  ImGui::SameLine();
  if (IconButton(EditorGlyph::Revert, "ModelMaterialResetCamera",
                 EditorButtonTone::Quiet, kIconSize,
                 "Reset the saved camera for this model")) {
    ResetPreviewCamera();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(authoringMode_ != AuthoringMode::ModelMaterials);
  if (ImGui::SmallButton("Focus Slot")) {
    FocusSelectedSlot();
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  ImGui::SameLine(0.0f, 8.0f);
  if (IconButton(EditorGlyph::Light, "ModelMaterialLightingPanel",
                 previewLightingPanelOpen_ ? EditorButtonTone::Primary
                                           : EditorButtonTone::Quiet,
                 kIconSize,
                 "Open the Preview Lighting panel")) {
    previewLightingPanelOpen_ = !previewLightingPanelOpen_;
  }
  ImGui::SameLine(0.0f, 8.0f);
  if (IconButton(EditorGlyph::More, "ModelMaterialPreviewControls",
                 EditorButtonTone::Quiet, kIconSize, "Preview controls")) {
    ImGui::OpenPopup("ModelMaterialPreviewControlsPopup");
  }
  if (ImGui::BeginPopup("ModelMaterialPreviewControlsPopup")) {
    ImGui::TextUnformatted("Preview navigation");
    ImGui::Separator();
    ImGui::TextDisabled("RMB + WASDQE   Fly");
    ImGui::TextDisabled("Alt + LMB       Orbit");
    ImGui::TextDisabled("MMB             Pan");
    ImGui::TextDisabled("Mouse Wheel     Zoom");
    ImGui::TextDisabled("F               Frame model");
    ImGui::Separator();
    ImGui::SeparatorText("Camera");
    DrawPreviewCameraControls();
    ImGui::Separator();
    ImGui::TextWrapped(
        "Camera state is kept per model and per workspace for this Editor "
        "session.");
    ImGui::SeparatorText("Preview Environment");
    DrawPreviewEnvironmentControls();
    ImGui::Checkbox("Preview anti-aliasing (FXAA)",
                    &previewSpatialAaEnabled_);
    ImGui::SeparatorText("Debug View");
    if (ImGui::BeginCombo("##MaterialPreviewDebugMode",
                          DebugModeLabel(previewDebugMode_))) {
      for (PreviewDebugMode mode : {PreviewDebugMode::Lit,
                                    PreviewDebugMode::Neutral,
                                    PreviewDebugMode::Clay,
                                    PreviewDebugMode::Unlit}) {
        if (ImGui::Selectable(DebugModeLabel(mode),
                              previewDebugMode_ == mode)) {
          previewDebugMode_ = mode;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::EndPopup();
  }

  ImGui::SameLine(0.0f, 12.0f);
  ToolbarDivider();
  ImGui::SameLine(0.0f, 12.0f);
  if (IconButton(EditorGlyph::Exit, "ModelMaterialExit",
                 EditorButtonTone::Quiet, kIconSize,
                 "Return to the Scene workspace")) {
    result.exitToSceneRequested = true;
  }
  ImGui::Separator();
#else
  (void)result;
  (void)commandRouter;
#endif
}

void ModelMaterialWorkspaceController::DrawPreviewWindow(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost,
    ModelMaterialWorkspaceResult &result,
    EditorCommandRouter &commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
  (void)scene;
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoCollapse;
  EditorViewInstance &view = workspaceHost.GetModelMaterialPreviewView();
  if (!ImGui::Begin("Preview###ModelMaterial/Preview", nullptr, flags)) {
    view.extent = {};
    view.interaction = {};
    previewRequestSubmitted_ = false;
    previewOutputReady_ = false;
    RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
    ImGui::End();
    return;
  }

  DrawPreviewToolbar(result, commandRouter);
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x, 160.0f);
  canvasSize.y = (std::max)(canvasSize.y, 160.0f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(origin,
                          {origin.x + canvasSize.x, origin.y + canvasSize.y},
                          IM_COL32(14, 18, 24, 255));

  view.extent.width = static_cast<uint32_t>(canvasSize.x);
  view.extent.height = static_cast<uint32_t>(canvasSize.y);
  const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
      RENDER3D::EDITORVIEW::GetOutput(view.renderViewId);
  previewOutputWidth_ = output.width;
  previewOutputHeight_ = output.height;
  previewOutputRevision_ = output.outputRevision;
  const ModelPreviewScene &activePreview = GetActivePreviewScene();
  previewOutputReady_ = IsPreviewReady() && output.ready &&
                        output.colorSrv.ptr != 0u &&
                        output.sceneRevision == activePreview.GetRevision();

  ImGui::SetCursorScreenPos(origin);
  if (previewOutputReady_) {
    ImGui::Image(reinterpret_cast<ImTextureID>(
                     static_cast<uintptr_t>(output.colorSrv.ptr)),
                 canvasSize);
  } else {
    ImGui::InvisibleButton("##ModelMaterialPreviewCanvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const char *message = output.status ==
            RENDER3D::EDITORVIEW::EditorInteractiveViewStatus::Failed
            ? RENDER3D::EDITORVIEW::GetEditorInteractiveViewFailureMessage(
                  output.failure).data()
        : IsPreviewReady()
            ? "Preparing runtime-sourced material preview..."
        : authoringMode_ == AuthoringMode::MaterialAsset
            ? "Preparing the material sphere..."
            : "Choose a model from Materials & Parts";
    const ImVec2 textSize = ImGui::CalcTextSize(message);
    drawList->AddText({origin.x + (canvasSize.x - textSize.x) * 0.5f,
                       origin.y + (canvasSize.y - textSize.y) * 0.5f},
                      IM_COL32(177, 191, 207, 255), message);
  }
  const bool canvasVisible = ImGui::IsItemVisible();
  const bool hovered = ImGui::IsItemHovered();
  const bool studioInspect =
      previewEnvironmentMode_ == PreviewEnvironmentMode::StudioInspect;
  const PreviewLightViewportInteraction lightInteraction =
      studioInspect && IsPreviewReady()
          ? DrawPreviewLightViewportInteraction(origin.x, origin.y,
                canvasSize.x, canvasSize.y)
          : PreviewLightViewportInteraction{};
  if (hovered) {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY,
                           ImGuiInputFlags_LockThisFrame);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX,
                           ImGuiInputFlags_LockThisFrame);
  }
  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const ImGuiIO &io = ImGui::GetIO();
  const EditorViewInputBlockState block = QueryEditorViewInputBlockState();
  EditorViewInputSubmission submission{};
  submission.rect = {origin.x, origin.y, canvasSize.x, canvasSize.y};
  submission.visible = canvasVisible;
  submission.focused = focused;
  submission.hovered = hovered;
  submission.rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  submission.rightMouseClicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  submission.middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  submission.middleMouseClicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  submission.leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  submission.leftMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  submission.altDown = io.KeyAlt;
  submission.pointerBlocked = block.pointer ||
                              lightInteraction.glyphHovered ||
                              lightInteraction.gizmoInteracting;
  submission.keyboardBlocked = block.keyboard;
  inputRouter_.SetGizmoCapture(view.renderViewId,
                               lightInteraction.gizmoInteracting);
  const EditorViewInputState &input =
      inputRouter_.Submit(view.renderViewId, submission);
  view.interaction.visible = canvasVisible;
  view.interaction.focused = focused;
  view.interaction.hovered = hovered;
  view.interaction.mouseCaptured = input.rightMouseCaptured ||
                                   input.middleMouseCaptured ||
                                   input.orbitMouseCaptured;
  view.interaction.keyboardActive = input.AcceptsKeyboard();
  view.interaction.gizmoCaptured = lightInteraction.gizmoInteracting;

  EditorDirectorCameraInput cameraInput{};
  cameraInput.lookActive = input.rightMouseCaptured;
  cameraInput.orbitActive = input.orbitMouseCaptured;
  cameraInput.panActive = input.middleMouseCaptured;
  cameraInput.mouseDeltaX = io.MouseDelta.x;
  cameraInput.mouseDeltaY = io.MouseDelta.y;
  cameraInput.wheelDelta =
      input.AcceptsWheel() ? io.MouseWheel * (io.KeyShift ? 4.0f : 1.0f)
                           : 0.0f;
  cameraInput.fast = io.KeyShift;
  if (input.rightMouseCaptured) {
    cameraInput.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
    cameraInput.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
    cameraInput.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
    cameraInput.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
    cameraInput.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
    cameraInput.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
  }
  const float aspect = (std::max)(0.05f, canvasSize.x / canvasSize.y);
  if (cameraController_.Update(cameraInput,
                               TIME::GetFrameContext().unscaledDt, aspect)) {
    ++cameraRevision_;
    SaveActiveCameraState();
  }
  const bool requestSurfaceSelection =
      authoringMode_ == AuthoringMode::ModelMaterials &&
      previewOutputReady_ && hovered && submission.leftMouseClicked &&
      !submission.pointerBlocked && !io.KeyAlt &&
      !input.rightMouseCaptured && !input.middleMouseCaptured &&
      !input.orbitMouseCaptured;
  if (requestSurfaceSelection) {
    ModelPreviewPointerRay ray{};
    const float normalizedX =
        (io.MousePos.x - origin.x) / canvasSize.x;
    const float normalizedY =
        (io.MousePos.y - origin.y) / canvasSize.y;
    if (BuildModelPreviewPointerRay(
            cameraController_.GetCamera(), normalizedX, normalizedY, ray)) {
      const ModelPreviewSurfacePick pick =
          PickModelPreviewSurface(ray, previewScene_);
      if (pick.valid) {
        (void)SelectSlotForEditing(scene, pick.materialIndex,
                                   statusMessage_);
      }
    }
  }
  if (input.AcceptsKeyboard() && hovered && !io.WantTextInput) {
    (void)commandRouter.ProcessViewportShortcuts(true);
  }

  if (IsPreviewReady()) {
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
    request.visible = canvasVisible;
    request.drawDebug = false;
    request.schedulingPriority = 100u;
    request.shadingMode = ResolveShadingMode(previewDebugMode_);
    request.displayExposure = view.visualization.displayExposure;
    request.sceneSourceOverride = activePreview.GetSceneSource();
    request.environmentOverride =
        studioInspect ? &previewEnvironment_ : nullptr;
    request.lightingOverride = studioInspect ? &previewLighting_ : nullptr;
    request.suppressSpatialLightingProviders = true;
    request.useDefaultSkyIbl =
        studioInspect && previewEnvironment_.sky.enabled &&
        previewEnvironment_.sky.reflectionIntensity > 0.0f;
    request.spatialAntiAliasing = previewSpatialAaEnabled_;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    previewRequestSubmitted_ = true;
  } else {
    RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
    previewRequestSubmitted_ = false;
  }
  cameraCutPending_ = false;

  bool showingStaleCompile = false;
  if (previewOutputReady_ && authoringDocument_.HasMaterial()) {
    const MaterialCompileResult &compile =
        authoringDocument_.GetCompileResult();
    showingStaleCompile = !compile.success;
    if (showingStaleCompile) {
      const char *warning =
          "CURRENT MATERIAL V5 GRAPH IS INVALID - PREVIEW PAUSED";
      const ImVec2 warningSize = ImGui::CalcTextSize(warning);
      drawList->AddRectFilled(
          origin,
          {origin.x + canvasSize.x, origin.y + canvasSize.y},
          IM_COL32(18, 12, 10, 174));
      drawList->AddRectFilled(
          {origin.x + 8.0f, origin.y + 8.0f},
          {origin.x + warningSize.x + 24.0f,
           origin.y + warningSize.y + 18.0f},
          IM_COL32(118, 58, 18, 238), 5.0f);
      drawList->AddText(
          {origin.x + 16.0f, origin.y + 13.0f},
          IM_COL32(255, 213, 133, 255), warning);
    }
  }
  if (authoringMode_ == AuthoringMode::ModelMaterials &&
      previewOutputReady_ && hovered && !io.KeyAlt) {
    drawList->AddText(
        {origin.x + 10.0f,
         origin.y + (showingStaleCompile ? 42.0f : 10.0f)},
        IM_COL32(214, 228, 238, 220),
        "Click a visible surface to edit its material");
  }

  ImGui::SetCursorScreenPos({origin.x, origin.y + canvasSize.y});
  ImGui::Dummy(ImVec2(1.0f, 1.0f));
  ImGui::End();
  DrawPreviewLightingPanel(scene);
#else
  (void)scene;
  (void)workspaceHost;
  (void)result;
  (void)commandRouter;
#endif
}

} // namespace HIKARI::EDITOR
