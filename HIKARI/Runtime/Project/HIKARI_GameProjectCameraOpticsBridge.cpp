#include "HIKARI/Project/ActionViews.h"

#include <cmath>
#include <cstring>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_CameraOptics.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectCameraLensConversion.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {
constexpr float kMinimumFocusDistance = CAMERA::kMinDepthOfFieldFocusDistance;

template <typename Record>
bool CanWriteRecord(const Record& record, uint32_t requiredSize, uint32_t requiredVersion) noexcept {
    return record.structSize >= requiredSize && record.version == requiredVersion;
}

template <typename Record>
void WriteRecordPrefix(Record& destination, const Record& source, uint32_t size) noexcept {
    std::memcpy(&destination, &source, size);
}

bool HasZeroReserved(const GameProjectDepthOfFieldSettings& settings) noexcept {
    return settings.reserved[0] == 0u && settings.reserved[1] == 0u && settings.reserved[2] == 0u;
}

bool CopyLens(const Camera3D& camera, uint64_t sourceCameraHandle, GameProjectCameraLensStage stage,
              GameProjectCameraLens& outLens) noexcept {
    const CAMERA::DepthOfFieldSettings& optics = camera.GetDepthOfFieldSettings();
    return PROJECT::INTERNAL::CopyCameraLens(outLens, sourceCameraHandle, stage,
                                             camera.IsOrthographic() ? GameProjectCameraProjectionKind::Orthographic
                                                                     : GameProjectCameraProjectionKind::Perspective,
                                             camera.GetFovYRad(), camera.GetOrthographicHeight(), camera.GetNearZ(),
                                             camera.GetFarZ(), optics);
}

bool CopyAuthoredLens(const CameraComponent& component, uint64_t cameraObjectHandle,
                      GameProjectCameraLens& outLens) noexcept {
    const CAMERA::DepthOfFieldSettings& optics = component.GetDepthOfFieldSettings();
    return PROJECT::INTERNAL::CopyCameraLens(outLens, cameraObjectHandle, GameProjectCameraLensStage::Authored,
                                             component.IsOrthographic() ? GameProjectCameraProjectionKind::Orthographic
                                                                        : GameProjectCameraProjectionKind::Perspective,
                                             component.GetFovYRad(), component.GetOrthographicHeight(),
                                             component.GetNearClip(), component.GetFarClip(), optics);
}

bool ToCameraObjectId(World& world, uint64_t runtimeHandle, SceneObjectId& outObjectId) noexcept {
    outObjectId = {};
    if (runtimeHandle == 0u) {
        return true;
    }
    GameObject* object = PROJECT::INTERNAL::FindObject(&world, runtimeHandle);
    if (object == nullptr || object->GetDocumentId().value == 0u) {
        return false;
    }
    outObjectId = object->GetDocumentId();
    return true;
}

bool ToRuntimeObjectHandle(const World& world, SceneObjectId objectId, uint64_t& outRuntimeHandle) noexcept {
    outRuntimeHandle = 0u;
    if (objectId.value == 0u) {
        return true;
    }
    const GameObject* object = world.FindObject(objectId);
    if (object == nullptr) {
        return false;
    }
    outRuntimeHandle = object->GetRuntimeHandle().ToValue();
    return outRuntimeHandle != 0u;
}

bool IsUsableWorld(World* world) noexcept {
    return world != nullptr && world->IsOwnerThread() && !GameProjectSdkAccess::IsWorldClosing(*world);
}

CAMERA::DepthOfFieldSettings ToRuntime(const GameProjectDepthOfFieldSettings& source) noexcept {
    CAMERA::DepthOfFieldSettings settings{};
    settings.enabled = source.enabled != 0u;
    settings.focusDistance = source.focusDistance;
    settings.focusRange = source.focusRange;
    settings.nearTransition = source.nearTransition;
    settings.farTransition = source.farTransition;
    settings.nearBlurRadius1080pPixels = source.nearBlurRadius1080pPixels;
    settings.farBlurRadius1080pPixels = source.farBlurRadius1080pPixels;
    return settings;
}

