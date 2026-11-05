#include "Scene/Vfx/HIKARI_VfxAttachmentResolver.h"

#include <memory>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/Runtime/HIKARI_AnimationSocketService.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::VFX {

bool ResolveVfxAnimationAttachment(const World& world, RuntimeObjectHandle owner, std::string_view socketName,
                                   VfxWorldTransform& outTransform) {
    if (!owner.IsValid()) {
        return false;
    }
    const GameObject* object = world.FindObject(owner);
    if (object == nullptr) {
        return false;
    }
    if (socketName.empty()) {
        const Transform3D& transform = object->GetTransform();
        if (MATH::DecomposeTRS(transform.GetWorldMatrix(), outTransform.position, outTransform.rotation,
                               outTransform.scale)) {
            return true;
        }
        outTransform.position = transform.position;
        outTransform.rotation = transform.rotation;
        outTransform.scale = transform.scale;
        return true;
    }

    const ModelComponent* modelComponent = object->GetComponent<ModelComponent>();
    const ModelAsset* model = modelComponent != nullptr ? modelComponent->GetModelAsset() : nullptr;
    const ANIMATION::AnimationSocketService* sockets = world.Services().Find<ANIMATION::AnimationSocketService>();
    if (model == nullptr || sockets == nullptr) {
        return false;
    }
    const ANIMATION::AnimationPoseService* poses = world.Services().Find<ANIMATION::AnimationPoseService>();
    const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot> pose =
        poses != nullptr ? poses->Find(owner) : nullptr;
    Transform3D socketTransform{};
    if (!sockets->Resolve(*model, pose.get(), object->GetTransform().GetWorldMatrix(), socketName,
                          ANIMATION::AnimationSocketSpace::World, socketTransform)) {
        return false;
    }
    return MATH::DecomposeTRS(socketTransform.GetWorldMatrix(), outTransform.position, outTransform.rotation,
                              outTransform.scale);
}

void ConfigureVfxWorldAttachmentResolver(World& world, VfxPlaybackService& playback) {
    playback.SetAttachmentResolver(
        [&world](RuntimeObjectHandle owner, std::string_view socketName, VfxWorldTransform& outTransform) {
            return ResolveVfxAnimationAttachment(world, owner, socketName, outTransform);
        });
}

} // namespace HIKARI::VFX
