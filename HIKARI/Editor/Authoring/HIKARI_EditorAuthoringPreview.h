#pragma once
#include <filesystem>
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {
uint64_t AllocateAuthoringPreviewViewId() noexcept;

// 文書の準備完了と画像読回しの境界を共有する。文書/再生状態は所有しない。
class EditorAuthoringPreview {
public:
    explicit EditorAuthoringPreview(RENDER3D::RenderViewId viewId) : viewId_(viewId) {}
    ~EditorAuthoringPreview();
    EditorOperationResult Start(const std::filesystem::path& libraryRoot,
        nlohmann::json metadata);
    void Tick(bool documentReady, nlohmann::json diagnostics = nlohmann::json::object());
    void Fail(std::string error) { error_ = std::move(error); }
    EditorOperationResult Poll();
    void Cancel();
    bool IsPending() const noexcept { return pending_; }
private:
    EditorOperationResult Pending() const;
    RENDER3D::RenderViewId viewId_;
    RENDER3D::EDITORVIEW::PreviewCaptureId captureId_ = 0;
    bool pending_ = false;
    std::string error_;
    std::string path_;
    nlohmann::json metadata_;
};
}
