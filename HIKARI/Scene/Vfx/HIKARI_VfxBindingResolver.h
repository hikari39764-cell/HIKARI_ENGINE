#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackTypes.h"

namespace HIKARI::VFX {

using VfxRuntimeObjectTransformResolver = std::function<bool(RuntimeObjectHandle, VfxWorldTransform&)>;
using VfxSceneObjectTransformResolver = std::function<bool(SceneObjectId, VfxWorldTransform&)>;

struct VfxBindingResolveContext {
    RuntimeObjectHandle ownerObject{};
    VfxWorldTransform ownerTransform{};
    const BINDING::EventRecord* event = nullptr;
    std::shared_ptr<const BINDING::Snapshot> variables{};
    VfxRuntimeObjectTransformResolver resolveRuntimeObject{};
    VfxSceneObjectTransformResolver resolveSceneObject{};
};

struct VfxResolvedTarget {
    VfxSpawnTarget kind = VfxSpawnTarget::Owner;
    RuntimeObjectHandle runtimeObject{};
    SceneObjectId sceneObject{};
    VfxWorldTransform baseTransform{};
};

bool BuildVfxPlayRequest(const VfxPlayerSlot& slot, const VfxBindingResolveContext& context, VfxPlayRequest& outRequest,
                         VfxResolvedTarget& outTarget, std::vector<VfxPlaybackDiagnostic>& outDiagnostics);

bool RefreshVfxTargetTransform(const VfxPlayerSlot& slot, const VfxResolvedTarget& target,
                               const VfxBindingResolveContext& context, VfxWorldTransform& outTransform);

std::vector<VfxParameterOverride> ResolveVfxVariableBindings(
    const VfxPlayerSlot& slot, const BINDING::Snapshot* variables,
    std::vector<VfxPlaybackDiagnostic>* outDiagnostics = nullptr);

} // namespace HIKARI::VFX
