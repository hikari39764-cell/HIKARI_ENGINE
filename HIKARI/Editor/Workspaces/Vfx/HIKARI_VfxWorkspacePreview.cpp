#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Vfx/HIKARI_VfxLightOutput.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::EDITOR {

    bool VfxWorkspaceController::CopyPreviewDocument(
        const VfxWorkspaceController& source, std::string& outMessage) {
        if (!source.document_.IsOpen()) { outMessage = "No VFX document is open"; return false; }
        document_.NewAsset(source.document_.Asset().name);
        if (!document_.ApplyAsset(source.document_.Asset(), outMessage)) return false;
        previewProgramRevision_ = 0;
        cameraController_.ResetFromCamera(source.cameraController_.GetCamera());
        previewInputOverrides_ = source.previewInputOverrides_;
        hiddenEmitterIds_ = source.hiddenEmitterIds_;
        soloEmitterId_ = source.soloEmitterId_;
        previewBypassAdjustments_ = source.previewBypassAdjustments_;
        previewCleanView_ = source.previewCleanView_;
        showPreviewGrid_ = source.showPreviewGrid_;
        showPreviewAxes_ = source.showPreviewAxes_;
        *previewEnvironment_ = *source.previewEnvironment_;
        previewLightIntensity_ = source.previewLightIntensity_;
        return true;
    }

    nlohmann::json VfxWorkspaceController::DescribeBackgroundPreview() const {
        const auto& input = previewRenderMailbox_.GetCurrent();
        size_t drawableSubmissions = 0;
        if (input) for (const auto& emitter : input->emitters)
            if (emitter.drawAfterSimulation) ++drawableSubmissions;
        return {{"actualTime", previewElapsedSeconds_}, {"waitingForRender", previewRenderMailbox_.HasPending()},
            {"seekPending", previewSeekTarget_.has_value()}, {"restartPending", previewRestartPending_},
            {"drawableSubmissions", drawableSubmissions}, {"previewMessage", statusMessage_}};
    }

    bool VfxWorkspaceController::PrepareBackgroundPreview(
        EditorViewInstance& view, float timeSeconds, std::string& outMessage) {
        if (!document_.IsOpen()) { outMessage = "No VFX document is open"; return false; }
        if (!std::isfinite(timeSeconds) || timeSeconds < 0 || timeSeconds > GetPreviewTimelineDuration()) {
            outMessage = "Capture time is outside the document timeline"; return false;
        }
        auto& camera = cameraController_.GetCamera();
        camera.SetPerspective(camera.GetFovYRad(), view.extent.Aspect(),
            camera.GetNearZ(), camera.GetFarZ());
        // 明示的な撮影はドラッグ用 debounce を待たない。背景では時間を進めないため。
        previewRestartDelaySeconds_ = 0.0f;
        if (!previewPlaying_ && !PlayPreview(outMessage)) return false;
        if (!SeekPreview(timeSeconds, outMessage)) return false;
        PausePreview(true);
        backgroundFrozenPrepared_ = false;
        previewNeedsRedraw_ = true;
        return true;
    }

    void VfxWorkspaceController::TickBackgroundPreview(
        EditorViewInstance& view, uint64_t frameIndex) {
        AcknowledgePreviewRenderInput();
        previewRestartDelaySeconds_ = 0.0f;
        if (previewSeekTarget_ || previewRestartPending_ || previewRenderMailbox_.HasPending()) {
            UpdatePreview(frameIndex, 0.0f);
        } else if (!backgroundFrozenPrepared_) {
            if (playbackService_.PublishFrozenRenderInput()) (void)PublishPreviewRenderInput();
            backgroundFrozenPrepared_ = true;
        }
        SubmitPreviewRequest(view, true);
    }

    bool VfxWorkspaceController::IsBackgroundPreviewSettled() const noexcept {
        return backgroundFrozenPrepared_ && !previewSeekTarget_ && !previewRestartPending_ &&
            !previewRenderMailbox_.HasPending();
    }

    void VfxWorkspaceController::SetPreviewPlaybackSpeed(
        float speed) noexcept {

        previewPlaybackSpeed_ = std::clamp(
            std::isfinite(speed) ? speed : 1.0f,
            0.1f,
            4.0f);
    }

    float VfxWorkspaceController::GetPreviewTimelineDuration()
        const noexcept {

        constexpr float kMinimumRange = 1.0f / 60.0f;
        if (!document_.IsOpen()) {
            return kMinimumRange;
        }
        const auto& asset = document_.Asset();
        float end = asset.durationSeconds;
        for (const auto& layer : asset.emitters) {
            if (!layer.basic.enabled || layer.kind == VFX::VfxLayerKind::Group) continue;
            const float activeEnd = layer.activeDuration > 0 ? layer.startDelay + layer.activeDuration : asset.durationSeconds;
            float tail = 0;
            if (layer.kind == VFX::VfxLayerKind::Particles)
                tail = layer.basic.lifetime.mode == VFX::VfxDistributionMode::RandomRange ?
                    layer.basic.lifetime.maximum : layer.basic.lifetime.minimum;
            else if (layer.history.mode != VFX::VfxHistoryMode::None) tail = layer.history.duration;
            end = (std::max)(end, activeEnd + tail);
        }
        return (std::max)(end, kMinimumRange);
    }

    void VfxWorkspaceController::NormalizePreviewTimelineRange() noexcept {
        constexpr float kMinimumRange = 1.0f / 60.0f;
        const float duration = GetPreviewTimelineDuration();
        if (!previewLoopRangeInitialized_) {
            previewLoopEnabled_ = true;
            previewLoopStartSeconds_ = 0.0f;
            // A looping authored effect must wrap at the same authored
            // duration as runtime playback. One-shot effects include their
            // particle lifetime tail before the Editor repeats them.
            previewLoopEndSeconds_ = document_.IsOpen() &&
                document_.Asset().loop
                ? std::clamp(
                    document_.Asset().durationSeconds,
                    kMinimumRange,
                    duration)
                : duration;
            previewLoopRangeInitialized_ = true;
            return;
        }
        previewLoopStartSeconds_ = std::clamp(
            previewLoopStartSeconds_,
            0.0f,
            (std::max)(duration - kMinimumRange, 0.0f));
        previewLoopEndSeconds_ = std::clamp(
            previewLoopEndSeconds_,
            previewLoopStartSeconds_ + kMinimumRange,
            duration);
    }

    void VfxWorkspaceController::SetPreviewLoopEnabled(
        bool enabled) noexcept {

        previewLoopEnabled_ = enabled;
        if (enabled && previewPlaying_ &&
            (previewElapsedSeconds_ < previewLoopStartSeconds_ ||
             previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f)) {
            previewRestartPending_ = true;
            previewRestartDelaySeconds_ = 0.0f;
        }
    }

    void VfxWorkspaceController::SetPreviewLoopRange(
        float startSeconds,
        float endSeconds) noexcept {

        constexpr float kMinimumRange = 1.0f / 60.0f;
        const float duration = GetPreviewTimelineDuration();
        previewLoopStartSeconds_ = std::clamp(
            std::isfinite(startSeconds) ? startSeconds : 0.0f,
            0.0f,
            (std::max)(duration - kMinimumRange, 0.0f));
        previewLoopEndSeconds_ = std::clamp(
            std::isfinite(endSeconds) ? endSeconds : duration,
            previewLoopStartSeconds_ + kMinimumRange,
            duration);
        previewLoopRangeInitialized_ = true;
        if (previewPlaying_ &&
            (previewElapsedSeconds_ < previewLoopStartSeconds_ ||
             previewElapsedSeconds_ > previewLoopEndSeconds_)) {
            previewRestartPending_ = true;
            previewRestartDelaySeconds_ = 0.0f;
        }
    }

    bool VfxWorkspaceController::PlayPreview(
        std::string& outMessage) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            statusMessage_ = outMessage;
            return false;
        }
        NormalizePreviewTimelineRange();
        if (previewPlaying_ && previewHandle_.IsValid()) {
            PausePreview(false);
            outMessage = "VFX preview resumed";
            statusMessage_ = outMessage;
            return true;
        }
        if (!RebuildPreviewProgram(outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        VFX::VfxPlayRequest request{};
        request.assetGuid = document_.GetAssetGuid().IsValid()
            ? document_.GetAssetGuid()
            : GenerateAssetGuid();
        request.program = previewProgram_;
        // Preview loop ranges are Editor session state. Runtime looping stays
        // disabled so every wrap is an explicit reset + deterministic seek.
        request.loopOverride = false;
        request.randomSeed = 0x48494b41u;
        request.parameters = BuildPreviewParameters();
        const VFX::VfxPlayResult result =
            playbackService_.PlayDetailed(request);
        if (!result.IsAccepted()) {
            outMessage = result.diagnostics.empty()
                ? "VFX preview playback was rejected"
                : result.diagnostics.front().message;
            statusMessage_ = outMessage;
            previewHandle_ = {};
            previewPlaying_ = false;
            return false;
        }
        previewHandle_ = result.handle;
        previewElapsedSeconds_ = 0.0f;
        previewPlaying_ = true;
        previewPaused_ = false;
        previewAccumulatedDeltaSeconds_ = 0.0f;
        previewRestartPending_ = false;
        previewRestartDelaySeconds_ = 0.0f;
        if (!SeekActivePreview(
                previewLoopStartSeconds_, outMessage, true)) {
            if (previewHandle_.IsValid()) {
                (void)playbackService_.Stop(previewHandle_);
            }
            previewHandle_ = {};
            previewPlaying_ = false;
            previewPaused_ = false;
            statusMessage_ = outMessage;
            return false;
        }
        outMessage = "VFX preview playing";
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::PausePreview(bool paused) {
        if (!previewPlaying_) {
            return;
        }
        NormalizePreviewTimelineRange();
        previewPaused_ = paused;
        // 凍結入力は UpdatePreview が GPU 受領後に配信する。未受領の
        // reset/spawn/seek をここで消すと、停止した瞬間の粒子状態を失う。
        if (!paused && previewLoopEnabled_ &&
            previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f) {
            previewRestartPending_ = true;
        }
        statusMessage_ = paused
            ? "VFX preview paused"
            : "VFX preview resumed";
    }

    void VfxWorkspaceController::StopPreview() {
        previewSeekTarget_.reset();
        NormalizePreviewTimelineRange();
        if (previewHandle_.IsValid()) {
            (void)playbackService_.Stop(previewHandle_);
            playbackService_.Update(previewFrameIndex_, 0.0f);
            (void)PublishPreviewRenderInput(
                VFX::VfxRenderInputPublishMode::SupersedePending);
        }
        ReleasePreviewNamespace();
        previewHandle_ = {};
        previewPlaying_ = false;
        previewPaused_ = false;
        previewRestartPending_ = false;
        previewRestartDelaySeconds_ = 0.0f;
        previewAccumulatedDeltaSeconds_ = 0.0f;
        previewElapsedSeconds_ = previewLoopStartSeconds_;
    }

    bool VfxWorkspaceController::RestartPreview(
        std::string& outMessage) {

        StopPreview();
        return PlayPreview(outMessage);
    }

    bool VfxWorkspaceController::SeekActivePreview(
        float seconds,
        std::string& outMessage,
        bool supersedePending) {

        if (!previewHandle_.IsValid()) {
            outMessage = "VFX preview is not active";
            return false;
        }
        if (previewRenderMailbox_.HasPending() && !supersedePending) {
            outMessage = "VFX preview is waiting for the current GPU frame";
            return false;
        }

        constexpr float kNominalFixedStep = 1.0f / 60.0f;
        const float duration = GetPreviewTimelineDuration();
        const float target = std::clamp(
            std::isfinite(seconds) ? seconds : 0.0f,
            0.0f,
            duration);
        const VFX::VfxSeekCapacity capacity =
            playbackService_.GetSeekCapacity(
                previewHandle_, previewFrameIndex_);
        if (!capacity.handleActive || capacity.maximumSteps == 0u) {
            outMessage = "VFX preview seek has no simulation budget";
            return false;
        }

        constexpr float kSeekEpsilon = 1.0e-6f;
        const float simulationStart =
            target + kSeekEpsilon < capacity.currentSeconds
                ? 0.0f
                : capacity.currentSeconds;
        const float simulationSpan = (std::max)(
            target - simulationStart, 0.0f);
        constexpr uint32_t kMaximumEditorSeekSteps = 64u;
        const uint32_t maximumSteps = (std::min)(
            capacity.maximumSteps,
            kMaximumEditorSeekSteps);
        const float fixedStep = simulationSpan <= kSeekEpsilon
            ? kNominalFixedStep
            : maximumSteps <= 1u
                ? simulationSpan
                : (std::max)(
                    kNominalFixedStep,
                    simulationSpan /
                        static_cast<float>(maximumSteps - 1u));
        const VFX::VfxSeekResult result = playbackService_.Seek(
            previewHandle_,
            previewFrameIndex_,
            target,
            fixedStep,
            maximumSteps);
        if (!result.accepted || !result.complete) {
            outMessage = result.diagnostics.empty()
                ? "VFX preview seek did not complete atomically"
                : result.diagnostics.front().message;
            return false;
        }
        previewElapsedSeconds_ = result.currentSeconds;
        previewAccumulatedDeltaSeconds_ = 0.0f;
        const VFX::VfxRenderInputPublishMode publishMode =
            supersedePending
                ? VFX::VfxRenderInputPublishMode::SupersedePending
                : VFX::VfxRenderInputPublishMode::RequireAcknowledged;
        if (!PublishPreviewRenderInput(publishMode)) {
            outMessage = "VFX preview frame is still pending";
            return false;
        }
        outMessage.clear();
        return true;
    }

    bool VfxWorkspaceController::SeekPreview(
        float seconds,
        std::string& outMessage) {

        if (!document_.IsOpen()) {
            outMessage = "No VFX document is open";
            statusMessage_ = outMessage;
            return false;
        }
        NormalizePreviewTimelineRange();
        const bool wasActive =
            previewPlaying_ && previewHandle_.IsValid();
        const bool wasPaused = previewPaused_;
        if (!wasActive && !PlayPreview(outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        previewPaused_ = wasActive ? wasPaused : true;
        if (previewRenderMailbox_.HasPending() || previewRestartPending_) {
            // Scrubbing follows UpdatePreview, which can already have sent a
            // GPU frame. Keep the latest target until that frame is consumed.
            previewSeekTarget_ = std::clamp(std::isfinite(seconds) ? seconds : 0.0f,
                0.0f, GetPreviewTimelineDuration());
            outMessage = "VFX preview seek queued";
            statusMessage_ = outMessage;
            return true;
        }
        previewSeekTarget_.reset();
        if (!SeekActivePreview(
                seconds, outMessage, !wasActive)) {
            statusMessage_ = outMessage;
            return false;
        }
        previewPlaying_ = true;
        previewPaused_ = wasActive ? wasPaused : true;
        outMessage = "VFX preview moved to " +
            std::to_string(previewElapsedSeconds_) + " s";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::SetPreviewInput(
        std::string_view inputId,
        const VFX::VfxValue& value,
        std::string& outMessage) {

        const VFX::VfxExposedInputData* input = FindInput(inputId);
        if (input == nullptr) {
            outMessage = "VFX preview input was not found";
            statusMessage_ = outMessage;
            return false;
        }
        if (input->type != value.type) {
            outMessage = "VFX preview input type mismatch";
            statusMessage_ = outMessage;
            return false;
        }
        previewInputOverrides_[std::string(inputId)] = value;
        if (previewHandle_.IsValid()) {
            std::vector<VFX::VfxPlaybackDiagnostic> diagnostics{};
            if (!playbackService_.SetParameters(
                    previewHandle_,
                    { VFX::VfxParameterOverride{
                        std::string(inputId), value } },
                    &diagnostics)) {
                outMessage = diagnostics.empty()
                    ? "VFX preview input was rejected"
                    : diagnostics.front().message;
                statusMessage_ = outMessage;
                return false;
            }
        }
        outMessage = "VFX preview input updated";
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::ResetPreviewInputs() {
        previewInputOverrides_.clear();
        if (previewPlaying_) {
            previewRestartPending_ = true;
            previewRestartDelaySeconds_ = 0.0f;
        }
        statusMessage_ = "VFX preview inputs reset";
    }

    bool VfxWorkspaceController::RebuildPreviewProgram(
        std::string& outMessage) {

        if (previewProgram_ != nullptr &&
            previewProgramRevision_ == document_.GetRevision()) {
            return true;
        }
        const VFX::VfxCompileResult result =
            VFX::CompileVfxAsset(document_.Asset());
        diagnostics_ = result.diagnostics;
        if (!result.success) {
            previewProgram_.reset();
            previewProgramRevision_ = 0u;
            const auto failure=std::find_if(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& diagnostic){
                return diagnostic.severity==VFX::VfxDiagnosticSeverity::Error;
            });
            outMessage = failure==result.diagnostics.end() ? "VFX preview compile failed" : failure->message;
            return false;
        }
        previewProgram_ = std::make_shared<const VFX::VfxProgram>(
            result.program);
        previewProgramRevision_ = document_.GetRevision();
        outMessage.clear();
        return true;
    }

    void VfxWorkspaceController::UpdatePreview(
        uint64_t frameIndex,
        float deltaSeconds) {

        previewFrameIndex_ = frameIndex;
        NormalizePreviewTimelineRange();
        AcknowledgePreviewRenderInput();
        const float elapsed = std::isfinite(deltaSeconds)
            ? (std::max)(deltaSeconds, 0.0f)
            : 0.0f;
        // Match VfxPlaybackService's runtime delta clamp. A realtime VFX view
        // is rendered every presented frame, so this only preserves time
        // across a genuine renderer/material retry.
        constexpr float kMaximumPendingPreviewTime = 0.25f;
        if (previewPlaying_ && !previewPaused_) {
            previewAccumulatedDeltaSeconds_ = (std::min)(
                previewAccumulatedDeltaSeconds_ +
                    elapsed * previewPlaybackSpeed_,
                kMaximumPendingPreviewTime);
        }
        bool simulationPublished = false;
        if (previewRestartPending_ && previewPlaying_) {
            previewRestartDelaySeconds_ = (std::max)(
                previewRestartDelaySeconds_ - elapsed,
                0.0f);
        }
        if (previewRenderMailbox_.HasPending()) {
            return;
        }
        if (!previewSeekTarget_ && !previewRestartPending_ &&
            previewPaused_ && previewPlaying_ && previewHandle_.IsValid() &&
            playbackService_.PublishFrozenRenderInput()) {
            // GPU 受領済みの最終状態だけを停止中の描画入力として残す。
            (void)PublishPreviewRenderInput(
                VFX::VfxRenderInputPublishMode::SupersedePending);
            return;
        }
        if (previewRestartPending_ && previewPlaying_) {
            if (previewRestartDelaySeconds_ <= 0.0f) {
                const bool wasPaused = previewPaused_;
                const auto seekTarget = previewSeekTarget_;
                std::string message{};
                if (!RestartPreview(message)) {
                    statusMessage_ = message;
                } else if (wasPaused) {
                    PausePreview(true);
                }
                if (previewPlaying_) previewSeekTarget_ = seekTarget;
                simulationPublished = previewPlaying_;
            }
        }
        if (previewSeekTarget_ && !previewRenderMailbox_.HasPending() &&
            previewPlaying_ && !previewRestartPending_) {
            const float target = *previewSeekTarget_;
            previewSeekTarget_.reset();
            if (!SeekActivePreview(target, statusMessage_)) {
                previewPaused_ = true;
                return;
            }
            statusMessage_ = "VFX preview moved to " + std::to_string(previewElapsedSeconds_) + " s";
            simulationPublished = true;
        }
        if (!previewPlaying_ ||
            (previewPaused_ && !simulationPublished)) {
            return;
        }
        if (!simulationPublished && previewPlaying_ && !previewPaused_ &&
            previewLoopEnabled_ &&
            previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f) {
            std::string message{};
            if (!SeekActivePreview(
                    previewLoopStartSeconds_, message)) {
                statusMessage_ = message;
                previewPaused_ = true;
            }
            simulationPublished = true;
        }
        if (!simulationPublished) {
            float simulationDelta = 0.0f;
            if (previewPlaying_ && !previewPaused_) {
                simulationDelta = (std::min)(
                    previewAccumulatedDeltaSeconds_,
                    kMaximumPendingPreviewTime);
                if (previewLoopEnabled_) {
                    simulationDelta = (std::min)(
                        simulationDelta,
                        (std::max)(
                            previewLoopEndSeconds_ -
                                previewElapsedSeconds_,
                            0.0f));
                }
            }
            if (simulationDelta <= 0.0f) {
                return;
            }
            playbackService_.Update(frameIndex, simulationDelta);
            previewAccumulatedDeltaSeconds_ = (std::max)(
                previewAccumulatedDeltaSeconds_ - simulationDelta,
                0.0f);
            if (previewPlaying_ && !previewPaused_) {
                const float playbackEnd = previewLoopEnabled_
                    ? previewLoopEndSeconds_
                    : GetPreviewTimelineDuration();
                previewElapsedSeconds_ = (std::min)(
                    previewElapsedSeconds_ + simulationDelta,
                    playbackEnd);
            }
            if (!PublishPreviewRenderInput()) {
                statusMessage_ =
                    "VFX preview delivery rejected an unacknowledged frame";
                return;
            }
        }
        if (previewPlaying_ &&
            !playbackService_.IsActive(previewHandle_)) {
            ReleasePreviewNamespace();
            previewPlaying_ = false;
            previewPaused_ = false;
            previewHandle_ = {};
        }

    }

    void VfxWorkspaceController::ReleasePreviewNamespace() {
        if (previewNamespaceRetireCallback_) {
            previewNamespaceRetireCallback_(
                playbackService_.GetPlaybackNamespace());
        }
    }

    void VfxWorkspaceController::AcknowledgePreviewRenderInput() {
#if defined(HIKARI_WITH_EDITOR)
        if (activePreviewViewId_.value == 0u) {
            return;
        }
        const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
            RENDER3D::EDITORVIEW::GetOutput(activePreviewViewId_);
        if (output.ready) {
            (void)previewRenderMailbox_.Acknowledge(
                output.sceneRevision);
        }
#endif
    }

    bool VfxWorkspaceController::PublishPreviewRenderInput(
        VFX::VfxRenderInputPublishMode mode) {

        RENDER3D::PARTICLES::ParticleRenderInput input =
            playbackService_.GetRenderInput();
        if (!hiddenEmitterIds_.empty() || !soloEmitterId_.empty()) {
            // 描画添字はコンパイル済み順序。source 配列の並びとは一致しない。
            const auto visible = [this](size_t index) {
                return previewProgram_ && index < previewProgram_->emitters.size() &&
                    IsEmitterPreviewVisible(previewProgram_->emitters[index].sourceEmitterId);
            };
            std::erase_if(
                input.emitters,
                [&visible](
                    const RENDER3D::PARTICLES::ParticleEmitterSubmission&
                        submission) {
                    return !visible(submission.key.emitterIndex);
                });
        }
        if (previewProgram_) for (auto& composition:input.compositions) for (auto& layer:composition.layers) {
            if (layer.emitterIndex>=previewProgram_->emitters.size()) continue;
            if (!IsEmitterPreviewVisible(previewProgram_->emitters[layer.emitterIndex].sourceEmitterId) ||
                (previewBypassAdjustments_ && layer.kind==RENDER3D::PARTICLES::ParticleCompositionLayerKind::Adjustment)) layer.enabled=false;
        }
        if (previewProgram_) for (auto& submission : input.emitters) {
            if (submission.key.emitterIndex >= previewProgram_->emitters.size()) continue;
            std::erase_if(submission.render.spatialEffects, [&](const auto& effect) {
                if (effect.ownerEmitterIndex >= previewProgram_->emitters.size()) return true;
                const auto& owner = previewProgram_->emitters[effect.ownerEmitterIndex];
                return !IsEmitterPreviewVisible(owner.sourceEmitterId) ||
                    (previewBypassAdjustments_ && owner.kind == VFX::VfxLayerKind::Adjustment);
            });
            for (const auto& curve : previewProgram_->emitters[submission.key.emitterIndex].layerCurves) {
                if (curve.channel != VFX::VfxLayerChannel::MaterialEffectMix || curve.ownerEmitterIndex >= previewProgram_->emitters.size()) continue;
                const auto& owner = previewProgram_->emitters[curve.ownerEmitterIndex];
                if (!previewBypassAdjustments_ && IsEmitterPreviewVisible(owner.sourceEmitterId)) continue;
                for (auto& parameter : submission.render.animatedParameters)
                    if (parameter.slot == curve.materialSlot) parameter.value = 0;
            }
        }
        // 全レイヤー非表示も有効な入力。直前の描画を保持すると非表示にできない。
        const bool published =
            previewRenderMailbox_.Publish(std::move(input), mode) != 0u;
        if (published) {
            // 新しい入力が届いたフレームは描き直す。
            previewNeedsRedraw_ = true;
        }
        return published;
    }

    void VfxWorkspaceController::SubmitPreviewRequest(
        EditorViewInstance& previewView,
        bool canvasVisible) {

#if defined(HIKARI_WITH_EDITOR)
        if (activePreviewViewId_.value != 0u &&
            activePreviewViewId_.value != previewView.renderViewId.value) {
            RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
        }
        activePreviewViewId_ = previewView.renderViewId;
        if (!document_.IsOpen() || !canvasVisible) {
            RENDER3D::EDITORVIEW::ClearRequest(
                previewView.renderViewId);
            return;
        }
        RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
        // 無変更の判定は view scheduler が所有する。停止中も extent/露出/guide
        // の変更を渡し、内容が変わったときだけ再描画する。
        if (previewNeedsRedraw_) ++previewContentRevision_;
        previewNeedsRedraw_ = false;
        request.contentRevision = previewContentRevision_;
        request.viewId = previewView.renderViewId;
        request.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
        request.cameraFrame.camera = cameraController_.GetCamera();
        request.cameraFrame.revision = cameraRevision_;
        request.cameraFrame.cameraCut = cameraCutPending_;
        request.cameraFrame.valid = true;
        request.width = previewView.extent.width;
        request.height = previewView.extent.height;
        request.sceneRevision = previewRenderMailbox_.GetSequence();
        request.visible = canvasVisible;
        request.drawDebug = false;
        // 再生中は連続描画、停止中はカメラ/資産/設定の変更時に更新する。
        request.continuousRealtime = !previewPaused_;
        request.schedulingPriority = 110u;
        request.shadingMode =
            RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Lit;
        request.displayExposure =
            previewView.visualization.displayExposure;
        request.sceneSourceOverride = emptySceneSource_.get();
        request.environmentOverride = previewEnvironment_.get();
        // 作業用の主光源を保ち、素材自身の光源は同じ runtime bridge で再構成する。
        previewLighting_->lights.resize(1);
        std::vector<uint8_t> visibleLayers;
        for (const auto& layer : document_.Asset().emitters)
            visibleLayers.push_back(IsEmitterPreviewVisible(layer.id) ? 1u : 0u);
        if (previewPlaying_)
            VFX::AppendVfxLightProviders(playbackService_, previewLighting_->lights, visibleLayers);
        request.lightingOverride = previewLighting_.get();
        request.particleInputOverride = previewRenderMailbox_.GetCurrent();
        request.guideOverlay.showGroundGrid = showPreviewGrid_ && !previewCleanView_;
        request.guideOverlay.showWorldAxes = showPreviewAxes_ && !previewCleanView_;
        request.guideOverlay.gridHalfCount = 12;
        request.guideOverlay.gridSpacing = 0.5f;
        request.guideOverlay.axisLength = 1.25f;
        request.suppressSpatialLightingProviders = true;
        request.useDefaultSkyIbl = true;
        request.spatialAntiAliasing = true;
        RENDER3D::EDITORVIEW::SubmitRequest(request);
        cameraCutPending_ = false;
#else
        (void)previewView;
        (void)canvasVisible;
#endif
    }

    std::vector<VFX::VfxParameterOverride>
        VfxWorkspaceController::BuildPreviewParameters() const {

        std::vector<VFX::VfxParameterOverride> parameters{};
        parameters.reserve(previewInputOverrides_.size());
        for (const auto& [inputId, value] : previewInputOverrides_) {
            if (const VFX::VfxExposedInputData* input =
                    FindInput(inputId);
                input != nullptr && input->type == value.type) {
                parameters.push_back({ inputId, value });
            }
        }
        return parameters;
    }

} // namespace HIKARI::EDITOR
