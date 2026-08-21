#include "HIKARI/Project/PresentationViews.h"

#include <cmath>
#include <string>

#include "Core/HIKARI_TimeService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        template<size_t Capacity>
        bool ReadFixedText(
            const char (&source)[Capacity],
            std::string& outValue) {
            size_t count = 0u;
            while (count < Capacity && source[count] != '\0') {
                ++count;
            }
            if (count == Capacity) {
                outValue.clear();
                return false;
            }
            outValue.assign(source, count);
            return true;
        }

        RuntimeObjectHandle DecodeRuntimeHandle(
            uint64_t value) noexcept {
            if (value == 0u) {
                return {};
            }
            const uint32_t encodedSlot = static_cast<uint32_t>(
                value & 0xFFFFFFFFu);
            const uint32_t generation = static_cast<uint32_t>(
                value >> 32u);
            if (encodedSlot == 0u || generation == 0u) {
                return {};
            }
            RuntimeObjectHandle result{};
            result.slot = encodedSlot - 1u;
            result.generation = generation;
            return result.IsValid() ? result : RuntimeObjectHandle{};
        }

        bool IsFinite(const GameProjectFloat3& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsFinite(const GameProjectQuat& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::isfinite(value.w);
        }

        bool IsValidTransform(
            const GameProjectTransform& transform) noexcept {
            if (!IsFinite(transform.position) ||
                !IsFinite(transform.rotation) ||
                !IsFinite(transform.scale) ||
                transform.scale.x <= 0.0f ||
                transform.scale.y <= 0.0f ||
                transform.scale.z <= 0.0f) {
                return false;
            }
            const float rotationLengthSquared =
                transform.rotation.x * transform.rotation.x +
                transform.rotation.y * transform.rotation.y +
                transform.rotation.z * transform.rotation.z +
                transform.rotation.w * transform.rotation.w;
            return std::isfinite(rotationLengthSquared) &&
                rotationLengthSquared > 1.0e-12f;
        }
    }

    PresentationModelView GameProjectSdkAccess::MakePresentationModelView(
        World& world) noexcept {
        return PresentationModelView(&world);
    }

    bool PresentationModelView::Submit(
        const GameProjectPresentationModelRequest& request) const noexcept {
        try {
            if (request.ownerObjectHandle == 0u ||
                request.instanceId == 0u ||
                request.model.IsEmpty() ||
                request.visible > 1u ||
                request.castShadow > 1u ||
                request.receiveShadow > 1u ||
                request.reserved != 0u ||
                request.attachment.enabled > 1u ||
                request.attachment.reserved[0] != 0u ||
                request.attachment.reserved[1] != 0u ||
                request.attachment.reserved[2] != 0u ||
                !IsValidTransform(request.worldTransform)) {
                return false;
            }

            World* world = GameProjectSdkAccess::GetWorld(*this);
            if (world == nullptr ||
                world->FindObject(
                    DecodeRuntimeHandle(request.ownerObjectHandle)) == nullptr) {
                return false;
            }

            std::string assetGuid{};
            std::string assetPath{};
            std::string materialSetAssetGuid{};
            std::string socketName{};
            if (!ReadFixedText(request.model.assetGuid, assetGuid) ||
                !ReadFixedText(request.model.assetPath, assetPath) ||
                !ReadFixedText(
                    request.materialSetAssetGuid,
                    materialSetAssetGuid) ||
                !ReadFixedText(
                    request.attachment.socketName,
                    socketName) ||
                (assetGuid.empty() && assetPath.empty()) ||
                (request.attachment.enabled != 0u && socketName.empty()) ||
                (request.attachment.enabled == 0u && !socketName.empty()) ||
                (request.attachment.enabled != 0u &&
                    !IsValidTransform(request.attachment.localTransform))) {
                return false;
            }

            PresentationModelService* service = world->Services().Find<
                PresentationModelService>();
            if (service == nullptr) {
                return false;
            }

            PresentationModelSubmission submission{};
            submission.ownerObjectHandle = request.ownerObjectHandle;
            submission.instanceId = request.instanceId;
            submission.assetGuid = std::move(assetGuid);
            submission.assetPath = std::move(assetPath);
            submission.materialSetAssetGuid =
                std::move(materialSetAssetGuid);
            submission.socketName = std::move(socketName);
            submission.attachmentEnabled =
                request.attachment.enabled != 0u;
            submission.worldTransform.position = {
                request.worldTransform.position.x,
                request.worldTransform.position.y,
                request.worldTransform.position.z
            };
            submission.worldTransform.rotation = {
                request.worldTransform.rotation.x,
                request.worldTransform.rotation.y,
                request.worldTransform.rotation.z,
                request.worldTransform.rotation.w
            };
            submission.worldTransform.scale = {
                request.worldTransform.scale.x,
                request.worldTransform.scale.y,
                request.worldTransform.scale.z
            };
            submission.localTransform.position = {
                request.attachment.localTransform.position.x,
                request.attachment.localTransform.position.y,
                request.attachment.localTransform.position.z
            };
            submission.localTransform.rotation = {
                request.attachment.localTransform.rotation.x,
                request.attachment.localTransform.rotation.y,
                request.attachment.localTransform.rotation.z,
                request.attachment.localTransform.rotation.w
            };
            submission.localTransform.scale = {
                request.attachment.localTransform.scale.x,
                request.attachment.localTransform.scale.y,
                request.attachment.localTransform.scale.z
            };
            submission.visible = request.visible != 0u;
            submission.castShadow = request.castShadow != 0u;
            submission.receiveShadow = request.receiveShadow != 0u;
            return service->Submit(
                submission,
                TIME::GetFrameContext().frameIndex);
        }
        catch (...) {
            return false;
        }
    }

    bool PresentationModelView::Remove(
        uint64_t ownerObjectHandle,
        uint64_t instanceId) const noexcept {
        try {
            if (ownerObjectHandle == 0u || instanceId == 0u) {
                return false;
            }
            World* world = GameProjectSdkAccess::GetWorld(*this);
            if (world == nullptr) {
                return false;
            }
            PresentationModelService* service = world->Services().Find<
                PresentationModelService>();
            return service != nullptr &&
                service->Remove(ownerObjectHandle, instanceId);
        }
        catch (...) {
            return false;
        }
    }

} // namespace HIKARI

namespace HIKARI {

    World* GameProjectSdkAccess::GetWorld(
        const PresentationModelView& view) noexcept {
        return static_cast<World*>(view.world_);
    }

} // namespace HIKARI
