#pragma once
#include <filesystem>
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {
EditorOperationResult StartEditorPreviewCapture(RENDER3D::RenderViewId viewId, const std::filesystem::path& folder = {},
                                                const std::string& outputPath = {});
EditorOperationResult PollEditorPreviewCapture(RENDER3D::EDITORVIEW::PreviewCaptureId id);
} // namespace HIKARI::EDITOR
