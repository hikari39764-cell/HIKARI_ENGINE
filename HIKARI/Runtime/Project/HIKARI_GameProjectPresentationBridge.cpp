#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectFixedText.h"
#include "HIKARI/Project/PresentationViews.h"
#include "HIKARI/Project/WorldView.h"

#include <cmath>
#include <string>
#include <utility>

#include "Core/HIKARI_TimeService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {
using PROJECT::INTERNAL::IsFinite;

bool IsValidTransform(const GameProjectTransform& transform) noexcept {
    if (!IsFinite(transform.position) || !IsFinite(transform.rotation) || !IsFinite(transform.scale) ||
        transform.scale.x <= 0.0f || transform.scale.y <= 0.0f || transform.scale.z <= 0.0f) {
        return false;
    }
    const float rotationLengthSquared =
        transform.rotation.x * transform.rotation.x + transform.rotation.y * transform.rotation.y +
        transform.rotation.z * transform.rotation.z + transform.rotation.w * transform.rotation.w;
    return std::isfinite(rotationLengthSquared) && rotationLengthSquared > 1.0e-12f;
}

bool SubmitPresentationModelRequest(World* world, const GameProjectPresentationModelRequest& request,
                                    std::string_view exclusiveNodeName) noexcept {
    try {
        if (request.ownerObjectHandle == 0u || request.instanceId == 0u || request.model.IsEmpty() ||
            request.visible > 1u || request.castShadow > 1u || request.receiveShadow > 1u || request.reserved != 0u ||
            request.attachment.enabled > 1u || NUMERIC::AnyNonZero(request.attachment.reserved) ||
            !IsValidTransform(request.worldTransform) ||
            (!exclusiveNodeName.empty() && exclusiveNodeName.size() > kGameProjectModelNodeNameMaximumBytes)) {
            return false;
        }

        if (world == nullptr ||
            world->FindObject(RuntimeObjectHandle::FromValue(request.ownerObjectHandle)) == nullptr) {
            return false;
        }

        std::string assetGuid{};
        std::string assetPath{};
        std::string materialSetAssetGuid{};
        std::string socketName{};
        if (!PROJECT::DETAIL::ReadFixedText(request.model.assetGuid, assetGuid) ||
            !PROJECT::DETAIL::ReadFixedText(request.model.assetPath, assetPath) ||
            !PROJECT::DETAIL::ReadFixedText(request.materialSetAssetGuid, materialSetAssetGuid) ||
            !PROJECT::DETAIL::ReadFixedText(request.attachment.socketName, socketName) ||
            (assetGuid.empty() && assetPath.empty()) || (request.attachment.enabled != 0u && socketName.empty()) ||
            (request.attachment.enabled == 0u && !socketName.empty()) ||
            (request.attachment.enabled != 0u && !IsValidTransform(request.attachment.localTransform))) {
            return false;
        }

        PresentationModelService* service = world->Services().Find<PresentationModelService>();
        if (service == nullptr) {
            return false;
        }

        PresentationModelSubmission submission{};
        submission.ownerObjectHandle = request.ownerObjectHandle;
        submission.instanceId = request.instanceId;
        submission.assetGuid = std::move(assetGuid);
        submission.assetPath = std::move(assetPath);
        submission.materialSetAssetGuid = std::move(materialSetAssetGuid);
        submission.socketName = std::move(socketName);
        if (!exclusiveNodeName.empty()) {
            submission.exclusiveNodeName.assign(exclusiveNodeName.data(), exclusiveNodeName.size());
        }
        submission.attachmentEnabled = request.attachment.enabled != 0u;
        submission.worldTransform.position = {request.worldTransform.position.x, request.worldTransform.position.y,
                                              request.worldTransform.position.z};
        submission.worldTransform.rotation = {request.worldTransform.rotation.x, request.worldTransform.rotation.y,
                                              request.worldTransform.rotation.z, request.worldTransform.rotation.w};
        submission.worldTransform.scale = {request.worldTransform.scale.x, request.worldTransform.scale.y,
                                           request.worldTransform.scale.z};
        submission.localTransform.position = {request.attachment.localTransform.position.x,
                                              request.attachment.localTransform.position.y,
                                              request.attachment.localTransform.position.z};
        submission.localTransform.rotation = {
            request.attachment.localTransform.rotation.x, request.attachment.localTransform.rotation.y,
            request.attachment.localTransform.rotation.z, request.attachment.localTransform.rotation.w};
        submission.localTransform.scale = {request.attachment.localTransform.scale.x,
                                           request.attachment.localTransform.scale.y,
                                           request.attachment.localTransform.scale.z};
        submission.visible = request.visible != 0u;
        submission.castShadow = request.castShadow != 0u;
        submission.receiveShadow = request.receiveShadow != 0u;
        return service->Submit(submission, TIME::GetFrameContext().frameIndex);
    } catch (...) {
        return false;
    }
}
} // namespace

PresentationModelView GameProjectSdkAccess::MakePresentationModelView(World& world) noexcept {
    return PresentationModelView(GameProjectWorldHandle{world.GetRuntimeIdentity()});
}

bool PresentationModelView::IsValid() const noexcept { return GameProjectSdkAccess::GetWorld(*this) != nullptr; }

PresentationModelView WorldView::GetPresentationModels() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakePresentationModelView(*world) : PresentationModelView{};
}

bool PresentationModelView::Submit(const GameProjectPresentationModelRequest& request) const noexcept {
    return SubmitPresentationModelRequest(GameProjectSdkAccess::GetWorld(*this), request, {});
}

bool PresentationModelView::SubmitExclusiveNode(const GameProjectPresentationModelRequest& request,
                                                std::string_view nodeName) const noexcept {
    if (nodeName.empty()) {
        return false;
    }
    return SubmitPresentationModelRequest(GameProjectSdkAccess::GetWorld(*this), request, nodeName);
}

bool PresentationModelView::Remove(uint64_t ownerObjectHandle, uint64_t instanceId) const noexcept {
    try {
        if (ownerObjectHandle == 0u || instanceId == 0u) {
            return false;
        }
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr) {
            return false;
        }
        PresentationModelService* service = world->Services().Find<PresentationModelService>();
        return service != nullptr && service->Remove(ownerObjectHandle, instanceId);
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI

namespace HIKARI {

World* GameProjectSdkAccess::GetWorld(const PresentationModelView& view) noexcept {
    return World::ResolveRuntimeWorld(view.world_.value);
}

} // namespace HIKARI
