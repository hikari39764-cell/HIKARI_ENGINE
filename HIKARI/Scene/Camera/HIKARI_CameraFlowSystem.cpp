#include "Scene/Camera/HIKARI_CameraFlowSystem.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "HIKARI/Project/FrameContext.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include <algorithm>
#include <utility>
#include <unordered_set>
namespace HIKARI {
using namespace SEQUENCER;
void CameraFlowSystem::OnWorldDetached(World& world) {
    if (auto* playback = world.Services().Find<SequencePlaybackService>())
        for (auto handle : tracked_)
            (void)playback->Stop(handle, SequenceStopReason::OwnerDestroyed);
    tracked_.clear();
}
void CameraFlowSystem::LateUpdate(World& world, const FrameContext& frame) {
    const auto* play = world.Services().Find<RuntimePlayStateService>();
    if (!play || !play->IsActive())
        return;
    std::vector<std::pair<GameObject*, CameraFlowComponent*>> flows;
    world.ForEachObjectWith<CameraFlowComponent>(
        [&](GameObject& o, CameraFlowComponent& c) { flows.emplace_back(&o, &c); });
    // 同時侵入は優先度、同順位はシーン ID で決定し、オブジェクト配列順には依存しない。
    std::sort(flows.begin(), flows.end(), [](const auto& a, const auto& b) {
        if (a.second->Settings().priority != b.second->Settings().priority)
            return a.second->Settings().priority > b.second->Settings().priority;
        return a.first->GetDocumentId().value < b.first->GetDocumentId().value;
    });
    std::vector<SequencePlaybackHandle> handles;
    std::unordered_set<uint64_t> claimed;
    std::unordered_set<CameraFlowComponent*> advanced;
    // 実行中フローを先に完了させ、そのフレームの交還先を他の領域が上書きしない。
    for (auto [owner, component] : flows) {
        const auto stage = component->runtime_.stage;
        if (stage == CameraFlowStage::Playing || stage == CameraFlowStage::Returning) {
            claimed.insert(component->settings_.cameraObjectId.value);
            Process(world, *owner, *component, frame.gameDt, true);
            advanced.insert(component);
        }
    }
    for (auto [owner, component] : flows) {
        if (!advanced.contains(component)) {
            const auto before = component->runtime_.runs;
            Process(world, *owner, *component, frame.gameDt,
                    !claimed.contains(component->settings_.cameraObjectId.value));
            if (before != component->runtime_.runs)
                claimed.insert(component->settings_.cameraObjectId.value);
        }
        if (component->runtime_.playback.IsValid())
            handles.push_back(component->runtime_.playback);
    }
    if (auto* playback = world.Services().Find<SequencePlaybackService>())
        for (auto handle : tracked_)
            if (std::find(handles.begin(), handles.end(), handle) == handles.end())
                (void)playback->Stop(handle, SequenceStopReason::OwnerDestroyed);
    tracked_ = std::move(handles);
}
void CameraFlowSystem::Finish(World& world, CameraFlowComponent& flow, CameraFollowComponent& camera, bool completed) {
    auto& r = flow.runtime_;
    const auto& s = flow.settings_;
    if (completed || s.finishOnInterrupt) {
        if (!s.afterMode.empty())
            camera.GameplayState().baseMode = s.afterMode;
        if (auto* director = world.Services().Find<CameraDirector>())
            director->SetBaseCamera(s.cameraObjectId);
        r.returnRemaining =
            camera.GameplayState().source ? camera.GameplayState().source->cinematicReturnSeconds : 0.45f;
        r.stage = CameraFlowStage::Returning;
        HIKARI_LOG_INFO("[CameraFlow] handoff mode=" + s.afterMode +
                        " camera=" + std::to_string(s.cameraObjectId.value));
    } else {
        r.stage = CameraFlowStage::Interrupted;
        r.diagnostic = "Cinematic interrupted; previous gameplay mode retained.";
    }
    if (auto* playback = world.Services().Find<SequencePlaybackService>())
        (void)playback->Stop(r.playback, completed ? SequenceStopReason::Completed : SequenceStopReason::Stopped);
    r.playback = {};
}
void CameraFlowSystem::Process(World& world, GameObject& owner, CameraFlowComponent& flow, float dt,
                               bool startAllowed) {
    auto& r = flow.runtime_;
    const auto& s = flow.settings_;
    auto* playback = world.Services().Find<SequencePlaybackService>();
    auto* subject = world.FindObject(s.subjectObjectId);
    auto* object = world.FindObject(s.cameraObjectId);
    auto* camera = object ? object->GetComponent<CameraFollowComponent>() : nullptr;
    const auto* lens = object ? object->GetComponent<CameraComponent>() : nullptr;
    auto fail = [&](CameraFlowStage stage, const char* diagnostic) {
        if (playback)
            (void)playback->Stop(r.playback, SequenceStopReason::Disabled);
        r.playback = {};
        r.stage = stage;
        r.diagnostic = diagnostic;
    };
    if (!s.enabled) {
        fail(CameraFlowStage::Disabled, "");
        return;
    }
    if (!playback || !subject || !camera || !camera->IsEnabled() || !lens || !lens->IsEnabled()) {
        fail(CameraFlowStage::Invalid, "Assign an existing subject and enabled gameplay camera; enable Cinematics.");
        return;
    }
    std::string error;
    const auto* store = world.Services().Find<GameplayCameraAssetStore>();
    auto profile = camera->GameplayState().previewSource;
    if (!profile && store)
        profile = store->Load(camera->GameplayBinding().profile, error);
    if (!profile || (!s.afterMode.empty() && !profile->FindMode(s.afterMode))) {
        fail(CameraFlowStage::Invalid, "The camera profile or destination mode is missing.");
        return;
    }
    const bool wasInside = r.inside;
    r.inside = CAMERA::ContainsCameraSubject(owner, s.halfExtents, CAMERA::WorldPosition(*subject));
    if (r.stage == CameraFlowStage::Returning) {
        r.returnRemaining -= std::max(0.0f, dt);
        if (r.returnRemaining <= 0)
            r.stage = CameraFlowStage::Completed;
    }
    if (r.stage == CameraFlowStage::Playing) {
        if (flow.interruptPending_) {
            flow.interruptPending_ = false;
            Finish(world, flow, *camera, false);
            return;
        }
        SequencePlaybackSnapshot snapshot;
        if (!playback->TryGetSnapshot(r.playback, snapshot)) {
            Finish(world, flow, *camera, false);
            return;
        }
        r.time = snapshot.timeSeconds;
        r.duration = snapshot.durationSeconds;
        // 最終フレームを保持し、基底モードを更新してから解放する。旧モードが一瞬映る隙間を作らない。
        if (snapshot.state == SequencePlaybackState::Paused && snapshot.timeSeconds >= snapshot.durationSeconds)
            Finish(world, flow, *camera, true);
        return;
    }
    if (r.stage == CameraFlowStage::Returning)
        return;
    const bool manual = std::exchange(flow.triggerPending_, false);
    if (r.entered && !s.repeatOnReentry && !manual)
        return;
    const auto& base = camera->GameplayState().baseMode.empty() ? camera->GameplayBinding().initialMode
                                                                : camera->GameplayState().baseMode;
    if (!s.requiredMode.empty() && s.requiredMode != (base.empty() ? profile->defaultMode : base))
        return;
    if (manual || (s.triggerOnExit ? (!r.inside && wasInside) : (r.inside && !wasInside))) {
        if (!startAllowed) {
            r.stage = CameraFlowStage::Invalid;
            r.diagnostic = "Another flow owns this camera handoff.";
            return;
        }
        if (!s.sequence.IsValid()) {
            r.entered = true;
            ++r.runs;
            r.time = r.duration = 0;
            r.diagnostic.clear();
            Finish(world, flow, *camera, true);
            return;
        }
        SequencePlayRequest request;
        request.assetGuid = s.sequence;
        request.channel = "Cinematics";
        request.priority = s.priority;
        request.channelPolicy = SequenceChannelPolicy::RejectIfOccupied;
        request.options.holdAtEnd = true;
        (void)request.bindings.BindSlot("Player", s.subjectObjectId);
        (void)request.bindings.BindSlot("Camera", s.cameraObjectId);
        (void)request.bindings.BindSlot("Owner", owner.GetDocumentId());
        auto result = playback->PlayDetailed(request);
        if (!result.IsAccepted()) {
            r.stage = CameraFlowStage::Invalid;
            r.diagnostic =
                result.diagnostics.empty() ? "Sequence could not start." : result.diagnostics.front().message;
            HIKARI_LOG_WARN("[CameraFlow] " + owner.GetName() + ": " + r.diagnostic);
            return;
        }
        r.entered = true;
        r.playback = result.handle;
        r.stage = CameraFlowStage::Playing;
        r.time = 0;
        ++r.runs;
        r.diagnostic.clear();
        HIKARI_LOG_INFO("[CameraFlow] entered " + owner.GetName() + " sequence=" + s.sequence.value);
    } else if (!r.entered || (s.repeatOnReentry && !r.inside))
        r.stage = CameraFlowStage::Armed;
}
} // namespace HIKARI
