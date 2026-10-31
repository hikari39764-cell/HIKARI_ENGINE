#include "Core/Text/HIKARI_UtfPath.h"
#include "HIKARI_EditorAuthoringPreview.h"
#include "Core/HIKARI_AssetGuid.h"
#include <atomic>
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"

namespace HIKARI::EDITOR {
namespace {
std::atomic<uint64_t> nextViewId{1024};
}
uint64_t AllocateAuthoringPreviewViewId() noexcept { return nextViewId.fetch_add(1); }
EditorAuthoringPreview::~EditorAuthoringPreview() { Cancel(); }

EditorOperationResult EditorAuthoringPreview::Start(const std::filesystem::path& libraryRoot, nlohmann::json metadata) {
    if (pending_)
        return {false, "CAPTURE_BUSY", "A capture is already pending for this document."};
    const auto folder = libraryRoot / "EditorAI" / "Captures";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error)
        return {false, "CAPTURE_DIRECTORY_FAILED", error.message()};
    path_ = TEXT::PathToUtf8(folder / (GenerateAssetGuid().value + ".png"));
    metadata_ = std::move(metadata);
    pending_ = true;
    captureId_ = 0;
    error_.clear();
    return Pending();
}
EditorOperationResult EditorAuthoringPreview::Pending() const {
    auto data = metadata_;
    data["pending"] = true;
    data["stage"] = captureId_ ? "readback" : "render";
    data["poll"] = {{"operation", "authoring.capture_status"}, {"parameters", nlohmann::json::object()}};
    const auto output = RENDER3D::EDITORVIEW::GetOutput(viewId_);
    if (output.failure != RENDER3D::EDITORVIEW::EditorInteractiveViewFailure::None)
        data["renderFailure"] = RENDER3D::EDITORVIEW::GetEditorInteractiveViewFailureMessage(output.failure);
    return {true, "PENDING", captureId_ ? "正在读取预览图片" : "正在准备并渲染文档", std::move(data)};
}
void EditorAuthoringPreview::Tick(bool documentReady, nlohmann::json diagnostics) {
    if (pending_ && diagnostics.is_object())
        metadata_.update(diagnostics);
    if (pending_ && !captureId_ && error_.empty() && documentReady) {
        captureId_ = RENDER3D::EDITORVIEW::RequestViewCapture(viewId_, path_);
        if (!captureId_)
            error_ = "The capture queue could not accept this preview.";
    }
}
EditorOperationResult EditorAuthoringPreview::Poll() {
    if (!pending_)
        return {false, "NO_CAPTURE", "No capture is pending."};
    if (!error_.empty()) {
        Cancel();
        return {false, "CAPTURE_FAILED", error_, metadata_};
    }
    if (!captureId_)
        return Pending();
    auto result = PollEditorPreviewCapture(captureId_);
    if (result.ok && result.data.value("pending", false))
        return Pending();
    pending_ = false;
    RENDER3D::EDITORVIEW::ClearRequest(viewId_);
    if (!result.ok)
        return result;
    result.data.update(metadata_);
    result.data["images"][0]["label"] = metadata_.value("label", std::string("Preview"));
    return result;
}
void EditorAuthoringPreview::Cancel() {
    if (captureId_ && pending_)
        RENDER3D::EDITORVIEW::CancelViewCapture(captureId_);
    pending_ = false;
    RENDER3D::EDITORVIEW::ClearRequest(viewId_);
}
} // namespace HIKARI::EDITOR
