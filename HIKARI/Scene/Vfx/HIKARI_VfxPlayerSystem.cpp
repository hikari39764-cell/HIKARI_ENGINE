#include "Scene/Vfx/HIKARI_VfxPlayerSystem.h"

#include <algorithm>
#include <memory>
#include <unordered_set>

#include "HIKARI/Project/FrameContext.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI {
namespace {
bool GetWorldTransform(const GameObject& object, VFX::VfxWorldTransform& outTransform) {
    const Transform3D& transform = object.GetTransform();
    if (MATH::DecomposeTRS(transform.GetWorldMatrix(), outTransform.position, outTransform.rotation,
                           outTransform.scale)) {
        return true;
    }
    outTransform.position = transform.position;
    outTransform.rotation = transform.rotation;
    outTransform.scale = transform.scale;
    return true;
}

bool HasCommand(const std::vector<VfxSlotCommand>& commands, VfxSlotId slotId, VfxSlotCommandKind kind) {
    return std::any_of(commands.begin(), commands.end(), [slotId, kind](const VfxSlotCommand& command) {
        return command.slotId == slotId && command.kind == kind;
    });
}
} // namespace

void VfxPlayerSystem::OnWorldAttached(World& world) {
    admissionStats_ = {};
    playbackService_ = world.Services().Find<VFX::VfxPlaybackService>();
    bindingStore_ = world.Services().Find<BINDING::RuntimeBindingStore>();
    runtimePlayState_ = world.Services().Find<RuntimePlayStateService>();
}

void VfxPlayerSystem::OnWorldDetached(World&) {
    for (auto& [key, state] : states_) {
        (void)key;
        StopState(state);
    }
    states_.clear();
    playbackService_ = nullptr;
    bindingStore_ = nullptr;
    runtimePlayState_ = nullptr;
}

void VfxPlayerSystem::PreRender(World& world, const FrameContext& frame) {
    // A frame may render more than once (bake passes); slot commands and
    // event triggers are consumed on the first pass only.
    if (lastFrameIndex_ == frame.frameIndex)
        return;
    lastFrameIndex_ = frame.frameIndex;
    if (playbackService_ == nullptr || bindingStore_ == nullptr || runtimePlayState_ == nullptr ||
        !runtimePlayState_->IsActive()) {
        for (auto& [key, state] : states_) {
            (void)key;
            StopState(state);
        }
        states_.clear();
        return;
    }

    const std::shared_ptr<const BINDING::Snapshot> snapshot = bindingStore_->PublishSnapshot();
    std::unordered_set<SlotKey, SlotKeyHash> seen{};
    world.ForEachObjectWith<VfxPlayerComponent>([this, &world, &snapshot, &seen](GameObject& owner,
                                                                                 VfxPlayerComponent& component) {
        VFX::VfxWorldTransform ownerTransform{};
        (void)GetWorldTransform(owner, ownerTransform);
        VFX::VfxBindingResolveContext baseContext{};
        baseContext.ownerObject = owner.GetRuntimeHandle();
        baseContext.ownerTransform = ownerTransform;
        baseContext.variables = snapshot;
        baseContext.resolveRuntimeObject = [&world](RuntimeObjectHandle handle, VFX::VfxWorldTransform& outTransform) {
            const GameObject* object = world.FindObject(handle);
            return object != nullptr && GetWorldTransform(*object, outTransform);
        };
        baseContext.resolveSceneObject = [&world](SceneObjectId id, VFX::VfxWorldTransform& outTransform) {
            const GameObject* object = world.FindObject(id);
            return object != nullptr && GetWorldTransform(*object, outTransform);
        };

        const std::vector<VfxSlotCommand> commands = component.ConsumePendingCommands();
        for (const VfxPlayerSlot& slot : component.GetSlots()) {
            const SlotKey key{owner.GetRuntimeHandle(), slot.id};
            seen.insert(key);
            SlotRuntimeState& state = states_[key];
            std::erase_if(state.active,
                          [this](const ActivePlayback& active) { return !playbackService_->IsActive(active.handle); });

            if (!component.IsEnabled() || !slot.enabled || HasCommand(commands, slot.id, VfxSlotCommandKind::Stop)) {
                StopState(state);
                continue;
            }
            RefreshActive(slot, baseContext, state);
            if (HasCommand(commands, slot.id, VfxSlotCommandKind::Play)) {
                TriggerSlot(slot, baseContext, state, 0u);
            }
            if (slot.playOnStart && !state.autoPlayConsumed) {
                state.autoPlayConsumed = true;
                TriggerSlot(slot, baseContext, state, 0u);
            }
            if (!slot.triggerEventId.empty()) {
                for (const BINDING::EventRecord* event :
                     bindingStore_->FindEventsAfter(slot.triggerEventId, state.lastObservedEventSequence)) {
                    state.lastObservedEventSequence = (std::max)(state.lastObservedEventSequence, event->sequence);
                    VFX::VfxBindingResolveContext eventContext = baseContext;
                    eventContext.event = event;
                    TriggerSlot(slot, eventContext, state, event->sequence);
                }
            }
        }
    });

    for (auto found = states_.begin(); found != states_.end();) {
        if (!seen.contains(found->first)) {
            StopState(found->second);
            found = states_.erase(found);
        } else {
            ++found;
        }
    }
}

