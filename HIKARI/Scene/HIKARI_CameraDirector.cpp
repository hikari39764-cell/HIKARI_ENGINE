#include "Scene/Camera/HIKARI_CameraCollision.h"
#include "Core/Math/HIKARI_ScalarMath.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "HIKARI_CameraDirector.h"

#include <algorithm>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

namespace {
constexpr float kMinAspect = 0.0001f;
constexpr float kVectorEpsilon = 0.00001f;
constexpr float kFloatEpsilon = 0.00001f;

float ClampAspect(float aspect, const Camera3D& fallbackCamera) {
    if (std::isfinite(aspect) && aspect >= kMinAspect) {
        return aspect;
    }
    const float fallbackAspect = fallbackCamera.GetAspect();
    return std::isfinite(fallbackAspect) && fallbackAspect >= kMinAspect ? fallbackAspect : 16.0f / 9.0f;
}

float Lerp(float from, float to, float amount) { return from + (to - from) * amount; }

MATH::Vec3 Lerp(const MATH::Vec3& from, const MATH::Vec3& to, float amount) { return from + (to - from) * amount; }

bool NearlyEqual(float lhs, float rhs) { return std::abs(lhs - rhs) <= kFloatEpsilon; }

bool ProjectionDiffers(const Camera3D& lhs, const Camera3D& rhs) {
    return lhs.IsOrthographic() != rhs.IsOrthographic() ||
           (lhs.IsOrthographic() && !NearlyEqual(lhs.GetOrthographicHeight(), rhs.GetOrthographicHeight())) ||
           !NearlyEqual(lhs.GetFovYRad(), rhs.GetFovYRad()) || !NearlyEqual(lhs.GetAspect(), rhs.GetAspect()) ||
           !NearlyEqual(lhs.GetNearZ(), rhs.GetNearZ()) || !NearlyEqual(lhs.GetFarZ(), rhs.GetFarZ());
}

// 同じ源のまま毎フレーム進むカメラの投影が「跳んだ」か。演出の画角アニメーションのような連続した変焦（1 フレームの
// 画角の変化が約 1° 以下）は跳びとみなさない。時域の履歴（TAA・DLSS）は前後のフレームの行列で再投影でき、変焦の
// 間ずっと履歴を捨てると、細かい模様（画面に貼った捕捉など）が累積できずに崩れて揺れ続ける。投影の種類・縦横比・
// 近遠の切断面の変化と、画角や正交の高さの大きな跳びは従来どおり投影の切り替えとして扱う。
bool ProjectionJumps(const Camera3D& lhs, const Camera3D& rhs) {
    constexpr float kContinuousFovStepRad = 0.0175f;
    constexpr float kContinuousOrthographicStep = 0.02f;
    if (lhs.IsOrthographic() != rhs.IsOrthographic() || !NearlyEqual(lhs.GetAspect(), rhs.GetAspect()) ||
        !NearlyEqual(lhs.GetNearZ(), rhs.GetNearZ()) || !NearlyEqual(lhs.GetFarZ(), rhs.GetFarZ())) {
        return true;
    }
    if (lhs.IsOrthographic()) {
        const float height = (std::max)(std::abs(lhs.GetOrthographicHeight()), kFloatEpsilon);
        return std::abs(lhs.GetOrthographicHeight() - rhs.GetOrthographicHeight()) >
               height * kContinuousOrthographicStep;
    }
    return std::abs(lhs.GetFovYRad() - rhs.GetFovYRad()) > kContinuousFovStepRad;
}

// 同じ視点の機位と演出カメラの間で所有者だけを渡す場合、時系列を切らない。
// 注視点までの距離は画に影響しないため、正規化済みの view 行列で比較する。演出の終わりの画から追従へ戻る時のように
// 同じ所から続く機位でも、呼吸・手持ちなど毎フレーム動く分だけ数 mm・0.1° ほどずれる。完全一致だけを同じ画とすると
// 戻るたびに時域の履歴を捨てて画面が一瞬崩れるので、回転成分は約 0.17°、平移は 1 cm まで同じ画とみなす。
bool ViewMatches(const Camera3D& lhs, const Camera3D& rhs) {
    constexpr float kRotationTolerance = 0.003f;
    constexpr float kTranslationTolerance = 0.01f;
    const auto& a = lhs.GetView();
    const auto& b = rhs.GetView();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) {
            const float tolerance = column == 3 && row < 3 ? kTranslationTolerance : kRotationTolerance;
            if (std::abs(a.m[column][row] - b.m[column][row]) > tolerance)
                return false;
        }
    return true;
}

CAMERA::DepthOfFieldSettings BlendDepthOfFieldSettings(const CAMERA::DepthOfFieldSettings& from,
                                                       const CAMERA::DepthOfFieldSettings& to, float amount) noexcept {
    CAMERA::DepthOfFieldSettings result{};
    const bool fromEndpoint = amount <= 0.0f;
    const bool toEndpoint = amount >= 1.0f;
    const float fromNearRadius = from.enabled ? from.nearBlurRadius1080pPixels : 0.0f;
    const float toNearRadius = to.enabled ? to.nearBlurRadius1080pPixels : 0.0f;
    const float fromFarRadius = from.enabled ? from.farBlurRadius1080pPixels : 0.0f;
    const float toFarRadius = to.enabled ? to.farBlurRadius1080pPixels : 0.0f;
    result.nearBlurRadius1080pPixels = fromEndpoint ? from.nearBlurRadius1080pPixels
                                       : toEndpoint ? to.nearBlurRadius1080pPixels
                                                    : Lerp(fromNearRadius, toNearRadius, amount);
    result.farBlurRadius1080pPixels = fromEndpoint ? from.farBlurRadius1080pPixels
                                      : toEndpoint ? to.farBlurRadius1080pPixels
                                                   : Lerp(fromFarRadius, toFarRadius, amount);
    result.enabled = fromEndpoint ? from.enabled
                     : toEndpoint ? to.enabled
                                  : result.nearBlurRadius1080pPixels > 0.0f || result.farBlurRadius1080pPixels > 0.0f;
    result.focusDistance = Lerp(from.focusDistance, to.focusDistance, amount);
    result.focusRange = Lerp(from.focusRange, to.focusRange, amount);
    result.nearTransition = Lerp(from.nearTransition, to.nearTransition, amount);
    result.farTransition = Lerp(from.farTransition, to.farTransition, amount);
    return CAMERA::NormalizeDepthOfFieldSettings(result);
}

// 切り替えの途中はシャッター角を補間する（無効側は 0°）。端点ではそのカメラの設定のまま。
CAMERA::MotionBlurSettings BlendMotionBlurSettings(const CAMERA::MotionBlurSettings& from,
                                                   const CAMERA::MotionBlurSettings& to, float amount) noexcept {
    if (amount <= 0.0f || amount >= 1.0f) {
        return amount <= 0.0f ? from : to;
    }
    CAMERA::MotionBlurSettings result{};
    result.shutterAngleDegrees =
        Lerp(from.enabled ? from.shutterAngleDegrees : 0.0f, to.enabled ? to.shutterAngleDegrees : 0.0f, amount);
    result.maxBlur1080pPixels = Lerp(from.maxBlur1080pPixels, to.maxBlur1080pPixels, amount);
    result.enabled = result.shutterAngleDegrees > 0.0f;
    return CAMERA::NormalizeMotionBlurSettings(result);
}

bool ResolveFocusDistance(const Camera3D& camera, const CAMERA::CameraFocusSubmission& request,
                          float& outDistance) noexcept {
    outDistance = 0.0f;
    const MATH::Vec3 eye = camera.GetPosition();
    const MATH::Vec3 forward = MATH::Normalize(camera.GetTarget() - eye);
    if (!MATH::IsFinite(eye) || !MATH::IsFinite(forward) || MATH::Length(forward) <= kVectorEpsilon) {
        return false;
    }

    float distance = 0.0f;
    switch (request.mode) {
    case CAMERA::CameraFocusMode::Distance: distance = request.distance; break;
    case CAMERA::CameraFocusMode::WorldPoint: distance = MATH::Dot(request.worldPoint - eye, forward); break;
    case CAMERA::CameraFocusMode::WorldPlane: {
        const float denominator = MATH::Dot(forward, request.planeNormal);
        if (!std::isfinite(denominator) || std::abs(denominator) <= kVectorEpsilon) {
            return false;
        }
        distance = MATH::Dot(request.planePoint - eye, request.planeNormal) / denominator;
        break;
    }
    default: return false;
    }
    if (!std::isfinite(distance) || distance < CAMERA::kMinDepthOfFieldFocusDistance) {
        return false;
    }
    outDistance = distance;
    return true;
}