bool ToRuntimeFocus(World& world, const GameProjectCameraFocusRequest& request,
                    CAMERA::CameraFocusSubmission& outSubmission) noexcept {
    if (!CanWriteRecord(request, kGameProjectCameraFocusRequestV1Size, kGameProjectCameraFocusRequestVersion) ||
        request.sourceId == 0u || request.reserved != 0u || !std::isfinite(request.weight) || request.weight < 0.0f ||
        request.weight > 1.0f || !ToCameraObjectId(world, request.cameraObjectHandle, outSubmission.cameraObjectId)) {
        return false;
    }

    outSubmission.sourceId = request.sourceId;
    outSubmission.priority = request.priority;
    outSubmission.weight = request.weight;
    switch (request.mode) {
    case GameProjectCameraFocusMode::Distance:
        if (!std::isfinite(request.distance) || request.distance < kMinimumFocusDistance) {
            return false;
        }
        outSubmission.mode = CAMERA::CameraFocusMode::Distance;
        outSubmission.distance = request.distance;
        break;
    case GameProjectCameraFocusMode::WorldPoint:
        if (!MATH::IsFinite(PROJECT::INTERNAL::ToRuntime(request.worldPoint))) {
            return false;
        }
        outSubmission.mode = CAMERA::CameraFocusMode::WorldPoint;
        outSubmission.worldPoint = PROJECT::INTERNAL::ToRuntime(request.worldPoint);
        break;
    case GameProjectCameraFocusMode::WorldPlane: {
        const MATH::Vec3 planePoint = PROJECT::INTERNAL::ToRuntime(request.planePoint);
        const MATH::Vec3 planeNormal = PROJECT::INTERNAL::ToRuntime(request.planeNormal);
        const float normalLength = MATH::Length(planeNormal);
        if (!MATH::IsFinite(planePoint) || !MATH::IsFinite(planeNormal) || !std::isfinite(normalLength) ||
            normalLength <= 1.0e-5f) {
            return false;
        }
        outSubmission.mode = CAMERA::CameraFocusMode::WorldPlane;
        outSubmission.planePoint = planePoint;
        outSubmission.planeNormal = planeNormal;
        break;
    }
    default: return false;
    }
    return true;
}
} // namespace

bool CameraView::GetAuthoredLens(uint64_t cameraObjectHandle, GameProjectCameraLens& outLens) const noexcept {
    if (!CanWriteRecord(outLens, kGameProjectCameraLensV1Size, kGameProjectCameraLensVersion) ||
        cameraObjectHandle == 0u) {
        return false;
    }
    try {
        World* world = static_cast<World*>(world_);
        if (!IsUsableWorld(world)) {
            return false;
        }
        GameObject* object = PROJECT::INTERNAL::FindObject(world, cameraObjectHandle);
        const CameraComponent* component = object != nullptr ? object->GetComponent<CameraComponent>() : nullptr;
        if (component == nullptr || !component->IsEnabled()) {
            return false;
        }
        GameProjectCameraLens lens{};
        if (!CopyAuthoredLens(*component, cameraObjectHandle, lens)) {
            return false;
        }
        WriteRecordPrefix(outLens, lens, kGameProjectCameraLensV1Size);
        return true;
    } catch (...) {
        return false;
    }
}

bool CameraView::GetControlLens(GameProjectCameraLens& outLens) const noexcept {
    if (!CanWriteRecord(outLens, kGameProjectCameraLensV1Size, kGameProjectCameraLensVersion)) {
        return false;
    }
    try {
        World* world = static_cast<World*>(world_);
        if (!IsUsableWorld(world)) {
            return false;
        }
        const GameplayCameraService* gameplay = world->Services().Find<GameplayCameraService>();
        const CameraDirector* director = world->Services().Find<CameraDirector>();
        if (gameplay == nullptr || !gameplay->IsRuntimeCameraActive() || director == nullptr) {
            return false;
        }
        const CameraDirectorStatus status = director->GetStatus();
        const SceneObjectId sourceCameraObjectId =
            status.activeOverrideAffectsControlBasis ? status.activeSourceCameraObjectId : status.baseCameraObjectId;
        uint64_t sourceCameraHandle = 0u;
        if (!ToRuntimeObjectHandle(*world, sourceCameraObjectId, sourceCameraHandle)) {
            return false;
        }
        GameProjectCameraLens lens{};
        if (!CopyLens(director->GetControlCamera(), sourceCameraHandle, GameProjectCameraLensStage::Control, lens)) {
            return false;
        }
        WriteRecordPrefix(outLens, lens, kGameProjectCameraLensV1Size);
        return true;
    } catch (...) {
        return false;
    }
}

bool CameraView::GetRenderedLens(GameProjectCameraLens& outLens) const noexcept {
    if (!CanWriteRecord(outLens, kGameProjectCameraLensV1Size, kGameProjectCameraLensVersion)) {
        return false;
    }
    GameProjectCameraViewSnapshot snapshot{};
    if (!TryGetViewSnapshot(snapshot)) {
        return false;
    }
    WriteRecordPrefix(outLens, snapshot.lens, kGameProjectCameraLensV1Size);
    return true;
}

