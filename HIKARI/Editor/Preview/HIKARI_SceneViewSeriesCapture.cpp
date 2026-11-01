#include "Editor/Preview/HIKARI_SceneViewSeriesCapture.h"

#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Editor/Authoring/HIKARI_EditorAuthoringPreview.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

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

// 中心から見る人への向き（方位 0 が +Z、90 が +X、仰角は上向きが正）。
MATH::Vec3 OrbitDirection(float azimuthDegrees, float elevationDegrees) {
    const float yaw = azimuthDegrees * std::numbers::pi_v<float> / 180.0f;
    const float elevation = elevationDegrees * std::numbers::pi_v<float> / 180.0f;
    return {std::sin(yaw) * std::cos(elevation), std::sin(elevation), std::cos(yaw) * std::cos(elevation)};
}

// 照明つき・照明なし（底色）・中性の灰色の照明。
constexpr TEXT::EnumName<RENDER3D::EDITORVIEW::EditorInteractiveShadingMode> kShadingNames[] = {
    {RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Lit, "lit"},
    {RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Unlit, "unlit"},
    {RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Neutral, "neutral"},
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
    const std::optional<RENDER3D::EDITORVIEW::EditorInteractiveShadingMode> shading =
        TEXT::TryParseEnum(kShadingNames, parameters.value("shading", std::string("lit")));
    if (!shading)
        return OperationFailure("INVALID_SHADING", "shading accepts lit, unlit or neutral.");
    if (!std::isfinite(distance) || !std::isfinite(pitch) || !std::isfinite(fov) || !requested.is_array() ||
        requested.empty() || requested.size() > kMaximumViews) {
        return OperationFailure("INVALID_VIEWS", "Provide 1 to 6 views and finite distance, pitch and fov.");
    }
    for (const nlohmann::json& item : requested) {
        // 名前の角度、{azimuth, elevation, distance} の軌道、{position, target} の機位のどれか。
        std::string label;
        MATH::Vec3 eye{};
        MATH::Vec3 look = center;
        float viewFov = fov * std::numbers::pi_v<float> / 180.0f;
        if (item.is_string() && item.get<std::string>() == "editor") {
            // 人の編集相機と同じ位置・向きから写す（相機は動かさない）。部屋の中など、周りを回ると壁に遮られる時に使う。
            const Camera3D& editor = scene.GetCamera();
            eye = editor.GetPosition();
            look = editor.GetTarget();
            label = "editor";
            // fovDegrees が無ければ、人が見ているのと同じ画角にする。
            if (!parameters.contains("fovDegrees"))
                viewFov = editor.GetFovYRad();
        } else if (item.is_string()) {
            const ViewAngle* angle = FindViewAngle(item.get<std::string>());
            if (angle == nullptr) {
                views_.clear();
                return OperationFailure("UNKNOWN_VIEW",
                                        "Named views are editor, front, three_quarter, right, back, left and top.");
            }
            label = angle->id;
            eye =
                center +
                OrbitDirection(angle->yawDegrees, angle->pitchDegrees != 0.0f ? angle->pitchDegrees : pitch) * distance;
        } else if (item.is_object() && item.contains("position")) {
            eye = JSONREAD::Vec3MemberOr(item, "position", center);
            look = JSONREAD::Vec3MemberOr(item, "target", center);
            label = item.value("label", std::string("custom"));
        } else if (item.is_object()) {
            eye = center + OrbitDirection(item.value("azimuth", 0.0f),
                                          std::clamp(item.value("elevation", pitch), -89.0f, 89.0f)) *
                               std::clamp(item.value("distance", distance), 0.1f, 10000.0f);
            label = item.value("label", std::string("orbit"));
        } else {
            views_.clear();
            return OperationFailure("INVALID_VIEW", "Each view is a name, {azimuth, elevation, distance} or "
                                                    "{position, target}.");
        }
        if (!MATH::IsFinite(eye) || !MATH::IsFinite(look) || !(MATH::Length(look - eye) > 1.0e-3f)) {
            views_.clear();
            return OperationFailure("INVALID_VIEW", "Each view is a name, {azimuth, elevation, distance} or "
                                                    "{position, target} with distinct finite points.");
        }
        const float range = MATH::Length(look - eye);
        View view{std::move(label), {}};
        view.camera.SetPerspective(viewFov, static_cast<float>(width_) / static_cast<float>(height_),
                                   (std::max)(0.01f, range * 0.001f), (std::max)(1000.0f, range * 50.0f));
        // 真上・真下から見る時は上方向を奥へ倒して退化を避ける。
        const MATH::Vec3 forward = MATH::Normalize(look - eye);
        const MATH::Vec3 up =
            std::abs(forward.y) > 0.98f ? MATH::Vec3{0.0f, 0.0f, -1.0f} : MATH::Vec3{0.0f, 1.0f, 0.0f};
        view.camera.SetLookAt(eye, look, up);
        views_.push_back(std::move(view));
    }
    if (!viewId_.value) {
        viewId_ = {AllocateAuthoringPreviewViewId()};
    }
    folder_ = scene.GetAssetDatabase().GetLibraryRoot() / "EditorAI" / "Captures";
    sceneRevision_ = scene.GetSceneDocumentRevision();
    worldIdentity_ = scene.GetWorld().GetRuntimeIdentity();
    next_ = 0;
    sceneEdited_ = false;
    shading_ = *shading;
    images_ = nlohmann::json::array();
    waitingForExposure_ = true;
    exposureWaitStart_ = std::chrono::steady_clock::now();
    lastExposureLog2_ = std::numeric_limits<float>::quiet_NaN();
    return ExposureSettled() ? SubmitNext() : Pending();
}