bool FocusRequestWins(const CAMERA::CameraFocusSubmission& focus,
                      const CAMERA::CameraDepthOfFieldSubmission& settings) noexcept {
    return focus.priority > settings.priority ||
           (focus.priority == settings.priority && focus.sourceId <= settings.sourceId);
}

bool IsCameraObjectEnabled(const World& world, SceneObjectId cameraObjectId) noexcept {
    const GameObject* object = world.FindObject(cameraObjectId);
    const CameraComponent* camera = object != nullptr ? object->GetComponent<CameraComponent>() : nullptr;
    return camera != nullptr && camera->IsEnabled();
}

MATH::Vec3 SafeUpForForward(const MATH::Vec3& forward, MATH::Vec3 up);

MATH::Vec3 ExtractAxis(const MATH::Mat4& matrix, int column) {
    return {matrix.m[column][0], matrix.m[column][1], matrix.m[column][2]};
}

MATH::Vec3 SafeUpForForward(const MATH::Vec3& forward, MATH::Vec3 up) {
    up = MATH::Normalize(up);
    if (MATH::Length(up) <= kVectorEpsilon || std::abs(MATH::Dot(forward, up)) >= 0.999f) {
        up = {0.0f, 1.0f, 0.0f};
    }
    if (std::abs(MATH::Dot(forward, up)) >= 0.999f) {
        up = {1.0f, 0.0f, 0.0f};
    }
    return up;
}
} // namespace

Camera3D CameraDirector::BlendViews(const Camera3D& from, const Camera3D& to, float amount) {
    // Projection kinds do not have a physically meaningful interpolated lens.
    if (from.IsOrthographic() != to.IsOrthographic())
        return to;
    const float blendAmount = std::clamp(std::isfinite(amount) ? amount : 1.0f, 0.0f, 1.0f);
    Camera3D result{};
    const MATH::Vec3 eye = Lerp(from.GetPosition(), to.GetPosition(), blendAmount);
    MATH::Vec3 target = Lerp(from.GetTarget(), to.GetTarget(), blendAmount);
    MATH::Vec3 up = MATH::Normalize(Lerp(from.GetUp(), to.GetUp(), blendAmount));
    if (MATH::Length(target - eye) <= kVectorEpsilon) {
        target = eye + MATH::Vec3{0.0f, 0.0f, 1.0f};
    }
    if (MATH::Length(up) <= kVectorEpsilon) {
        up = {0.0f, 1.0f, 0.0f};
    }
    up = SafeUpForForward(MATH::Normalize(target - eye), up);

    result.SetPerspective(
        Lerp(from.GetFovYRad(), to.GetFovYRad(), blendAmount), Lerp(from.GetAspect(), to.GetAspect(), blendAmount),
        Lerp(from.GetNearZ(), to.GetNearZ(), blendAmount), Lerp(from.GetFarZ(), to.GetFarZ(), blendAmount));
    if (to.IsOrthographic())
        result.SetOrthographic(Lerp(from.GetOrthographicHeight(), to.GetOrthographicHeight(), blendAmount),
                               result.GetAspect(), result.GetNearZ(), result.GetFarZ());
    result.SetLookAt(eye, target, up);
    result.SetDepthOfFieldSettings(
        BlendDepthOfFieldSettings(from.GetDepthOfFieldSettings(), to.GetDepthOfFieldSettings(), blendAmount));
    result.SetMotionBlurSettings(
        BlendMotionBlurSettings(from.GetMotionBlurSettings(), to.GetMotionBlurSettings(), blendAmount));
    return result;
}

void CameraDirector::Reset() {
    baseCameraObjectId_ = {};
    activeSourceCameraObjectId_ = {};
    activeOverrideToken_ = {};
    overrides_.clear();
    resolvedFrame_ = {};
    controlCamera_ = {};
    blendStartCamera_ = {};
    activeBlend_ = {};
    blendElapsedSeconds_ = 0.0f;
    blendActive_ = false;
    activeOverrideAffectsControlBasis_ = false;
    revision_ = 0;

    NUMERIC::AdvanceNonZero(epoch_);
    nextTokenValue_ = 1;
    nextInsertionOrder_ = 1;
}

void CameraDirector::SetBaseCamera(SceneObjectId cameraObjectId) {
    if (!(baseCameraObjectId_ == cameraObjectId)) {
        baseCameraObjectId_ = cameraObjectId;
        // ベースカメラの変更自体はブレンド設定を持たないため、Cut として扱う。
        activeBlend_ = {};
    }
}

SceneObjectId CameraDirector::GetBaseCamera() const noexcept { return baseCameraObjectId_; }

bool CameraDirector::IsWinningOverride(CameraOverrideToken token) const noexcept {
    const auto* winner = FindWinningOverride();
    return winner && winner->token.value == token.value && winner->token.epoch == token.epoch;
}

CameraOverrideToken CameraDirector::PushOverride(const CameraActivationRequest& request) {
    CameraOverrideToken token{};
    token.value = nextTokenValue_++;
    if (nextTokenValue_ == 0) {
        nextTokenValue_ = 1;
    }
    token.epoch = epoch_;

    OverrideEntry entry{};
    entry.token = token;
    entry.request = request;
    entry.insertionOrder = nextInsertionOrder_++;
    if (nextInsertionOrder_ == 0) {
        nextInsertionOrder_ = 1;
    }
    overrides_.push_back(entry);
    return token;
}

bool CameraDirector::ReleaseOverride(CameraOverrideToken token) {
    if (!token.IsValid() || token.epoch != epoch_) {
        return false;
    }

    const OverrideEntry* winning = FindWinningOverride();
    const bool releasingWinner =
        winning != nullptr && winning->token.value == token.value && winning->token.epoch == token.epoch;

    const auto found = std::find_if(overrides_.begin(), overrides_.end(), [token](const OverrideEntry& entry) {
        return entry.token.value == token.value && entry.token.epoch == token.epoch;
    });
    if (found == overrides_.end()) {
        return false;
    }

    if (releasingWinner) {
        // Override 解除後にベースへ戻る場合は、解除対象のブレンド設定を使う。
        activeBlend_ = found->request.returnBlend.value_or(found->request.blend);
        blendActive_ = false;
        blendElapsedSeconds_ = 0.0f;
    }
    overrides_.erase(found);
    return true;
}

bool CameraDirector::SetOverrideCamera(CameraOverrideToken token, const Camera3D& camera) {
    if (!token.IsValid() || token.epoch != epoch_) {
        return false;
    }
    const auto found = std::find_if(overrides_.begin(), overrides_.end(), [token](const OverrideEntry& entry) {
        return entry.token.value == token.value && entry.token.epoch == token.epoch;
    });
    if (found == overrides_.end()) {
        return false;
    }
    found->cameraOverride = camera;
    found->hasCameraOverride = true;
    return true;
}

bool CameraDirector::HasActiveOverride() const noexcept { return !overrides_.empty(); }