bool CameraView::SubmitDepthOfField(const GameProjectCameraDepthOfFieldRequest& request) const noexcept {
    if (!CanWriteRecord(request, kGameProjectCameraDepthOfFieldRequestV1Size,
                        kGameProjectCameraDepthOfFieldRequestVersion) ||
        request.sourceId == 0u || request.reserved != 0u || !std::isfinite(request.weight) || request.weight < 0.0f ||
        request.weight > 1.0f ||
        !CanWriteRecord(request.settings, kGameProjectDepthOfFieldSettingsV1Size,
                        kGameProjectDepthOfFieldSettingsVersion) ||
        request.settings.enabled > 1u || !HasZeroReserved(request.settings)) {
        return false;
    }
    const CAMERA::DepthOfFieldSettings settings = ToRuntime(request.settings);
    if (!CAMERA::IsValidDepthOfFieldSettings(settings)) {
        return false;
    }
    try {
        World* world = static_cast<World*>(world_);
        if (!IsUsableWorld(world)) {
            return false;
        }
        CAMERA::CameraDepthOfFieldSubmission submission{};
        if (!ToCameraObjectId(*world, request.cameraObjectHandle, submission.cameraObjectId)) {
            return false;
        }
        CAMERA::CameraRigService* rig = world->Services().Find<CAMERA::CameraRigService>();
        if (rig == nullptr) {
            return false;
        }
        submission.sourceId = request.sourceId;
        submission.priority = request.priority;
        submission.weight = request.weight;
        submission.settings = settings;
        return rig->SubmitDepthOfField(submission);
    } catch (...) {
        return false;
    }
}

bool CameraView::GetAuthoredMotionBlur(uint64_t cameraObjectHandle,
                                       GameProjectMotionBlurSettings& outSettings) const noexcept {
    if (!CanWriteRecord(outSettings, kGameProjectMotionBlurSettingsV1Size, kGameProjectMotionBlurSettingsVersion) ||
        cameraObjectHandle == 0u) {
        return false;
    }
    try {
        World* world = static_cast<World*>(world_);
        if (!IsUsableWorld(world)) {
            return false;
        }
        GameObject* object = PROJECT::INTERNAL::FindObject(world, cameraObjectHandle);
        const CameraComponent* component = object != nullptr ? object->GetComponent<CameraComponent>() : nullptr;
        if (component == nullptr || !component->IsEnabled()) {
            return false;
        }
        const CAMERA::MotionBlurSettings& authored = component->GetMotionBlurSettings();
        GameProjectMotionBlurSettings settings{};
        settings.enabled = authored.enabled ? 1u : 0u;
        settings.shutterAngleDegrees = authored.shutterAngleDegrees;
        settings.maxBlur1080pPixels = authored.maxBlur1080pPixels;
        WriteRecordPrefix(outSettings, settings, kGameProjectMotionBlurSettingsV1Size);
        return true;
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitMotionBlur(const GameProjectCameraMotionBlurRequest& request) const noexcept {
    if (!CanWriteRecord(request, kGameProjectCameraMotionBlurRequestV1Size,
                        kGameProjectCameraMotionBlurRequestVersion) ||
        request.reserved != 0u ||
        !CanWriteRecord(request.settings, kGameProjectMotionBlurSettingsV1Size,
                        kGameProjectMotionBlurSettingsVersion) ||
        request.settings.enabled > 1u || request.settings.reserved[0] != 0u || request.settings.reserved[1] != 0u ||
        request.settings.reserved[2] != 0u) {
        return false;
    }
    try {
        World* world = static_cast<World*>(world_);
        if (!IsUsableWorld(world)) {
            return false;
        }
        CAMERA::CameraMotionBlurSubmission submission{};
        if (!ToCameraObjectId(*world, request.cameraObjectHandle, submission.cameraObjectId)) {
            return false;
        }
        CAMERA::CameraRigService* rig = world->Services().Find<CAMERA::CameraRigService>();
        if (rig == nullptr) {
            return false;
        }
        submission.sourceId = request.sourceId;
        submission.priority = request.priority;
        submission.weight = request.weight;
        submission.settings = {request.settings.enabled != 0u, request.settings.shutterAngleDegrees,
                               request.settings.maxBlur1080pPixels};
        // 送り主・重み・範囲の検証は CameraRigService が持つ（DoF と同じ受付）。
        return rig->SubmitMotionBlur(submission);
    } catch (...) {
        return false;
    }
}

bool CameraView::SubmitFocus(const GameProjectCameraFocusRequest& request) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        if (!IsUsableWorld(world)) {
            return false;
        }
        CAMERA::CameraFocusSubmission submission{};
        if (!ToRuntimeFocus(*world, request, submission)) {
            return false;
        }
        CAMERA::CameraRigService* rig = world->Services().Find<CAMERA::CameraRigService>();
        return rig != nullptr && rig->SubmitFocus(submission);
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI
