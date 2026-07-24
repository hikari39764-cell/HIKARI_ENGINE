#include "Scene/Sequencer/Runtime/HIKARI_SequencePlayerSystem.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Components/HIKARI_SequencePlayerComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"

namespace HIKARI {

    namespace {
        std::string BuildDiagnosticMessage(
            const std::vector<SEQUENCER::SequencePlaybackDiagnostic>&
                diagnostics) {

            std::ostringstream stream{};
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.message.empty()) {
                    continue;
                }
                if (stream.tellp() > 0) {
                    stream << " | ";
                }
                if (!diagnostic.code.empty()) {
                    stream << '[' << diagnostic.code << "] ";
                }
                stream << diagnostic.message;
            }
            return stream.str();
        }
    }

    void SequencePlayerSystem::OnWorldAttached(World& world) {
        playbackService_ = world.Services().Find<
            SEQUENCER::SequencePlaybackService>();
        runtimePlayState_ = world.Services().Find<
            RuntimePlayStateService>();
    }

    void SequencePlayerSystem::OnWorldDetached(World& world) {
        if (playbackService_ == nullptr) {
            runtimePlayState_ = nullptr;
            return;
        }
        world.ForEachObjectWith<SequencePlayerComponent>(
            [this](GameObject&, SequencePlayerComponent& component) {
                StopComponent(
                    component,
                    SEQUENCER::SequenceStopReason::OwnerDestroyed);
            });
        for (SEQUENCER::SequencePlaybackHandle handle : trackedHandles_) {
            if (playbackService_->IsActive(handle)) {
                (void)playbackService_->Stop(
                    handle,
                    SEQUENCER::SequenceStopReason::OwnerDestroyed);
            }
        }
        trackedHandles_.clear();
        playbackService_ = nullptr;
        runtimePlayState_ = nullptr;
    }

    void SequencePlayerSystem::Update(
        World& world,
        const FrameContext&) {

        if (playbackService_ == nullptr || runtimePlayState_ == nullptr ||
            !runtimePlayState_->IsActive()) {
            return;
        }
        std::vector<SEQUENCER::SequencePlaybackHandle> activeHandles{};
        world.ForEachObjectWith<SequencePlayerComponent>(
            [this, &activeHandles](
                GameObject& owner,
                SequencePlayerComponent& component) {
                ProcessComponent(owner, component);
                if (component.playbackHandle_.IsValid()) {
                    activeHandles.push_back(component.playbackHandle_);
                }
            });
        for (SEQUENCER::SequencePlaybackHandle tracked : trackedHandles_) {
            if (std::find(
                    activeHandles.begin(),
                    activeHandles.end(),
                    tracked) == activeHandles.end() &&
                playbackService_->IsActive(tracked)) {
                (void)playbackService_->Stop(
                    tracked,
                    SEQUENCER::SequenceStopReason::OwnerDestroyed);
            }
        }
        trackedHandles_ = std::move(activeHandles);
    }

    void SequencePlayerSystem::ProcessComponent(
        GameObject& owner,
        SequencePlayerComponent& component) {

        using Action = SequencePlayerComponent::PendingAction;
        if (component.playbackHandle_.IsValid() &&
            !playbackService_->IsActive(component.playbackHandle_)) {
            component.playbackHandle_ = {};
            component.runtimePaused_ = false;
        }

        if (component.pendingAction_ == Action::Stop) {
            StopComponent(
                component,
                SEQUENCER::SequenceStopReason::Stopped);
            component.pendingAction_ = Action::None;
        }
        if (!component.enabled_) {
            if (component.stopOnDisable_) {
                StopComponent(
                    component,
                    SEQUENCER::SequenceStopReason::Disabled);
            }
            return;
        }

        const Action action = component.pendingAction_;
        component.pendingAction_ = Action::None;
        if (action == Action::Play ||
            (action == Action::None && component.autoPlayPending_)) {
            component.autoPlayPending_ = false;
            PlayComponent(owner, component);
        } else if (action == Action::Pause) {
            if (playbackService_->Pause(component.playbackHandle_)) {
                component.runtimePaused_ = true;
            }
        } else if (action == Action::Resume) {
            if (playbackService_->Resume(component.playbackHandle_)) {
                component.runtimePaused_ = false;
            }
        } else if (action == Action::Seek) {
            (void)playbackService_->Seek(
                component.playbackHandle_,
                component.pendingSeekTimeSeconds_);
        }

        SEQUENCER::SequencePlaybackSnapshot snapshot{};
        if (playbackService_->TryGetSnapshot(
                component.playbackHandle_,
                snapshot)) {
            component.runtimePaused_ = snapshot.state ==
                SEQUENCER::SequencePlaybackState::Paused;
        }
    }

    void SequencePlayerSystem::PlayComponent(
        GameObject& owner,
        SequencePlayerComponent& component) {

        if (component.sequenceAssetGuid_.empty()) {
            component.lastDiagnostic_ =
                "[InvalidAssetRequest] Sequence Asset is not assigned";
            return;
        }
        if (playbackService_->IsActive(component.playbackHandle_)) {
            if (!component.restartIfPlaying_) {
                component.lastDiagnostic_ =
                    "Sequence is already active";
                return;
            }
            (void)playbackService_->Stop(
                component.playbackHandle_,
                SEQUENCER::SequenceStopReason::Replaced);
            component.playbackHandle_ = {};
        }

        SEQUENCER::SequencePlayRequest request{};
        request.assetGuid = AssetGuid{ component.sequenceAssetGuid_ };
        request.options.loop = component.loop_;
        request.options.playbackRate = component.playbackRate_;
        request.startTimeSeconds = component.startTimeSeconds_;
        request.channel = component.channel_;
        request.priority = component.priority_;
        request.channelPolicy = component.channelPolicy_;
        if (!component.ownerSlotName_.empty()) {
            (void)request.bindings.BindSlot(
                component.ownerSlotName_,
                owner.GetDocumentId());
        }
        for (const SequencePlayerBinding& binding : component.bindings_) {
            if (!binding.slotName.empty() &&
                binding.sceneObjectId.value != 0) {
                (void)request.bindings.BindSlot(
                    binding.slotName,
                    binding.sceneObjectId);
            }
        }

        SEQUENCER::SequencePlayResult result =
            playbackService_->PlayDetailed(request);
        component.playbackHandle_ = result.handle;
        component.runtimePaused_ = false;
        component.lastDiagnostic_ = BuildDiagnosticMessage(
            result.diagnostics);
        if (result.IsAccepted() && component.lastDiagnostic_.empty()) {
            component.lastDiagnostic_ = "Sequence playback started";
        }
        if (result.IsAccepted()) {
            HIKARI_LOG_INFO(
                "[SequencePlayer] started asset=" +
                component.sequenceAssetGuid_ +
                " owner=" + owner.GetName());
        } else {
            HIKARI_LOG_WARN(
                "[SequencePlayer] rejected asset=" +
                component.sequenceAssetGuid_ +
                " owner=" + owner.GetName() +
                " reason=" +
                (component.lastDiagnostic_.empty()
                    ? std::string("Unknown")
                    : component.lastDiagnostic_));
        }
    }

    void SequencePlayerSystem::StopComponent(
        SequencePlayerComponent& component,
        SEQUENCER::SequenceStopReason reason) {

        if (playbackService_ != nullptr &&
            component.playbackHandle_.IsValid()) {
            (void)playbackService_->Stop(
                component.playbackHandle_,
                reason);
        }
        component.playbackHandle_ = {};
        component.runtimePaused_ = false;
    }

} // namespace HIKARI