const RENDER3D::ResolvedCameraFrame& CameraDirector::Resolve(const World& world, const Camera3D& fallbackCamera,
                                                             float aspect, float deltaTime) {
    resolvedFrame_.cameraCut = false;
    resolvedFrame_.projectionChanged = false;
    resolvedFrame_.continuousCameraSource = false;

    const float resolvedAspect = ClampAspect(aspect, fallbackCamera);
    const OverrideEntry* winningOverride = FindWinningOverride();
    SceneObjectId gameplaySource = baseCameraObjectId_;
    const CAMERA::CameraRigService* rigService = world.Services().Find<CAMERA::CameraRigService>();
    if (!IsCameraObjectEnabled(world, gameplaySource) && rigService != nullptr) {
        (void)rigService->TryResolveFallbackCamera(gameplaySource);
    }
    const SceneObjectId requestedSource =
        winningOverride != nullptr ? winningOverride->request.cameraObjectId : gameplaySource;
    const CameraOverrideToken requestedOverrideToken =
        winningOverride != nullptr ? winningOverride->token : CameraOverrideToken{};

    Camera3D targetCamera{};
    bool requestedSourceValid = TryResolveCameraObject(world, requestedSource, resolvedAspect, targetCamera);
    if (winningOverride != nullptr && winningOverride->hasCameraOverride) {
        requestedSourceValid = true;
        targetCamera = winningOverride->cameraOverride;
        targetCamera.SetAspect(resolvedAspect);
    }
    if (!requestedSourceValid) {
        targetCamera = fallbackCamera;
        targetCamera.SetAspect(resolvedAspect);
    }

    float requestedModifierWeightScale = 1.0f;
    if (requestedSourceValid && rigService != nullptr) {
        CAMERA::CameraRigSubmission resolvedPose{};
        if (rigService->TryResolvePose(requestedSource, resolvedPose)) {
            requestedModifierWeightScale = resolvedPose.pose.modifierWeightScale;
        }
    }
    if (requestedSourceValid && rigService != nullptr) {
        rigService->ApplyModifiers(requestedSource, winningOverride != nullptr, targetCamera,
                                   requestedModifierWeightScale);
    }

    const SceneObjectId resolvedSource = requestedSourceValid ? requestedSource : SceneObjectId{};
    const bool sourceChanged = !resolvedFrame_.valid || !(activeSourceCameraObjectId_ == requestedSource) ||
                               activeOverrideToken_.value != requestedOverrideToken.value ||
                               activeOverrideToken_.epoch != requestedOverrideToken.epoch ||
                               resolvedFrame_.sourceCameraObjectId != resolvedSource.value;

    if (sourceChanged) {
        CameraBlendDesc transitionBlend{};
        if (requestedSourceValid && winningOverride != nullptr) {
            transitionBlend = winningOverride->request.blend;
        } else if (requestedSourceValid && winningOverride == nullptr) {
            transitionBlend = activeBlend_;
        }
        if (!requestedSourceValid) {
            transitionBlend = {};
        }

        activeSourceCameraObjectId_ = requestedSource;
        activeOverrideToken_ = requestedOverrideToken;
        const auto* origin = winningOverride && winningOverride->request.continuityOrigin
                                 ? &*winningOverride->request.continuityOrigin
                                 : nullptr;
        BeginSourceTransition(resolvedSource, targetCamera, transitionBlend, origin);
    }

    if (blendActive_) {
        AdvanceBlend(targetCamera, deltaTime);
    } else if (!sourceChanged) {
        resolvedFrame_.projectionChanged = ProjectionJumps(resolvedFrame_.camera, targetCamera);
        resolvedFrame_.camera = targetCamera;
    }

    // CameraFollow の boom collision 後に project modifier と Director
    // blend が加わるため、通常 gameplay camera は最終 pose をもう一度
    // Scene-owned safety boundary に通す。明示 cinematic override は authored
    // camera policy を保持し、phase 1 では対象外とする。
    if (winningOverride == nullptr && resolvedSource.value != 0u) {
        const Camera3D unsafeResolvedCamera = resolvedFrame_.camera;
        CAMERA::GameplayCameraSafetyResult safety{};
        if (CAMERA::ResolveGameplayCameraSafety(world, resolvedSource, resolvedFrame_.camera, &safety)) {
            resolvedFrame_.projectionChanged =
                resolvedFrame_.projectionChanged || ProjectionJumps(unsafeResolvedCamera, resolvedFrame_.camera);
        }
    }

    if (rigService != nullptr) {
        CAMERA::CameraDepthOfFieldSubmission settingsRequest{};
        const bool hasSettingsRequest = rigService->TryResolveDepthOfField(resolvedSource, settingsRequest);
        if (hasSettingsRequest) {
            const CAMERA::DepthOfFieldSettings resolvedSettings = BlendDepthOfFieldSettings(
                resolvedFrame_.camera.GetDepthOfFieldSettings(), settingsRequest.settings, settingsRequest.weight);
            resolvedFrame_.camera.SetDepthOfFieldSettings(resolvedSettings);
        }

        CAMERA::CameraMotionBlurSubmission motionBlurRequest{};
        if (rigService->TryResolveMotionBlur(resolvedSource, motionBlurRequest)) {
            resolvedFrame_.camera.SetMotionBlurSettings(BlendMotionBlurSettings(
                resolvedFrame_.camera.GetMotionBlurSettings(), motionBlurRequest.settings, motionBlurRequest.weight));
        }

        CAMERA::CameraFocusSubmission focusRequest{};
        const bool hasFocusRequest = rigService->TryResolveFocus(resolvedSource, focusRequest);
        const bool focusWins =
            hasFocusRequest && (!hasSettingsRequest || FocusRequestWins(focusRequest, settingsRequest));
        if (focusWins) {
            float requestedFocusDistance = 0.0f;
            if (ResolveFocusDistance(resolvedFrame_.camera, focusRequest, requestedFocusDistance)) {
                CAMERA::DepthOfFieldSettings resolvedSettings = resolvedFrame_.camera.GetDepthOfFieldSettings();
                resolvedSettings.focusDistance =
                    Lerp(resolvedSettings.focusDistance, requestedFocusDistance, focusRequest.weight);
                resolvedFrame_.camera.SetDepthOfFieldSettings(resolvedSettings);
            }
        }
    }

    resolvedFrame_.sourceCameraObjectId = resolvedSource.value;
    resolvedFrame_.revision = revision_;
    resolvedFrame_.valid = true;

    Camera3D baseControlCamera{};
    const bool baseControlValid = TryResolveCameraObject(world, gameplaySource, resolvedAspect, baseControlCamera);
    if (!baseControlValid) {
        baseControlCamera = fallbackCamera;
        baseControlCamera.SetAspect(resolvedAspect);
    } else if (rigService != nullptr) {
        CAMERA::CameraRigSubmission basePose{};
        float baseModifierWeightScale = 1.0f;
        if (rigService->TryResolvePose(gameplaySource, basePose)) {
            baseModifierWeightScale = basePose.pose.modifierWeightScale;
        }
        rigService->ApplyModifiers(gameplaySource, false, baseControlCamera, baseModifierWeightScale);
    }

    activeOverrideAffectsControlBasis_ =
        winningOverride != nullptr && requestedSourceValid && winningOverride->request.affectsControlBasis;
    controlCamera_ = activeOverrideAffectsControlBasis_ ? resolvedFrame_.camera : baseControlCamera;

    return resolvedFrame_;
}