const VfxPlayerSystemAdmissionStats& VfxPlayerSystem::GetAdmissionStats() const noexcept { return admissionStats_; }

void VfxPlayerSystem::StopState(SlotRuntimeState& state) {
    if (playbackService_ != nullptr) {
        for (const ActivePlayback& active : state.active) {
            (void)playbackService_->Stop(active.handle);
        }
    }
    state.active.clear();
}

void VfxPlayerSystem::TriggerSlot(const VfxPlayerSlot& slot, const VFX::VfxBindingResolveContext& context,
                                  SlotRuntimeState& state, uint64_t eventSequence) {
    if (slot.retriggerPolicy == VfxRetriggerPolicy::IgnoreIfPlaying && !state.active.empty()) {
        return;
    }
    if (slot.retriggerPolicy == VfxRetriggerPolicy::Restart) {
        StopState(state);
    }
    if (slot.retriggerPolicy == VfxRetriggerPolicy::Parallel &&
        state.active.size() >= VFX::kMaximumParallelVfxInstancesPerSlot) {
        ++admissionStats_.parallelLimitRejections;
        return;
    }
    VFX::VfxPlayRequest request{};
    VFX::VfxResolvedTarget target{};
    std::vector<VFX::VfxPlaybackDiagnostic> diagnostics{};
    if (!VFX::BuildVfxPlayRequest(slot, context, request, target, diagnostics)) {
        return;
    }
    request.randomSeed ^= static_cast<uint32_t>(eventSequence);
    VFX::VfxPlayResult result = playbackService_->PlayDetailed(request);
    if (result.IsAccepted()) {
        state.active.push_back({result.handle, target});
        ++admissionStats_.acceptedTriggers;
    } else {
        ++admissionStats_.playbackRejections;
    }
}

void VfxPlayerSystem::RefreshActive(const VfxPlayerSlot& slot, const VFX::VfxBindingResolveContext& context,
                                    SlotRuntimeState& state) {
    const std::vector<VFX::VfxParameterOverride> parameters =
        VFX::ResolveVfxVariableBindings(slot, context.variables.get());
    for (size_t index = state.active.size(); index-- > 0u;) {
        ActivePlayback& active = state.active[index];
        if (!playbackService_->IsActive(active.handle)) {
            state.active.erase(state.active.begin() + index);
            continue;
        }
        (void)playbackService_->SetParameters(active.handle, parameters);
        if (!slot.followTarget) {
            continue;
        }
        VFX::VfxWorldTransform transform{};
        if (!VFX::RefreshVfxTargetTransform(slot, active.target, context, transform)) {
            (void)playbackService_->Stop(active.handle);
            state.active.erase(state.active.begin() + index);
            continue;
        }
        (void)playbackService_->SetTransform(active.handle, transform);
    }
}

size_t VfxPlayerSystem::SlotKeyHash::operator()(const SlotKey& key) const noexcept {
    uint64_t hash = key.owner.ToValue();
    hash ^= key.slotId.value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    return static_cast<size_t>(hash);
}

} // namespace HIKARI
