#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {

void UiBindingSourceComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1}, {"targetObjectId", SCENE::SERIALIZATION::WriteSceneObjectReference(targetObjectId_.value)}};
}

void UiBindingSourceComponent::Deserialize(const nlohmann::json& in) {
    targetObjectId_ = {};
    if (const nlohmann::json* target = JSONREAD::FindMember(in, "targetObjectId")) {
        targetObjectId_.value = SCENE::SERIALIZATION::SceneObjectReferenceOr(*target);
    }
}

void UiBindingSourceComponent::BuildInspector(IInspectorBuilder& builder) {
    SceneObjectId target = targetObjectId_;
    const AuthoringFieldDescriptor field{"targetObjectId", TEXT::LocalizeCStr("uibind.sourceTarget"),
                                         "UI descendants read runtime variables from this object."};
    if (builder.SceneObjectIdPicker(field, target)) {
        targetObjectId_ = target;
    }
}

bool UiBindingSourceComponent::TryResolveTarget(RuntimeObjectHandle& outTarget) const noexcept {
    outTarget = {};
    const GameObject* owner = GetOwner();
    const World* world = owner != nullptr ? owner->GetWorld() : nullptr;
    if (world == nullptr || targetObjectId_.value == 0u) {
        return false;
    }
    const GameObject* target = world->FindObject(targetObjectId_);
    if (target == nullptr || target->GetWorld() != world) {
        return false;
    }
    outTarget = target->GetRuntimeHandle();
    return outTarget.IsValid();
}

UiBindingSourceState ResolveInheritedUiBindingSource(const GameObject& object,
                                                     RuntimeObjectHandle& outTarget) noexcept {
    outTarget = {};
    for (const GameObject* current = &object; current != nullptr; current = current->GetParent()) {
        const UiBindingSourceComponent* source = current->GetComponent<UiBindingSourceComponent>();
        if (source == nullptr) {
            continue;
        }
        return source->TryResolveTarget(outTarget) ? UiBindingSourceState::Resolved : UiBindingSourceState::Invalid;
    }
    return UiBindingSourceState::None;
}

} // namespace HIKARI
