#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoring.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoringInternal.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {
namespace VfxPlayerAuthoringInternal {
BINDING::ValueType ToBindingType(VFX::VfxValueType type) noexcept {
    switch (type) {
    case VFX::VfxValueType::Bool: return BINDING::ValueType::Bool;
    case VFX::VfxValueType::Int: return BINDING::ValueType::Int;
    case VFX::VfxValueType::Float2: return BINDING::ValueType::Float2;
    case VFX::VfxValueType::Float3: return BINDING::ValueType::Float3;
    case VFX::VfxValueType::Float4: return BINDING::ValueType::Float4;
    case VFX::VfxValueType::Color: return BINDING::ValueType::Color;
    case VFX::VfxValueType::Float:
    default: return BINDING::ValueType::Float;
    }
}

const VFX::VfxProgram* ResolveProgram(const AssetRegistry* registry, const VfxPlayerSlot& slot) noexcept {
    if (registry == nullptr || !slot.effectAssetGuid.IsValid()) {
        return nullptr;
    }
    const auto* descriptor = registry->FindAs<VfxAssetDescriptor>(AssetId{slot.effectAssetGuid.value});
    return descriptor != nullptr && descriptor->program != nullptr ? descriptor->program.get() : nullptr;
}

const VFX::VfxProgramInput* FindProgramInput(const VFX::VfxProgram* program, std::string_view inputId) noexcept {
    if (program == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(program->inputs.begin(), program->inputs.end(),
                                    [inputId](const VFX::VfxProgramInput& input) { return input.id == inputId; });
    return found == program->inputs.end() ? nullptr : &*found;
}

bool IsFiniteVec3(const MATH::Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

AuthoringCommandResult Error(std::string code, std::string message) {
    return {false, std::move(code), std::move(message), {}};
}

AuthoringCommandResult Ok(std::string message, nlohmann::json data) {
    return {true, "OK", std::move(message), std::move(data)};
}

VfxPlayerComponent* Cast(IComponent& component) noexcept { return dynamic_cast<VfxPlayerComponent*>(&component); }

const VfxPlayerComponent* Cast(const IComponent& component) noexcept {
    return dynamic_cast<const VfxPlayerComponent*>(&component);
}
} // namespace VfxPlayerAuthoringInternal

AuthoringCommandResult ValidateVfxPlayerComponentAuthoring(const IComponent& base, const InspectorContext& context) {
    using VfxPlayerAuthoringInternal::Cast;
    using VfxPlayerAuthoringInternal::Error;
    using VfxPlayerAuthoringInternal::FindProgramInput;
    using VfxPlayerAuthoringInternal::IsFiniteVec3;
    using VfxPlayerAuthoringInternal::Ok;
    using VfxPlayerAuthoringInternal::ToBindingType;

    const VfxPlayerComponent* component = Cast(base);
    if (component == nullptr) {
        return Error("COMPONENT_TYPE_MISMATCH", "VFX authoring validation requires VfxPlayerComponent.");
    }

    const BINDING::RuntimeBindingStore* bindingStore =
        context.worldServices != nullptr ? context.worldServices->Find<BINDING::RuntimeBindingStore>() : nullptr;
    for (const VfxPlayerSlot& slot : component->GetSlots()) {
        if (!IsFiniteVec3(slot.offset.position) || !IsFiniteVec3(slot.offset.rotationEulerDegrees) ||
            !IsFiniteVec3(slot.offset.scale)) {
            return Error("NON_FINITE_VFX_OFFSET", "VFX slot offsets must contain only finite values.");
        }
        const VFX::VfxProgram* program = nullptr;
        if (slot.effectAssetGuid.IsValid()) {
            if (!IsValidAssetGuid(slot.effectAssetGuid.value)) {
                return Error("INVALID_VFX_ASSET_GUID",
                             "VFX slot '" + slot.name + "' contains an invalid effect asset GUID.");
            }
            if (context.assetRegistry == nullptr) {
                return Error("VFX_ASSET_CONTEXT_UNAVAILABLE",
                             "VFX effect references require an active asset registry.");
            }
            const AssetDescriptor* baseDescriptor = context.assetRegistry->FindDescriptor(slot.effectAssetGuid.value);
            if (baseDescriptor == nullptr) {
                return Error("VFX_ASSET_NOT_FOUND", "VFX effect asset was not found: " + slot.effectAssetGuid.value);
            }
            if (baseDescriptor->type != AssetType::VfxEffect) {
                return Error("VFX_ASSET_TYPE_MISMATCH",
                             "Referenced asset is not a VFX effect: " + slot.effectAssetGuid.value);
            }
            const auto* descriptor = dynamic_cast<const VfxAssetDescriptor*>(baseDescriptor);
            if (descriptor == nullptr || descriptor->program == nullptr) {
                return Error("VFX_PROGRAM_UNAVAILABLE",
                             "VFX effect has no compiled program: " + slot.effectAssetGuid.value);
            }
            program = descriptor->program.get();
        } else if (!slot.variableBindings.empty()) {
            return Error("VFX_ASSET_REQUIRED", "VFX input bindings require an effect asset.");
        }

        const bool eventTargetRequired =
            slot.spawnTarget == VfxSpawnTarget::EventSource || slot.spawnTarget == VfxSpawnTarget::EventPayloadPosition;
        if (slot.triggerEventId.empty()) {
            if (eventTargetRequired) {
                return Error("VFX_EVENT_REQUIRED", "The selected VFX spawn target requires a trigger event.");
            }
        } else {
            if (bindingStore == nullptr) {
                return Error("RUNTIME_BINDING_CONTEXT_UNAVAILABLE",
                             "VFX event references require the Runtime Binding catalog.");
            }
            const BINDING::EventDefinition* event = bindingStore->GetCatalog().FindEvent(slot.triggerEventId);
            if (event == nullptr) {
                return Error("RUNTIME_EVENT_NOT_FOUND", "Runtime event was not found: " + slot.triggerEventId);
            }
            if (slot.spawnTarget == VfxSpawnTarget::EventPayloadPosition &&
                (!event->hasPayload || event->payloadType != BINDING::ValueType::Float3)) {
                return Error("RUNTIME_EVENT_PAYLOAD_MISMATCH",
                             "Event payload position requires a Float3 runtime event: " + slot.triggerEventId);
            }
        }

        if (slot.spawnTarget == VfxSpawnTarget::FixedObject) {
            if (slot.fixedObjectId.value == 0u) {
                return Error("VFX_FIXED_OBJECT_REQUIRED",
                             "The selected VFX spawn target requires a fixed Scene object.");
            }
            if (context.sceneDocument == nullptr) {
                return Error("SCENE_OBJECT_CONTEXT_UNAVAILABLE",
                             "VFX fixed-object references require an active Scene document.");
            }
            if (FindSceneObjectData(*context.sceneDocument, slot.fixedObjectId) == nullptr) {
                return Error("SCENE_OBJECT_NOT_FOUND",
                             "VFX fixed Scene object was not found: " + std::to_string(slot.fixedObjectId.value));
            }
        }

        std::vector<std::string_view> validatedInputs{};
        validatedInputs.reserve(slot.variableBindings.size());
        for (const VfxVariableBinding& binding : slot.variableBindings) {
            if (std::find(validatedInputs.begin(), validatedInputs.end(), binding.inputId) != validatedInputs.end()) {
                return Error("DUPLICATE_VFX_INPUT_BINDING",
                             "VFX exposed input is bound more than once: " + binding.inputId);
            }
            validatedInputs.push_back(binding.inputId);
            const VFX::VfxProgramInput* input = FindProgramInput(program, binding.inputId);
            if (input == nullptr) {
                return Error("VFX_INPUT_NOT_FOUND",
                             "VFX exposed input was not found in the compiled program: " + binding.inputId);
            }
            if (binding.variableId.empty()) {
                continue;
            }
            if (bindingStore == nullptr) {
                return Error("RUNTIME_BINDING_CONTEXT_UNAVAILABLE",
                             "VFX variable references require the Runtime Binding catalog.");
            }
            const BINDING::VariableDefinition* variable = bindingStore->GetCatalog().FindVariable(binding.variableId);
            if (variable == nullptr) {
                return Error("RUNTIME_VARIABLE_NOT_FOUND", "Runtime variable was not found: " + binding.variableId);
            }
            if (variable->defaultValue.type != ToBindingType(input->type)) {
                return Error("RUNTIME_VARIABLE_TYPE_MISMATCH",
                             "Runtime variable type does not match VFX input '" + binding.inputId + "'.");
            }
        }
    }
    return Ok("VFX component authoring references are valid.");
}

} // namespace HIKARI