const RENDER3D::ResolvedCameraFrame& CameraDirector::GetResolvedFrame() const noexcept { return resolvedFrame_; }

const Camera3D& CameraDirector::GetControlCamera() const noexcept { return controlCamera_; }

CameraDirectorStatus CameraDirector::GetStatus() const noexcept {
    CameraDirectorStatus status{};
    status.baseCameraObjectId = baseCameraObjectId_;
    status.activeSourceCameraObjectId = activeSourceCameraObjectId_;
    status.overrideCount = overrides_.size();
    status.blending = blendActive_;
    status.activeOverrideAffectsControlBasis = activeOverrideAffectsControlBasis_;
    return status;
}

const CameraDirector::OverrideEntry* CameraDirector::FindWinningOverride() const {
    const OverrideEntry* winner = nullptr;
    for (const OverrideEntry& entry : overrides_) {
        if (winner == nullptr || entry.request.priority > winner->request.priority ||
            (entry.request.priority == winner->request.priority && entry.insertionOrder > winner->insertionOrder)) {
            winner = &entry;
        }
    }
    return winner;
}

bool CameraDirector::TryResolveCameraObject(const World& world, SceneObjectId cameraObjectId, float aspect,
                                            Camera3D& outCamera) const {
    if (cameraObjectId.value == 0) {
        return false;
    }

    const GameObject* cameraObject = world.FindObject(cameraObjectId);
    if (cameraObject == nullptr) {
        return false;
    }

    const CameraComponent* component = cameraObject->GetComponent<CameraComponent>();
    if (component == nullptr || !component->IsEnabled()) {
        return false;
    }

    MATH::Vec3 eye{};
    MATH::Vec3 target{};
    MATH::Vec3 up{};
    float verticalFov = component->GetFovYRad();
    float nearClip = component->GetNearClip(), farClip = component->GetFarClip();

    CAMERA::CameraRigSubmission rigSubmission{};
    const CAMERA::CameraRigService* rigService = world.Services().Find<CAMERA::CameraRigService>();
    if (rigService != nullptr && rigService->TryResolvePose(cameraObjectId, rigSubmission)) {
        eye = rigSubmission.pose.eye;
        target = rigSubmission.pose.target;
        MATH::Vec3 forward = MATH::Normalize(target - eye);
        up = SafeUpForForward(forward, rigSubmission.pose.up);
        if (rigSubmission.pose.overrideClipPlanes) {
            nearClip = rigSubmission.pose.nearClip;
            farClip = rigSubmission.pose.farClip;
        }
        if (rigSubmission.pose.overrideVerticalFov) {
            verticalFov = rigSubmission.pose.verticalFovRadians;
        }
    } else {
        const MATH::Mat4 worldMatrix = cameraObject->GetTransform().GetWorldMatrix();
        eye = ExtractAxis(worldMatrix, 3);
        MATH::Vec3 forward = MATH::Normalize(ExtractAxis(worldMatrix, 2));
        if (MATH::Length(forward) <= kVectorEpsilon) {
            forward = {0.0f, 0.0f, 1.0f};
        }
        target = eye + forward;
        up = SafeUpForForward(forward, ExtractAxis(worldMatrix, 1));
    }

    outCamera.SetPerspective(verticalFov, (std::max)(aspect, kMinAspect), nearClip, farClip);
    if (component->IsOrthographic()) {
        outCamera.SetOrthographic(component->GetOrthographicHeight(), aspect, component->GetNearClip(),
                                  component->GetFarClip());
        const float grid = component->GetPixelGridSize();
        if (grid > 0) {
            const auto basis = MATH::BuildViewBasisRH(target - eye, up);
            const auto offset =
                basis.right * (std::round(MATH::Dot(eye, basis.right) / grid) * grid - MATH::Dot(eye, basis.right)) +
                basis.up * (std::round(MATH::Dot(eye, basis.up) / grid) * grid - MATH::Dot(eye, basis.up));
            eye = eye + offset;
            target = target + offset;
        }
    }
    outCamera.SetLookAt(eye, target, up);
    outCamera.SetDepthOfFieldSettings(component->GetDepthOfFieldSettings());
    outCamera.SetMotionBlurSettings(component->GetMotionBlurSettings());
    return true;
}

