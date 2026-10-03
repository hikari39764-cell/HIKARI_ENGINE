#include "HIKARI/Project/CameraViewSnapshot.h"
#include "HIKARI/Project/ActionViews.h"

#include <cmath>
#include <cstring>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_CameraOptics.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {

using PROJECT::INTERNAL::ToRuntime;

constexpr float kProjectionEpsilon = 1.0e-6f;

template <typename Record>
bool HasWritableHeader(const Record& record, uint32_t requiredSize, uint32_t requiredVersion) noexcept {
    return record.structSize >= requiredSize && record.version == requiredVersion;
}

template <typename Record>
void WriteRecordPrefix(Record& destination, const Record& source, uint32_t size) noexcept {
    std::memcpy(&destination, &source, size);
}

bool IsValidRect(const GameProjectScreenRect& rect, uint32_t width, uint32_t height) noexcept {
    return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) &&
           std::isfinite(rect.bottom) && rect.left >= 0.0f && rect.top >= 0.0f && rect.right > rect.left &&
           rect.bottom > rect.top && rect.right <= static_cast<float>(width) &&
           rect.bottom <= static_cast<float>(height);
}

bool IsValidSnapshot(const GameProjectCameraViewSnapshot& snapshot) noexcept {
    if (!HasWritableHeader(snapshot, kGameProjectCameraViewSnapshotV1Size, kGameProjectCameraViewSnapshotVersion) ||
        snapshot.worldIdentity == 0u || snapshot.primaryViewId == 0u || snapshot.sceneRenderWidth == 0u ||
        snapshot.sceneRenderHeight == 0u || snapshot.outputPhysicalWidth == 0u || snapshot.outputPhysicalHeight == 0u ||
        snapshot.uiLogicalWidth == 0u || snapshot.uiLogicalHeight == 0u || !std::isfinite(snapshot.aspectRatio) ||
        snapshot.aspectRatio <= 0.0f ||
        !HasWritableHeader(snapshot.lens, kGameProjectCameraLensV1Size, kGameProjectCameraLensVersion) ||
        snapshot.lens.reserved != 0u ||
        !HasWritableHeader(snapshot.lens.depthOfField, kGameProjectDepthOfFieldSettingsV1Size,
                           kGameProjectDepthOfFieldSettingsVersion) ||
        snapshot.lens.depthOfField.enabled > 1u || snapshot.lens.depthOfField.reserved[0] != 0u ||
        snapshot.lens.depthOfField.reserved[1] != 0u || snapshot.lens.depthOfField.reserved[2] != 0u ||
        !CAMERA::IsValidDepthOfFieldSettings(
            {snapshot.lens.depthOfField.enabled != 0u, snapshot.lens.depthOfField.focusDistance,
             snapshot.lens.depthOfField.focusRange, snapshot.lens.depthOfField.nearTransition,
             snapshot.lens.depthOfField.farTransition, snapshot.lens.depthOfField.nearBlurRadius1080pPixels,
             snapshot.lens.depthOfField.farBlurRadius1080pPixels}) ||
        !MATH::IsFinite(ToRuntime(snapshot.pose.eye)) || !MATH::IsFinite(ToRuntime(snapshot.pose.target)) ||
        !MATH::IsFinite(ToRuntime(snapshot.pose.up))) {
        return false;
    }

    switch (snapshot.source) {
    case GameProjectPrimaryViewSource::Game:
    case GameProjectPrimaryViewSource::EditorScene:
    case GameProjectPrimaryViewSource::EditorCameraPreview:
    case GameProjectPrimaryViewSource::EditorSequencePreview: break;
    default: return false;
    }
    switch (snapshot.presentationTarget) {
    case GameProjectPresentationTarget::BackBuffer:
    case GameProjectPresentationTarget::EditorViewport: break;
    default: return false;
    }
    switch (snapshot.lens.stage) {
    case GameProjectCameraLensStage::Rendered: break;
    default: return false;
    }
    if (!std::isfinite(snapshot.lens.verticalFovRadians) || !std::isfinite(snapshot.lens.orthographicHeight)) {
        return false;
    }
    switch (snapshot.lens.projection) {
    case GameProjectCameraProjectionKind::Perspective:
        if (snapshot.lens.verticalFovRadians <= 0.0f || snapshot.lens.verticalFovRadians >= 3.14159265f) {
            return false;
        }
        break;
    case GameProjectCameraProjectionKind::Orthographic:
        if (snapshot.lens.orthographicHeight <= 0.0f) {
            return false;
        }
        break;
    default: return false;
    }

    if (!std::isfinite(snapshot.lens.nearClip) || !std::isfinite(snapshot.lens.farClip) ||
        snapshot.lens.nearClip < 0.0f || snapshot.lens.farClip <= snapshot.lens.nearClip ||
        !IsValidRect(snapshot.sceneContentPhysical, snapshot.outputPhysicalWidth, snapshot.outputPhysicalHeight) ||
        !IsValidRect(snapshot.sceneContentLogical, snapshot.uiLogicalWidth, snapshot.uiLogicalHeight)) {
        return false;
    }

    const MATH::Vec3 forward = ToRuntime(snapshot.pose.target) - ToRuntime(snapshot.pose.eye);
    const MATH::Vec3 up = ToRuntime(snapshot.pose.up);
    if (MATH::Length(forward) <= kProjectionEpsilon || MATH::Length(up) <= kProjectionEpsilon ||
        std::abs(MATH::Dot(MATH::Normalize(forward), MATH::Normalize(up))) >= 0.999f) {
        return false;
    }
    return true;
}

} // namespace

