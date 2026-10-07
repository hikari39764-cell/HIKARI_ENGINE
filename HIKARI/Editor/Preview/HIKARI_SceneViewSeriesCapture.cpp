#include "Editor/Preview/HIKARI_SceneViewSeriesCapture.h"

#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Editor/Authoring/HIKARI_EditorAuthoringPreview.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::EDITOR {
namespace {
constexpr std::size_t kMaximumViews = 6u;

struct ViewAngle {
    const char* id;
    float yawDegrees;
    // 0 なら呼び出し側の仰角を使う。
    float pitchDegrees;
};

// 正面は +Z 側から中心を見る。glTF のモデルは +Z を向くので正面が写る。
constexpr ViewAngle kViewAngles[] = {
    {"front", 0.0f, 0.0f},  {"three_quarter", 45.0f, 0.0f}, {"right", 90.0f, 0.0f},
    {"back", 180.0f, 0.0f}, {"left", 270.0f, 0.0f},         {"top", 0.0f, 89.0f},
};

const ViewAngle* FindViewAngle(std::string_view id) {
    for (const ViewAngle& angle : kViewAngles) {
        if (id == angle.id) {
            return &angle;
        }
    }
    return nullptr;
}

bool ResolveCenter(DocumentSceneBase& scene, const nlohmann::json& parameters, MATH::Vec3& center, float& radius,
                   EditorOperationResult& failure) {
    if (parameters.contains("focusObjectId")) {
        GameObject* object = scene.GetWorld().FindObject(SceneObjectId{parameters.value("focusObjectId", uint64_t{})});
        if (object == nullptr) {
            failure = OperationFailure("OBJECT_NOT_FOUND", "focusObjectId does not name a scene object.");
            return false;
        }
        const MATH::Mat4 world = object->GetTransform().GetWorldMatrix();
        center = {world.m[3][0], world.m[3][1], world.m[3][2]};
        const auto bounds = ObserveSceneObjectBounds(scene, object->GetDocumentId());
        if (BOUNDS::IsUsable(bounds)) {
            const auto sphere = BOUNDS::ComputeCenterRadius(bounds);
            center = {sphere.x, sphere.y, sphere.z};
            radius = sphere.w;
        }
        return true;
    }
    if (parameters.contains("center")) {
        const nlohmann::json& value = parameters["center"];
        if (!value.is_array() || value.size() != 3u || !value[0].is_number() || !value[1].is_number() ||
            !value[2].is_number()) {
            failure = OperationFailure("INVALID_CENTER", "center must be [x, y, z].");
            return false;
        }
        center = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        if (!MATH::IsFinite(center)) {
            failure = OperationFailure("INVALID_CENTER", "center must be finite.");
            return false;
        }
        return true;
    }
    // 指定が無ければ、現在のシーンカメラが見ている点を中心にする。
    center = scene.GetResolvedCameraFrame().camera.GetTarget();
    return true;
}
} // namespace

SceneViewSeriesCapture::~SceneViewSeriesCapture() { Clear(); }

EditorOperationResult SceneViewSeriesCapture::Start(DocumentSceneBase& scene, const nlohmann::json& parameters) {
    if (!views_.empty()) {
        return OperationFailure("CAPTURE_BUSY", "A multi-view capture is already pending.");
    }
    MATH::Vec3 center{};
    float radius = 0.0f;
    EditorOperationResult failure{};
    if (!ResolveCenter(scene, parameters, center, radius, failure)) {
        return failure;
    }
    const float pitch = std::clamp(parameters.value("pitchDegrees", 20.0f), -80.0f, 89.0f);
    const float fov = std::clamp(parameters.value("fovDegrees", 50.0f), 10.0f, 120.0f);
    width_ = static_cast<uint32_t>(std::clamp(parameters.value("width", 768), 128, 1536));
    height_ = static_cast<uint32_t>(std::clamp(parameters.value("height", 432), 128, 1536));
    const float halfFov = std::atan(std::tan(fov * std::numbers::pi_v<float> / 360.0f) *
                                    (std::min)(1.0f, static_cast<float>(width_) / static_cast<float>(height_)));
    const float fittedDistance = radius > 0 ? radius * 1.1f / std::sin(halfFov) : 6.0f;
    const float distance = std::clamp(parameters.value("distance", fittedDistance), 0.1f, 10000.0f);
    const nlohmann::json requested =
        parameters.value("views", nlohmann::json::array({"front", "three_quarter", "right", "top"}));
    if (!std::isfinite(distance) || !std::isfinite(pitch) || !std::isfinite(fov) || !requested.is_array() ||
        requested.empty() || requested.size() > kMaximumViews) {
        return OperationFailure("INVALID_VIEWS", "Provide 1 to 6 views and finite distance, pitch and fov.");
    }
    for (const nlohmann::json& item : requested) {
        const ViewAngle* angle = item.is_string() ? FindViewAngle(item.get<std::string>()) : nullptr;
        if (angle == nullptr) {
            views_.clear();
            return OperationFailure("UNKNOWN_VIEW", "views accepts front, three_quarter, right, back, left and top.");
        }
        const float yaw = angle->yawDegrees * std::numbers::pi_v<float> / 180.0f;
        const float elevation =
            (angle->pitchDegrees != 0.0f ? angle->pitchDegrees : pitch) * std::numbers::pi_v<float> / 180.0f;
        const MATH::Vec3 direction{std::sin(yaw) * std::cos(elevation), std::sin(elevation),
                                   std::cos(yaw) * std::cos(elevation)};
        View view{angle->id, {}};
        view.camera.SetPerspective(fov * std::numbers::pi_v<float> / 180.0f,
                                   static_cast<float>(width_) / static_cast<float>(height_),
                                   (std::max)(0.01f, distance * 0.001f), (std::max)(1000.0f, distance * 50.0f));
        // 真上からの時は上方向を奥へ倒して退化を避ける。
        const MATH::Vec3 up =
            angle->pitchDegrees > 80.0f ? MATH::Vec3{0.0f, 0.0f, -1.0f} : MATH::Vec3{0.0f, 1.0f, 0.0f};
        view.camera.SetLookAt(center + direction * distance, center, up);
        views_.push_back(std::move(view));
    }
    if (!viewId_.value) {
        viewId_ = {AllocateAuthoringPreviewViewId()};
    }
    folder_ = scene.GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "Captures";
    sceneRevision_ = scene.GetSceneDocumentRevision();
    worldIdentity_ = scene.GetWorld().GetRuntimeIdentity();
    next_ = 0;
    images_ = nlohmann::json::array();
    return SubmitNext();
}

