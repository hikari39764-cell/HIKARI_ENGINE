#include "Scene/Vfx/HIKARI_VfxBindingResolver.h"

#include <utility>

namespace HIKARI::VFX {
namespace {
VfxValue ConvertValue(const BINDING::Value& source) noexcept {
    VfxValue value{};
    switch (source.type) {
    case BINDING::ValueType::Bool: value.type = VfxValueType::Bool; break;
    case BINDING::ValueType::Int: value.type = VfxValueType::Int; break;
    case BINDING::ValueType::Float2: value.type = VfxValueType::Float2; break;
    case BINDING::ValueType::Float3: value.type = VfxValueType::Float3; break;
    case BINDING::ValueType::Float4: value.type = VfxValueType::Float4; break;
    case BINDING::ValueType::Color: value.type = VfxValueType::Color; break;
    case BINDING::ValueType::Float:
    default: value.type = VfxValueType::Float; break;
    }
    value.boolean = source.boolean;
    value.integer = source.integer;
    value.numeric = {source.numeric.x, source.numeric.y, source.numeric.z, source.numeric.w};
    return value;
}

VfxWorldTransform ComposeOffset(const VfxWorldTransform& base, const VfxTransformData& offset) {
    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const MATH::Quat offsetRotation = MATH::Quat::FromEulerXYZ(offset.rotationEulerDegrees.x * kDegreesToRadians,
                                                               offset.rotationEulerDegrees.y * kDegreesToRadians,
                                                               offset.rotationEulerDegrees.z * kDegreesToRadians);
    const MATH::Mat4 matrix = MATH::Mat4::TRS(base.position, base.rotation, base.scale) *
                              MATH::Mat4::TRS(offset.position, offsetRotation, offset.scale);
    VfxWorldTransform result{};
    if (!MATH::DecomposeTRS(matrix, result.position, result.rotation, result.scale)) {
        result = base;
        result.position = base.position + offset.position;
    }
    return result;
}

bool ResolveBaseTransform(const VfxResolvedTarget& target, const VfxBindingResolveContext& context,
                          VfxWorldTransform& outTransform) {
    switch (target.kind) {
    case VfxSpawnTarget::Owner:
        if (target.runtimeObject == context.ownerObject) {
            outTransform = context.ownerTransform;
            return true;
        }
        return context.resolveRuntimeObject && context.resolveRuntimeObject(target.runtimeObject, outTransform);
    case VfxSpawnTarget::EventSource:
        return target.runtimeObject.IsValid() && context.resolveRuntimeObject &&
               context.resolveRuntimeObject(target.runtimeObject, outTransform);
    case VfxSpawnTarget::FixedObject:
        return target.sceneObject.value != 0u && context.resolveSceneObject &&
               context.resolveSceneObject(target.sceneObject, outTransform);
    case VfxSpawnTarget::EventPayloadPosition: outTransform = target.baseTransform; return true;
    default: return false;
    }
}
} // namespace

bool BuildVfxPlayRequest(const VfxPlayerSlot& slot, const VfxBindingResolveContext& context, VfxPlayRequest& outRequest,
                         VfxResolvedTarget& outTarget, std::vector<VfxPlaybackDiagnostic>& outDiagnostics) {
    if (!slot.effectAssetGuid.IsValid()) {
        outDiagnostics.push_back({VfxDiagnosticSeverity::Error, "Vfx.MissingAsset", "VFX slot has no effect asset"});
        return false;
    }
    VfxResolvedTarget target{};
    target.kind = slot.spawnTarget;
    switch (slot.spawnTarget) {
    case VfxSpawnTarget::Owner:
        target.runtimeObject = context.ownerObject;
        target.baseTransform = context.ownerTransform;
        break;
    case VfxSpawnTarget::EventSource:
        if (context.event == nullptr || !context.event->sourceObject.IsValid() || !context.resolveRuntimeObject ||
            !context.resolveRuntimeObject(context.event->sourceObject, target.baseTransform)) {
            outDiagnostics.push_back({VfxDiagnosticSeverity::Error, "Vfx.EventSourceUnavailable",
                                      "VFX event source object could not be resolved"});
            return false;
        }
        target.runtimeObject = context.event->sourceObject;
        break;
    case VfxSpawnTarget::FixedObject:
        if (slot.fixedObjectId.value == 0u || !context.resolveSceneObject ||
            !context.resolveSceneObject(slot.fixedObjectId, target.baseTransform)) {
            outDiagnostics.push_back({VfxDiagnosticSeverity::Error, "Vfx.FixedObjectUnavailable",
                                      "VFX fixed Scene object could not be resolved"});
            return false;
        }
        target.sceneObject = slot.fixedObjectId;
        break;
    case VfxSpawnTarget::EventPayloadPosition:
        if (context.event == nullptr || !context.event->hasPayload ||
            context.event->payload.type != BINDING::ValueType::Float3) {
            outDiagnostics.push_back({VfxDiagnosticSeverity::Error, "Vfx.PositionPayloadRequired",
                                      "VFX payload position requires a Float3 event payload"});
            return false;
        }
        target.baseTransform.position = {context.event->payload.numeric.x, context.event->payload.numeric.y,
                                         context.event->payload.numeric.z};
        break;
    }

    VfxPlayRequest request{};
    request.assetGuid = slot.effectAssetGuid;
    request.ownerObject = context.ownerObject;
    request.transform = ComposeOffset(target.baseTransform, slot.offset);
    switch (slot.loopMode) {
    case VfxLoopMode::ForceLoop: request.loopOverride = true; break;
    case VfxLoopMode::ForceOneShot: request.loopOverride = false; break;
    case VfxLoopMode::AssetDefault:
    default: break;
    }
    request.randomSeed = slot.randomSeed;
    request.parameters = ResolveVfxVariableBindings(slot, context.variables.get(), &outDiagnostics);
    outRequest = std::move(request);
    outTarget = target;
    return true;
}

bool RefreshVfxTargetTransform(const VfxPlayerSlot& slot, const VfxResolvedTarget& target,
                               const VfxBindingResolveContext& context, VfxWorldTransform& outTransform) {
    VfxWorldTransform base{};
    if (!ResolveBaseTransform(target, context, base)) {
        return false;
    }
    outTransform = ComposeOffset(base, slot.offset);
    return true;
}

std::vector<VfxParameterOverride> ResolveVfxVariableBindings(const VfxPlayerSlot& slot,
                                                             const BINDING::Snapshot* variables,
                                                             std::vector<VfxPlaybackDiagnostic>* outDiagnostics) {
    std::vector<VfxParameterOverride> result{};
    for (const VfxVariableBinding& binding : slot.variableBindings) {
        const BINDING::Value* value = variables != nullptr ? variables->Find(binding.variableId) : nullptr;
        if (value == nullptr) {
            if (outDiagnostics != nullptr) {
                outDiagnostics->push_back({VfxDiagnosticSeverity::Warning, "Vfx.VariableUnavailable",
                                           "VFX variable binding is unavailable: " + binding.variableId});
            }
            continue;
        }
        result.push_back({binding.inputId, ConvertValue(*value)});
    }
    return result;
}

} // namespace HIKARI::VFX
