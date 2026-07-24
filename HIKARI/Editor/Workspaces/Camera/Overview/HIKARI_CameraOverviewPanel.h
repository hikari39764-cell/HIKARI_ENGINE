#pragma once

#include <cstdint>

#include "Editor/Views/HIKARI_EditorViewInstance.h"

namespace HIKARI {

class DocumentSceneBase;
struct EditorContext;

namespace EDITOR {

enum class CameraOverviewActionKind : uint8_t {
  None,
  Select,
  SetDefault,
  ViewThrough,
  ExitView,
  SnapToView,
};

struct CameraOverviewAction {
  CameraOverviewActionKind kind = CameraOverviewActionKind::None;
  SceneObjectId cameraObjectId{};

  bool IsValid() const noexcept {
    return kind != CameraOverviewActionKind::None;
  }
};

struct CameraOverviewPanelResult {
  CameraOverviewAction action{};
};

class CameraOverviewPanel {
public:
  CameraOverviewPanelResult
  DrawOverviewContents(DocumentSceneBase &scene, EditorContext &context,
                       EditorViewInstance &view) const;

  CameraOverviewPanelResult
  DrawCameraListContents(DocumentSceneBase &scene,
                         EditorContext &context) const;
};

} // namespace EDITOR

} // namespace HIKARI