EditorOperationResult SceneViewSeriesCapture::SubmitNext() {
    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
    request.viewId = viewId_;
    request.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    request.width = width_;
    request.height = height_;
    request.cameraFrame.camera = views_[next_].camera;
    request.cameraFrame.valid = true;
    request.cameraFrame.cameraCut = true;
    request.cameraFrame.revision = next_ + 1u;
    request.sceneRevision = sceneRevision_;
    request.contentRevision = next_ + 1u;
    request.visible = true;
    request.drawDebug = false;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    EditorOperationResult result = StartEditorPreviewCapture(viewId_, folder_);
    if (!result.ok) {
        Clear();
        return result;
    }
    // 読み回しが即座に終わった場合も Poll で同じ経路を通す。
    ticket_ = result.data.contains("poll") ? result.data["poll"].value("captureId", uint64_t{})
                                           : result.data.value("captureId", uint64_t{});
    return Pending();
}

EditorOperationResult SceneViewSeriesCapture::Pending() const {
    return {true,
            "PENDING",
            "正在渲染第 " + std::to_string(next_ + 1u) + "/" + std::to_string(views_.size()) + " 个角度。",
            {{"pending", true}, {"stage", "render"}, {"poll", {{"action", "scene.capture_views.status"}}}}};
}

EditorOperationResult SceneViewSeriesCapture::Poll(DocumentSceneBase* scene) {
    if (views_.empty()) {
        return OperationFailure("NO_CAPTURE", "No multi-view capture is pending.");
    }
    if (scene == nullptr || scene->GetSceneDocumentRevision() != sceneRevision_ ||
        scene->GetWorld().GetRuntimeIdentity() != worldIdentity_) {
        Clear();
        return OperationFailure("DOCUMENT_CONFLICT", "The scene changed during capture; capture it again.");
    }
    EditorOperationResult result = PollEditorPreviewCapture(ticket_);
    if (!result.ok) {
        Clear();
        return result;
    }
    if (result.data.value("pending", false)) {
        return Pending();
    }
    nlohmann::json image = result.data["images"][0];
    image["label"] = "Scene inspection · " + views_[next_].label;
    image["camera"] = result.data["camera"];
    image["frameIndex"] = result.data["frameIndex"];
    images_.push_back(std::move(image));
    ++next_;
    if (next_ < views_.size()) {
        return SubmitNext();
    }
    nlohmann::json images = std::move(images_);
    const std::size_t count = views_.size();
    Clear();
    return OperationOk("已返回 " + std::to_string(count) + " 个角度的截图；画面内容仍需视觉检查。",
                       {{"images", std::move(images)},
                        {"source", "scene_inspection"},
                        {"note", "Separate inspection renders; use capture.preview for final game output."}});
}

void SceneViewSeriesCapture::Clear() {
    if (ticket_ != 0u) {
        RENDER3D::EDITORVIEW::CancelViewCapture(ticket_);
    }
    if (viewId_.value) {
        RENDER3D::EDITORVIEW::ClearRequest(viewId_);
    }
    ticket_ = 0u;
    views_.clear();
    next_ = 0u;
    images_ = nlohmann::json::array();
}

} // namespace HIKARI::EDITOR