void CameraDirector::BeginSourceTransition(SceneObjectId sourceCameraObjectId, const Camera3D& targetCamera,
                                           const CameraBlendDesc& blend, const Camera3D* continuityOrigin) {
    const bool hadResolvedCamera = resolvedFrame_.valid;
    const bool projectionChanged = !hadResolvedCamera || ProjectionDiffers(resolvedFrame_.camera, targetCamera);

    NUMERIC::AdvanceNonZero(revision_);

    activeBlend_ = blend;
    if (!std::isfinite(activeBlend_.durationSeconds)) {
        activeBlend_.durationSeconds = 0.0f;
    }
    activeBlend_.durationSeconds = (std::max)(activeBlend_.durationSeconds, 0.0f);
    blendElapsedSeconds_ = 0.0f;
    blendStartCamera_ = hadResolvedCamera ? resolvedFrame_.camera : targetCamera;

    const bool canBlend =
        hadResolvedCamera && activeBlend_.mode == CameraBlendMode::EaseInOut && activeBlend_.durationSeconds > 0.0f;
    blendActive_ = canBlend;

    resolvedFrame_.sourceCameraObjectId = sourceCameraObjectId.value;
    resolvedFrame_.projectionChanged = projectionChanged;
    const bool originMatches = continuityOrigin && !ProjectionDiffers(resolvedFrame_.camera, *continuityOrigin) &&
                               ViewMatches(resolvedFrame_.camera, *continuityOrigin);
    const bool sameView =
        hadResolvedCamera && !projectionChanged && (ViewMatches(resolvedFrame_.camera, targetCamera) || originMatches);
    resolvedFrame_.cameraCut = !canBlend && !sameView;
    resolvedFrame_.continuousCameraSource = !projectionChanged && (canBlend || sameView);
    resolvedFrame_.camera = canBlend ? blendStartCamera_ : targetCamera;
    resolvedFrame_.revision = revision_;
    resolvedFrame_.valid = true;
}

void CameraDirector::AdvanceBlend(const Camera3D& targetCamera, float deltaTime) {
    if (!blendActive_) {
        return;
    }

    const float safeDeltaTime = std::isfinite(deltaTime) ? (std::max)(deltaTime, 0.0f) : 0.0f;
    blendElapsedSeconds_ += safeDeltaTime;
    const float normalizedTime =
        activeBlend_.durationSeconds > 0.0f ? blendElapsedSeconds_ / activeBlend_.durationSeconds : 1.0f;
    const float blendAmount = MATH::SmoothStep01(normalizedTime);
    resolvedFrame_.camera = BlendViews(blendStartCamera_, targetCamera, blendAmount);

    if (normalizedTime >= 1.0f) {
        resolvedFrame_.camera = targetCamera;
        blendActive_ = false;
    }
}

} // namespace HIKARI