// 検査ビューは主ビューの自動露出を借りる。順応の途中（夜から昼へ変えた直後など）に撮ると白や黒に飛ぶので、
// 目標との差が 1/4 段以内になるか、露出が動かなくなる（主ビューを描いていない）か、4 秒経つまで待つ。
bool SceneViewSeriesCapture::ExposureSettled() {
    const RENDER3D::COLOR::ExposureFrameState& state = POST::PostSystem::GetExposureState();
    const float current = std::log2((std::max)(state.exposure, 1.0e-30f));
    const bool settled = shading_ != RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Lit || !state.valid ||
                         state.mode != RENDER3D::COLOR::ExposureMode::Automatic ||
                         std::abs(current - std::log2((std::max)(state.targetExposure, 1.0e-30f))) <= 0.25f ||
                         current == lastExposureLog2_ ||
                         std::chrono::steady_clock::now() - exposureWaitStart_ > std::chrono::seconds(4);
    lastExposureLog2_ = current;
    waitingForExposure_ = !settled;
    return settled;
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
    request.shadingMode = shading_;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    EditorOperationResult result = StartEditorPreviewCapture(viewId_, folder_);
    if (!result.ok) {
        Clear();
        return result;
    }
    // 読み回しが即座に終わった場合も Poll で同じ経路を通す。
    ticket_ = result.data.contains("poll") ? result.data["poll"]["parameters"].value("captureId", uint64_t{})
                                           : result.data.value("captureId", uint64_t{});
    return Pending();
}

EditorOperationResult SceneViewSeriesCapture::Pending() const {
    return {true,
            "PENDING",
            waitingForExposure_
                ? std::string("正在等待自动曝光稳定。")
                : "正在渲染第 " + std::to_string(next_ + 1u) + "/" + std::to_string(views_.size()) + " 个角度。",
            {{"pending", true},
             {"stage", waitingForExposure_ ? "exposure" : "render"},
             {"poll", {{"operation", "scene.capture_views.status"}, {"parameters", nlohmann::json::object()}}}}};
}

EditorOperationResult SceneViewSeriesCapture::Poll(DocumentSceneBase* scene) {
    if (views_.empty()) {
        return OperationFailure("NO_CAPTURE", "No multi-view capture is pending.");
    }
    // 別の場面へ切り替わった時だけ止める。人が並行して編集した変更は止めずに、そのまま写して知らせる。
    if (scene == nullptr || scene->GetWorld().GetRuntimeIdentity() != worldIdentity_) {
        Clear();
        return OperationFailure("SCENE_CHANGED", "The scene was closed or replaced during capture; capture again.");
    }
    sceneEdited_ = sceneEdited_ || scene->GetSceneDocumentRevision() != sceneRevision_;
    if (waitingForExposure_) {
        return ExposureSettled() ? SubmitNext() : Pending();
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
    const bool edited = sceneEdited_;
    Clear();
    return OperationOk("已返回 " + std::to_string(count) + " 个角度的截图；画面内容仍需视觉检查。",
                       {{"images", std::move(images)},
                        {"source", "scene_inspection"},
                        {"sceneEditedDuringCapture", edited},
                        {"note", "Background renders that do not move the editor view or draw editor helpers."}});
}

void SceneViewSeriesCapture::Clear() {
    if (ticket_ != 0u) {
        RENDER3D::EDITORVIEW::CancelViewCapture(ticket_);
    }
    if (viewId_.value) {
        RENDER3D::EDITORVIEW::ClearRequest(viewId_);
    }
    ticket_ = 0u;
    waitingForExposure_ = false;
    views_.clear();
    next_ = 0u;
    images_ = nlohmann::json::array();
}

} // namespace HIKARI::EDITOR
