#include "Core/Text/HIKARI_UtfPath.h"
#include "HIKARI_EditorPreviewCapture.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI::EDITOR {
EditorOperationResult PollEditorPreviewCapture(RENDER3D::EDITORVIEW::PreviewCaptureId id) {
    const auto result = RENDER3D::EDITORVIEW::GetViewCaptureResult(id);
    if (!result.done)
        return {true,
                "PENDING",
                "正在读取预览图片。",
                {{"pending", true}, {"poll", {{"action", "capture.status"}, {"captureId", id}}}}};
    if (!result.error.empty())
        return {false, "CAPTURE_FAILED", result.error};
    return {true,
            "OK",
            "预览图片已就绪。",
            {{"captureId", id},
             {"viewId", result.viewId.value},
             {"width", result.width},
             {"height", result.height},
             {"outputRevision", result.outputRevision},
             {"images", nlohmann::json::array({{{"path", result.path}, {"label", "Preview"}}})}}};
}
EditorOperationResult StartEditorPreviewCapture(RENDER3D::RenderViewId viewId, const std::filesystem::path& folder,
                                                const std::string& outputPath) {
    if (!viewId.value)
        return {false, "NO_PREVIEW", "当前工作区没有可截图的预览。请使用后台文档预览或附加图片。"};
    std::error_code error;
    auto directory = folder.empty() ? std::filesystem::temp_directory_path(error) / "HIKARI-EditorAI" : folder;
    if (error)
        return {false, "CAPTURE_DIRECTORY_FAILED", error.message()};
    const auto path = outputPath.empty() ? directory / (GenerateAssetGuid().value + ".png")
                                         : std::filesystem::path(std::u8string(
                                               reinterpret_cast<const char8_t*>(outputPath.data()), outputPath.size()));
    if (path.extension() != ".png")
        return {false, "PNG_REQUIRED", "截图输出路径必须以 .png 结尾。"};
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return {false, "CAPTURE_DIRECTORY_FAILED", error.message()};
    const auto id = RENDER3D::EDITORVIEW::RequestViewCapture(viewId, TEXT::PathToUtf8(path));
    if (!id)
        return {false, "CAPTURE_BUSY", "截图队列繁忙或输出路径已被使用。"};
    return PollEditorPreviewCapture(id);
}
} // namespace HIKARI::EDITOR