bool CameraView::TryGetViewSnapshot(GameProjectCameraViewSnapshot& outSnapshot) const noexcept {
    if (!HasWritableHeader(outSnapshot, kGameProjectCameraViewSnapshotV1Size, kGameProjectCameraViewSnapshotVersion)) {
        return false;
    }
    try {
        World* world = static_cast<World*>(world_);
        if (world == nullptr || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world)) {
            return false;
        }
        GameProjectCameraViewSnapshot snapshot{};
        if (!SERVICES::detail::TryGetLatestPrimaryViewSnapshot(world->GetRuntimeIdentity(), snapshot) ||
            !IsValidSnapshot(snapshot)) {
            return false;
        }
        WriteRecordPrefix(outSnapshot, snapshot, kGameProjectCameraViewSnapshotV1Size);
        return true;
    } catch (...) {
        return false;
    }
}

bool ProjectWorldToScreen(const GameProjectCameraViewSnapshot& snapshot, GameProjectFloat3 worldPosition,
                          GameProjectScreenCoordinateSpace coordinateSpace,
                          GameProjectWorldToScreenResult& outResult) noexcept {
    if (!IsValidSnapshot(snapshot) ||
        !HasWritableHeader(outResult, kGameProjectWorldToScreenResultV1Size, kGameProjectWorldToScreenResultVersion) ||
        outResult.reserved != 0u || !MATH::IsFinite(ToRuntime(worldPosition))) {
        return false;
    }

    const GameProjectScreenRect* contentRect = nullptr;
    switch (coordinateSpace) {
    case GameProjectScreenCoordinateSpace::PhysicalOutputPixels: contentRect = &snapshot.sceneContentPhysical; break;
    case GameProjectScreenCoordinateSpace::UiLogicalPixels: contentRect = &snapshot.sceneContentLogical; break;
    default: return false;
    }

    const MATH::Vec3 eye = ToRuntime(snapshot.pose.eye);
    const MATH::Vec3 forward = MATH::Normalize(ToRuntime(snapshot.pose.target) - eye);
    const MATH::ViewBasisRH basis = MATH::BuildViewBasisRH(forward, ToRuntime(snapshot.pose.up));
    const MATH::Vec3 relative = ToRuntime(worldPosition) - eye;
    const float viewDepth = MATH::Dot(relative, basis.forward);
    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    if (snapshot.lens.projection == GameProjectCameraProjectionKind::Perspective) {
        if (std::abs(viewDepth) <= kProjectionEpsilon) {
            return false;
        }
        const float halfHeight = std::tan(snapshot.lens.verticalFovRadians * 0.5f) * viewDepth;
        const float halfWidth = halfHeight * snapshot.aspectRatio;
        normalizedX = MATH::Dot(relative, basis.right) / halfWidth;
        normalizedY = MATH::Dot(relative, basis.up) / halfHeight;
    } else {
        const float halfHeight = snapshot.lens.orthographicHeight * 0.5f;
        const float halfWidth = halfHeight * snapshot.aspectRatio;
        normalizedX = MATH::Dot(relative, basis.right) / halfWidth;
        normalizedY = MATH::Dot(relative, basis.up) / halfHeight;
    }
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) || !std::isfinite(viewDepth)) {
        return false;
    }

    GameProjectWorldToScreenResult result{};
    result.frameIndex = snapshot.frameIndex;
    result.coordinateSpace = coordinateSpace;
    result.x = contentRect->left + (normalizedX * 0.5f + 0.5f) * (contentRect->right - contentRect->left);
    result.y = contentRect->top + (0.5f - normalizedY * 0.5f) * (contentRect->bottom - contentRect->top);
    result.viewDepth = viewDepth;
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
        return false;
    }
    if (viewDepth > 0.0f) {
        result.flags |= GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::InFront);
    }
    if (viewDepth >= snapshot.lens.nearClip && viewDepth <= snapshot.lens.farClip) {
        result.flags |= GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::WithinDepthRange);
    }
    if (viewDepth > 0.0f && result.x >= contentRect->left && result.x < contentRect->right &&
        result.y >= contentRect->top && result.y < contentRect->bottom) {
        result.flags |= GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::InsideSceneContent);
    }
    WriteRecordPrefix(outResult, result, kGameProjectWorldToScreenResultV1Size);
    return true;
}

} // namespace HIKARI
