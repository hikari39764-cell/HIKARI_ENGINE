#include "Scene/HIKARI_CameraActivationVolumeSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Core/HIKARI_FrameContext.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        MATH::Vec3 ExtractPosition(const GameObject& object) noexcept {
            const MATH::Mat4 world =
                object.GetTransform().GetWorldMatrix();
            return {
                world.m[3][0],
                world.m[3][1],
                world.m[3][2]
            };
        }

        bool ContainsPoint(
            const GameObject& volume,
            const MATH::Vec3& halfExtents,
            const MATH::Vec3& worldPoint) noexcept {

            const MATH::Mat4 inverse = MATH::Inverse(
                volume.GetTransform().GetWorldMatrix());
            const MATH::Vec4 local = inverse.TransformPoint({
                worldPoint.x,
                worldPoint.y,
                worldPoint.z,
                1.0f
            });
            return MATH::IsFinite(local) &&
                std::abs(local.x) <= halfExtents.x &&
                std::abs(local.y) <= halfExtents.y &&
                std::abs(local.z) <= halfExtents.z;
        }

        bool SameRequest(
            const CameraActivationRequest& lhs,
            const CameraActivationRequest& rhs) noexcept {

            return lhs.cameraObjectId == rhs.cameraObjectId &&
                lhs.priority == rhs.priority &&
                lhs.affectsControlBasis == rhs.affectsControlBasis &&
                lhs.blend.mode == rhs.blend.mode &&
                std::abs(
                    lhs.blend.durationSeconds -
                    rhs.blend.durationSeconds) <= 1.0e-5f;
        }
    }

    void CameraActivationVolumeSystem::OnWorldAttached(World& world) {
        director_ = world.Services().Find<CameraDirector>();
        runtimePlayState_ = world.Services().Find<
            RuntimePlayStateService>();
    }

    void CameraActivationVolumeSystem::OnWorldDetached(World&) {
        if (director_ != nullptr) {
            for (const auto& [_, active] : activeRequests_) {
                (void)director_->ReleaseOverride(active.token);
            }
        }
        activeRequests_.clear();
        director_ = nullptr;
        runtimePlayState_ = nullptr;
    }

    void CameraActivationVolumeSystem::LateUpdate(
        World& world,
        const FrameContext&) {

        if (director_ == nullptr) {
            return;
        }
#if defined(HIKARI_WITH_EDITOR)
        if (runtimePlayState_ == nullptr ||
            !runtimePlayState_->IsActive()) {
            return;
        }
#endif

        std::unordered_set<uint64_t> liveVolumes{};
        world.ForEachObjectWith<CameraActivationVolumeComponent>(
            [this, &world, &liveVolumes](
                GameObject& volume,
                CameraActivationVolumeComponent& component) {
                const uint64_t volumeRuntimeId =
                    volume.GetRuntimeHandle().ToValue();
                if (volumeRuntimeId == 0u) {
                    return;
                }
                liveVolumes.insert(volumeRuntimeId);

                GameObject* subject = world.FindObject(
                    component.GetSubjectObjectId());
                GameObject* camera = world.FindObject(
                    component.GetCameraObjectId());
                const CameraComponent* cameraComponent = camera != nullptr
                    ? camera->GetComponent<CameraComponent>()
                    : nullptr;
                const bool valid = component.IsEnabled() &&
                    subject != nullptr && cameraComponent != nullptr &&
                    cameraComponent->IsEnabled();
                const bool active = valid && ContainsPoint(
                    volume,
                    component.GetHalfExtents(),
                    ExtractPosition(*subject));
                if (!active) {
                    ReleaseRequest(volumeRuntimeId);
                    return;
                }

                CameraActivationRequest request{};
                request.cameraObjectId = component.GetCameraObjectId();
                request.priority = component.GetPriority();
                request.affectsControlBasis =
                    component.GetAffectsControlBasis();
                request.blend.mode = component.GetBlendSeconds() > 0.0f
                    ? CameraBlendMode::EaseInOut
                    : CameraBlendMode::Cut;
                request.blend.durationSeconds =
                    component.GetBlendSeconds();

                const auto existing = activeRequests_.find(
                    volumeRuntimeId);
                if (existing != activeRequests_.end() &&
                    SameRequest(existing->second.request, request)) {
                    return;
                }
                ReleaseRequest(volumeRuntimeId);
                const CameraOverrideToken token =
                    director_->PushOverride(request);
                if (token.IsValid()) {
                    activeRequests_[volumeRuntimeId] = {
                        token,
                        request
                    };
                }
            });

        for (auto it = activeRequests_.begin();
            it != activeRequests_.end();) {
            if (liveVolumes.contains(it->first)) {
                ++it;
                continue;
            }
            (void)director_->ReleaseOverride(it->second.token);
            it = activeRequests_.erase(it);
        }
    }

    void CameraActivationVolumeSystem::ReleaseRequest(
        uint64_t volumeRuntimeId) noexcept {

        const auto found = activeRequests_.find(volumeRuntimeId);
        if (found == activeRequests_.end()) {
            return;
        }
        if (director_ != nullptr) {
            (void)director_->ReleaseOverride(found->second.token);
        }
        activeRequests_.erase(found);
    }

} // namespace HIKARI
