#include "HIKARI_CameraDirector.h"

#include <algorithm>
#include <cmath>

#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
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
            return std::isfinite(fallbackAspect) && fallbackAspect >= kMinAspect
                ? fallbackAspect
                : 16.0f / 9.0f;
        }

        float SmoothStep(float value) {
            const float t = std::clamp(value, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float Lerp(float from, float to, float amount) {
            return from + (to - from) * amount;
        }

        MATH::Vec3 Lerp(const MATH::Vec3& from, const MATH::Vec3& to, float amount) {
            return from + (to - from) * amount;
        }

        bool NearlyEqual(float lhs, float rhs) {
            return std::abs(lhs - rhs) <= kFloatEpsilon;
        }

        bool ProjectionDiffers(const Camera3D& lhs, const Camera3D& rhs) {
            return !NearlyEqual(lhs.GetFovYRad(), rhs.GetFovYRad()) ||
                !NearlyEqual(lhs.GetAspect(), rhs.GetAspect()) ||
                !NearlyEqual(lhs.GetNearZ(), rhs.GetNearZ()) ||
                !NearlyEqual(lhs.GetFarZ(), rhs.GetFarZ());
        }

        bool IsCameraObjectEnabled(
            const World& world,
            SceneObjectId cameraObjectId) noexcept {

            const GameObject* object = world.FindObject(cameraObjectId);
            const CameraComponent* camera = object != nullptr
                ? object->GetComponent<CameraComponent>()
                : nullptr;
            return camera != nullptr && camera->IsEnabled();
        }

        MATH::Vec3 SafeUpForForward(const MATH::Vec3& forward, MATH::Vec3 up);

        Camera3D BlendCamera(const Camera3D& from, const Camera3D& to, float amount) {
            Camera3D result{};
            const MATH::Vec3 eye = Lerp(from.GetPosition(), to.GetPosition(), amount);
            MATH::Vec3 target = Lerp(from.GetTarget(), to.GetTarget(), amount);
            MATH::Vec3 up = MATH::Normalize(Lerp(from.GetUp(), to.GetUp(), amount));
            if (MATH::Length(target - eye) <= kVectorEpsilon) {
                target = eye + MATH::Vec3{ 0.0f, 0.0f, 1.0f };
            }
            if (MATH::Length(up) <= kVectorEpsilon) {
                up = { 0.0f, 1.0f, 0.0f };
            }
            up = SafeUpForForward(MATH::Normalize(target - eye), up);

            result.SetPerspective(
                Lerp(from.GetFovYRad(), to.GetFovYRad(), amount),
                Lerp(from.GetAspect(), to.GetAspect(), amount),
                Lerp(from.GetNearZ(), to.GetNearZ(), amount),
                Lerp(from.GetFarZ(), to.GetFarZ(), amount));
            result.SetLookAt(eye, target, up);
            return result;
        }

        MATH::Vec3 ExtractAxis(const MATH::Mat4& matrix, int column) {
            return {
                matrix.m[column][0],
                matrix.m[column][1],
                matrix.m[column][2]
            };
        }

        MATH::Vec3 SafeUpForForward(const MATH::Vec3& forward, MATH::Vec3 up) {
            up = MATH::Normalize(up);
            if (MATH::Length(up) <= kVectorEpsilon ||
                std::abs(MATH::Dot(forward, up)) >= 0.999f) {
                up = { 0.0f, 1.0f, 0.0f };
            }
            if (std::abs(MATH::Dot(forward, up)) >= 0.999f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            return up;
        }
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

        ++epoch_;
        if (epoch_ == 0) {
            epoch_ = 1;
        }
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

    SceneObjectId CameraDirector::GetBaseCamera() const noexcept {
        return baseCameraObjectId_;
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
        const bool releasingWinner = winning != nullptr &&
            winning->token.value == token.value &&
            winning->token.epoch == token.epoch;

        const auto found = std::find_if(
            overrides_.begin(),
            overrides_.end(),
            [token](const OverrideEntry& entry) {
                return entry.token.value == token.value && entry.token.epoch == token.epoch;
            });
        if (found == overrides_.end()) {
            return false;
        }

        if (releasingWinner) {
            // Override 解除後にベースへ戻る場合は、解除対象のブレンド設定を使う。
            activeBlend_ = found->request.blend;
            blendActive_ = false;
            blendElapsedSeconds_ = 0.0f;
        }
        overrides_.erase(found);
        return true;
    }

    bool CameraDirector::SetOverrideCamera(
        CameraOverrideToken token,
        const Camera3D& camera) {

        if (!token.IsValid() || token.epoch != epoch_) {
            return false;
        }
        const auto found = std::find_if(
            overrides_.begin(),
            overrides_.end(),
            [token](const OverrideEntry& entry) {
                return entry.token.value == token.value &&
                    entry.token.epoch == token.epoch;
            });
        if (found == overrides_.end()) {
            return false;
        }
        found->cameraOverride = camera;
        found->hasCameraOverride = true;
        return true;
    }

    bool CameraDirector::ClearOverrideCamera(CameraOverrideToken token) {
        if (!token.IsValid() || token.epoch != epoch_) {
            return false;
        }
        const auto found = std::find_if(
            overrides_.begin(),
            overrides_.end(),
            [token](const OverrideEntry& entry) {
                return entry.token.value == token.value &&
                    entry.token.epoch == token.epoch;
            });
        if (found == overrides_.end()) {
            return false;
        }
        found->hasCameraOverride = false;
        found->cameraOverride = {};
        return true;
    }

    bool CameraDirector::HasActiveOverride() const noexcept {
        return !overrides_.empty();
    }

    const RENDER3D::ResolvedCameraFrame& CameraDirector::Resolve(
        const World& world,
        const Camera3D& fallbackCamera,
        float aspect,
        float deltaTime) {

        resolvedFrame_.cameraCut = false;
        resolvedFrame_.projectionChanged = false;

        const float resolvedAspect = ClampAspect(aspect, fallbackCamera);
        const OverrideEntry* winningOverride = FindWinningOverride();
        SceneObjectId gameplaySource = baseCameraObjectId_;
        const CAMERA::CameraRigService* rigService =
            world.Services().Find<CAMERA::CameraRigService>();
        if (!IsCameraObjectEnabled(world, gameplaySource) &&
            rigService != nullptr) {
            (void)rigService->TryResolveFallbackCamera(gameplaySource);
        }
        const SceneObjectId requestedSource = winningOverride != nullptr
            ? winningOverride->request.cameraObjectId
            : gameplaySource;
        const CameraOverrideToken requestedOverrideToken =
            winningOverride != nullptr
                ? winningOverride->token
                : CameraOverrideToken{};

        Camera3D targetCamera{};
        const bool requestedSourceValid = TryResolveCameraObject(
            world,
            requestedSource,
            resolvedAspect,
            targetCamera);
        if (requestedSourceValid && winningOverride != nullptr &&
            winningOverride->hasCameraOverride) {
            targetCamera = winningOverride->cameraOverride;
            targetCamera.SetPerspective(
                targetCamera.GetFovYRad(),
                resolvedAspect,
                targetCamera.GetNearZ(),
                targetCamera.GetFarZ());
        }
        if (!requestedSourceValid) {
            targetCamera = fallbackCamera;
            targetCamera.SetPerspective(
                fallbackCamera.GetFovYRad(),
                resolvedAspect,
                fallbackCamera.GetNearZ(),
                fallbackCamera.GetFarZ());
        }

        if (requestedSourceValid && rigService != nullptr) {
            rigService->ApplyModifiers(
                requestedSource,
                winningOverride != nullptr,
                targetCamera);
        }

        const SceneObjectId resolvedSource = requestedSourceValid
            ? requestedSource
            : SceneObjectId{};
        const bool sourceChanged =
            !resolvedFrame_.valid ||
            !(activeSourceCameraObjectId_ == requestedSource) ||
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
            BeginSourceTransition(resolvedSource, targetCamera, transitionBlend);
        }

        if (blendActive_) {
            AdvanceBlend(targetCamera, deltaTime);
        } else if (!sourceChanged) {
            resolvedFrame_.projectionChanged = ProjectionDiffers(resolvedFrame_.camera, targetCamera);
            resolvedFrame_.camera = targetCamera;
        }

        resolvedFrame_.sourceCameraObjectId = resolvedSource.value;
        resolvedFrame_.revision = revision_;
        resolvedFrame_.valid = true;

        Camera3D baseControlCamera{};
        const bool baseControlValid = TryResolveCameraObject(
            world,
            gameplaySource,
            resolvedAspect,
            baseControlCamera);
        if (!baseControlValid) {
            baseControlCamera = fallbackCamera;
            baseControlCamera.SetPerspective(
                fallbackCamera.GetFovYRad(),
                resolvedAspect,
                fallbackCamera.GetNearZ(),
                fallbackCamera.GetFarZ());
        } else if (rigService != nullptr) {
            rigService->ApplyModifiers(
                gameplaySource,
                false,
                baseControlCamera);
        }

        activeOverrideAffectsControlBasis_ =
            winningOverride != nullptr &&
            requestedSourceValid &&
            winningOverride->request.affectsControlBasis;
        controlCamera_ = activeOverrideAffectsControlBasis_
            ? resolvedFrame_.camera
            : baseControlCamera;

        return resolvedFrame_;
    }

    const RENDER3D::ResolvedCameraFrame& CameraDirector::GetResolvedFrame() const noexcept {
        return resolvedFrame_;
    }

    const Camera3D& CameraDirector::GetControlCamera() const noexcept {
        return controlCamera_;
    }

    CameraDirectorStatus CameraDirector::GetStatus() const noexcept {
        CameraDirectorStatus status{};
        status.baseCameraObjectId = baseCameraObjectId_;
        status.activeSourceCameraObjectId = activeSourceCameraObjectId_;
        status.overrideCount = overrides_.size();
        status.blending = blendActive_;
        status.activeOverrideAffectsControlBasis =
            activeOverrideAffectsControlBasis_;
        return status;
    }

    const CameraDirector::OverrideEntry* CameraDirector::FindWinningOverride() const {
        const OverrideEntry* winner = nullptr;
        for (const OverrideEntry& entry : overrides_) {
            if (winner == nullptr ||
                entry.request.priority > winner->request.priority ||
                (entry.request.priority == winner->request.priority &&
                    entry.insertionOrder > winner->insertionOrder)) {
                winner = &entry;
            }
        }
        return winner;
    }

    bool CameraDirector::TryResolveCameraObject(
        const World& world,
        SceneObjectId cameraObjectId,
        float aspect,
        Camera3D& outCamera) const {

        if (cameraObjectId.value == 0) {
            return false;
        }

        const GameObject* cameraObject =
            world.FindObject(cameraObjectId);
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

        CAMERA::CameraRigSubmission rigSubmission{};
        const CAMERA::CameraRigService* rigService =
            world.Services().Find<CAMERA::CameraRigService>();
        if (rigService != nullptr &&
            rigService->TryResolvePose(cameraObjectId, rigSubmission)) {
            eye = rigSubmission.pose.eye;
            target = rigSubmission.pose.target;
            MATH::Vec3 forward = MATH::Normalize(target - eye);
            up = SafeUpForForward(forward, rigSubmission.pose.up);
            if (rigSubmission.pose.overrideVerticalFov) {
                verticalFov = rigSubmission.pose.verticalFovRadians;
            }
        } else {
            const MATH::Mat4 worldMatrix =
                cameraObject->GetTransform().GetWorldMatrix();
            eye = ExtractAxis(worldMatrix, 3);
            MATH::Vec3 forward = MATH::Normalize(
                ExtractAxis(worldMatrix, 2));
            if (MATH::Length(forward) <= kVectorEpsilon) {
                forward = { 0.0f, 0.0f, 1.0f };
            }
            target = eye + forward;
            up = SafeUpForForward(
                forward,
                ExtractAxis(worldMatrix, 1));
        }

        outCamera.SetPerspective(
            verticalFov,
            (std::max)(aspect, kMinAspect),
            component->GetNearClip(),
            component->GetFarClip());
        outCamera.SetLookAt(eye, target, up);
        return true;
    }

    bool CameraDirector::BeginSourceTransition(
        SceneObjectId sourceCameraObjectId,
        const Camera3D& targetCamera,
        const CameraBlendDesc& blend) {

        const bool hadResolvedCamera = resolvedFrame_.valid;
        const bool projectionChanged =
            !hadResolvedCamera || ProjectionDiffers(resolvedFrame_.camera, targetCamera);

        ++revision_;
        if (revision_ == 0) {
            revision_ = 1;
        }

        activeBlend_ = blend;
        if (!std::isfinite(activeBlend_.durationSeconds)) {
            activeBlend_.durationSeconds = 0.0f;
        }
        activeBlend_.durationSeconds = (std::max)(activeBlend_.durationSeconds, 0.0f);
        blendElapsedSeconds_ = 0.0f;
        blendStartCamera_ = hadResolvedCamera ? resolvedFrame_.camera : targetCamera;

        const bool canBlend =
            hadResolvedCamera &&
            activeBlend_.mode == CameraBlendMode::EaseInOut &&
            activeBlend_.durationSeconds > 0.0f;
        blendActive_ = canBlend;

        resolvedFrame_.sourceCameraObjectId = sourceCameraObjectId.value;
        resolvedFrame_.projectionChanged = projectionChanged;
        resolvedFrame_.cameraCut = !canBlend;
        resolvedFrame_.camera = canBlend ? blendStartCamera_ : targetCamera;
        resolvedFrame_.revision = revision_;
        resolvedFrame_.valid = true;
        return canBlend;
    }

    void CameraDirector::AdvanceBlend(const Camera3D& targetCamera, float deltaTime) {
        if (!blendActive_) {
            return;
        }

        const float safeDeltaTime = std::isfinite(deltaTime)
            ? (std::max)(deltaTime, 0.0f)
            : 0.0f;
        blendElapsedSeconds_ += safeDeltaTime;
        const float normalizedTime = activeBlend_.durationSeconds > 0.0f
            ? blendElapsedSeconds_ / activeBlend_.durationSeconds
            : 1.0f;
        const float blendAmount = SmoothStep(normalizedTime);
        resolvedFrame_.camera = BlendCamera(blendStartCamera_, targetCamera, blendAmount);

        if (normalizedTime >= 1.0f) {
            resolvedFrame_.camera = targetCamera;
            blendActive_ = false;
        }
    }

} // namespace HIKARI
