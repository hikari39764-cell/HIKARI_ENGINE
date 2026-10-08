#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewSession.h"

#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Vfx/HIKARI_VfxLightOutput.h"

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::EDITOR {
namespace {
void CollectSubtreeIds(const VFX::VfxAssetData& asset, std::string_view rootId,
                       std::unordered_set<std::string>& outIds) {
    outIds.insert(std::string(rootId));
    bool expanded = true;
    while (expanded) {
        expanded = false;
        for (const VFX::VfxEmitterData& emitter : asset.emitters) {
            if (!emitter.parentEmitterId.empty() && outIds.contains(emitter.parentEmitterId) &&
                outIds.insert(emitter.id).second) {
                expanded = true;
            }
        }
    }
}

} // namespace

void VfxPreviewSession::AcknowledgePreviewRenderInput() {
#if defined(HIKARI_WITH_EDITOR)
    if (activePreviewViewId_.value == 0u) {
        return;
    }
    const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
        RENDER3D::EDITORVIEW::GetOutput(activePreviewViewId_);
    if (output.ready) {
        (void)previewRenderMailbox_.Acknowledge(output.sceneRevision);
    }
#endif
}
std::vector<VFX::VfxParameterOverride> VfxPreviewSession::BuildPreviewParameters() const {
    std::vector<VFX::VfxParameterOverride> parameters{};
    parameters.reserve(previewInputOverrides_.size());
    for (const auto& [inputId, value] : previewInputOverrides_) {
        if (const VFX::VfxExposedInputData* input = FindInput(inputId); input != nullptr && input->type == value.type) {
            parameters.push_back({inputId, value});
        }
    }
    return parameters;
}
void VfxPreviewSession::ClearSoloEmitter() noexcept {
    soloEmitterId_.clear();
    NotifyEmitterVisibilityChanged();
}

float VfxPreviewSession::GetPreviewElapsedSeconds() const noexcept { return previewElapsedSeconds_; }
float VfxPreviewSession::GetPreviewLoopEndSeconds() const noexcept { return previewLoopEndSeconds_; }
float VfxPreviewSession::GetPreviewLoopStartSeconds() const noexcept { return previewLoopStartSeconds_; }
float VfxPreviewSession::GetPreviewPlaybackSpeed() const noexcept { return previewPlaybackSpeed_; }
float VfxPreviewSession::GetPreviewTimelineDuration() const noexcept {
    constexpr float kMinimumRange = 1.0f / 60.0f;
    if (!source_) {
        return kMinimumRange;
    }
    const auto& asset = *source_;
    float end = asset.durationSeconds;
    for (const auto& layer : asset.emitters) {
        if (!layer.basic.enabled || layer.kind == VFX::VfxLayerKind::Group)
            continue;
        const float activeEnd =
            layer.activeDuration > 0 ? layer.startDelay + layer.activeDuration : asset.durationSeconds;
        float tail = 0;
        if (layer.kind == VFX::VfxLayerKind::Particles)
            tail = layer.basic.lifetime.mode == VFX::VfxDistributionMode::RandomRange ? layer.basic.lifetime.maximum
                                                                                      : layer.basic.lifetime.minimum;
        else if (layer.history.mode != VFX::VfxHistoryMode::None)
            tail = layer.history.duration;
        end = (std::max)(end, activeEnd + tail);
    }
    return (std::max)(end, kMinimumRange);
}
const std::string& VfxPreviewSession::GetSoloEmitterId() const noexcept { return soloEmitterId_; }

bool VfxPreviewSession::IsEmitterHidden(std::string_view emitterId) const noexcept {
    return hiddenEmitterIds_.contains(std::string(emitterId));
}
bool VfxPreviewSession::IsEmitterPreviewVisible(std::string_view emitterId) const noexcept {
    if (!source_)
        return false;
    const VFX::VfxAssetData& asset = *source_;

    if (!soloEmitterId_.empty()) {
        std::unordered_set<std::string> soloTree{};
        CollectSubtreeIds(asset, soloEmitterId_, soloTree);
        if (!soloTree.contains(std::string(emitterId))) {
            return false;
        }
    }

    std::string current{emitterId};
    for (int guard = 0; guard < 1024 && !current.empty(); ++guard) {
        if (hiddenEmitterIds_.contains(current))
            return false;
        const VFX::VfxEmitterData* emitter = nullptr;
        for (const VFX::VfxEmitterData& candidate : asset.emitters) {
            if (candidate.id == current) {
                emitter = &candidate;
                break;
            }
        }
        if (emitter == nullptr)
            break;
        current = emitter->parentEmitterId;
    }
    return true;
}
bool VfxPreviewSession::IsPreviewLoopEnabled() const noexcept { return previewLoopEnabled_; }
bool VfxPreviewSession::IsPreviewPaused() const noexcept { return previewPaused_; }
bool VfxPreviewSession::IsPreviewPlaying() const noexcept { return previewPlaying_; }
void VfxPreviewSession::NormalizePreviewTimelineRange() noexcept {
    constexpr float kMinimumRange = 1.0f / 60.0f;
    const float duration = GetPreviewTimelineDuration();
    if (!previewLoopRangeInitialized_) {
        previewLoopEnabled_ = true;
        previewLoopStartSeconds_ = 0.0f;
        // A looping authored effect must wrap at the same authored
        // duration as runtime playback. One-shot effects include their
        // particle lifetime tail before the Editor repeats them.
        previewLoopEndSeconds_ =
            source_ && source_->loop ? std::clamp(source_->durationSeconds, kMinimumRange, duration) : duration;
        previewLoopRangeInitialized_ = true;
        return;
    }
    previewLoopStartSeconds_ = std::clamp(previewLoopStartSeconds_, 0.0f, (std::max)(duration - kMinimumRange, 0.0f));
    previewLoopEndSeconds_ = std::clamp(previewLoopEndSeconds_, previewLoopStartSeconds_ + kMinimumRange, duration);
}
void VfxPreviewSession::NotifyEmitterStructureChanged() noexcept {
    if (source_) {
        std::erase_if(hiddenEmitterIds_, [this](const std::string& id) { return FindEmitter(id) == nullptr; });
        if (!soloEmitterId_.empty() && FindEmitter(soloEmitterId_) == nullptr) {
            soloEmitterId_.clear();
        }
    } else {
        hiddenEmitterIds_.clear();
        soloEmitterId_.clear();
    }
    NotifyEmitterVisibilityChanged();
}
void VfxPreviewSession::NotifyEmitterVisibilityChanged() noexcept {
    if (source_) {
        (void)PublishPreviewRenderInput(VFX::VfxRenderInputPublishMode::SupersedePending);
    }
}
void VfxPreviewSession::PausePreview(bool paused) {
    if (!previewPlaying_) {
        return;
    }
    NormalizePreviewTimelineRange();
    previewPaused_ = paused;
    // 凍結入力は UpdatePreview が GPU 受領後に配信する。未受領の
    // reset/spawn/seek をここで消すと、停止した瞬間の粒子状態を失う。
    if (!paused && previewLoopEnabled_ && previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f) {
        previewRestartPending_ = true;
    }
    statusMessage_ = paused ? "VFX preview paused" : "VFX preview resumed";
}
bool VfxPreviewSession::PlayPreview(std::string& outMessage) {
    if (!source_) {
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
    request.assetGuid = sourceGuid_.IsValid() ? sourceGuid_ : GenerateAssetGuid();
    request.program = previewProgram_;
    // Preview loop ranges are Editor session state. Runtime looping stays
    // disabled so every wrap is an explicit reset + deterministic seek.
    request.loopOverride = false;
    request.randomSeed = 0x48494b41u;
    request.parameters = BuildPreviewParameters();
    const VFX::VfxPlayResult result = playbackService_.PlayDetailed(request);
    if (!result.IsAccepted()) {
        outMessage =
            result.diagnostics.empty() ? "VFX preview playback was rejected" : result.diagnostics.front().message;
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
    if (!SeekActivePreview(previewLoopStartSeconds_, outMessage, true)) {
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

bool VfxPreviewSession::PublishPreviewRenderInput(VFX::VfxRenderInputPublishMode mode) {
    RENDER3D::PARTICLES::ParticleRenderInput input = playbackService_.GetRenderInput();
    if (!hiddenEmitterIds_.empty() || !soloEmitterId_.empty()) {
        // 描画添字はコンパイル済み順序。source 配列の並びとは一致しない。
        const auto visible = [this](size_t index) {
            return previewProgram_ && index < previewProgram_->emitters.size() &&
                   IsEmitterPreviewVisible(previewProgram_->emitters[index].sourceEmitterId);
        };
        std::erase_if(input.emitters, [&visible](const RENDER3D::PARTICLES::ParticleEmitterSubmission& submission) {
            return !visible(submission.key.emitterIndex);
        });
    }
    if (previewProgram_)
        for (auto& composition : input.compositions)
            for (auto& layer : composition.layers) {
                if (layer.emitterIndex >= previewProgram_->emitters.size())
                    continue;
                if (!IsEmitterPreviewVisible(previewProgram_->emitters[layer.emitterIndex].sourceEmitterId) ||
                    (previewBypassAdjustments_ &&
                     layer.kind == RENDER3D::PARTICLES::ParticleCompositionLayerKind::Adjustment))
                    layer.enabled = false;
            }
    if (previewProgram_)
        for (auto& submission : input.emitters) {
            if (submission.key.emitterIndex >= previewProgram_->emitters.size())
                continue;
            std::erase_if(submission.render.spatialEffects, [&](const auto& effect) {
                if (effect.ownerEmitterIndex >= previewProgram_->emitters.size())
                    return true;
                const auto& owner = previewProgram_->emitters[effect.ownerEmitterIndex];
                return !IsEmitterPreviewVisible(owner.sourceEmitterId) ||
                       (previewBypassAdjustments_ && owner.kind == VFX::VfxLayerKind::Adjustment);
            });
            for (const auto& curve : previewProgram_->emitters[submission.key.emitterIndex].layerCurves) {
                if (curve.channel != VFX::VfxLayerChannel::MaterialEffectMix ||
                    curve.ownerEmitterIndex >= previewProgram_->emitters.size())
                    continue;
                const auto& owner = previewProgram_->emitters[curve.ownerEmitterIndex];
                if (!previewBypassAdjustments_ && IsEmitterPreviewVisible(owner.sourceEmitterId))
                    continue;
                for (auto& parameter : submission.render.animatedParameters)
                    if (parameter.slot == curve.materialSlot)
                        parameter.value = 0;
            }
        }
    // 全レイヤー非表示も有効な入力。直前の描画を保持すると非表示にできない。
    const bool published = previewRenderMailbox_.Publish(std::move(input), mode) != 0u;
    if (published) {
        // 新しい入力が届いたフレームは描き直す。
        previewNeedsRedraw_ = true;
    }
    return published;
}
bool VfxPreviewSession::RebuildPreviewProgram(std::string& outMessage) {
    if (previewProgram_ != nullptr && !previewProgramDirty_) {
        return true;
    }
    const VFX::VfxCompileResult result = VFX::CompileVfxAsset(*source_);
    diagnostics_ = result.diagnostics;
    if (!result.success) {
        previewProgram_.reset();
        previewProgramDirty_ = true;
        const auto failure =
            std::find_if(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.severity == VFX::VfxDiagnosticSeverity::Error;
            });
        outMessage = failure == result.diagnostics.end() ? "VFX preview compile failed" : failure->message;
        return false;
    }
    previewProgram_ = std::make_shared<const VFX::VfxProgram>(result.program);
    previewProgramDirty_ = false;
    outMessage.clear();
    return true;
}
void VfxPreviewSession::ReleasePreviewNamespace() {
    if (previewNamespaceRetireCallback_) {
        previewNamespaceRetireCallback_(playbackService_.GetPlaybackNamespace());
    }
}
void VfxPreviewSession::ResetPreviewInputs() {
    previewInputOverrides_.clear();
    if (previewPlaying_) {
        previewRestartPending_ = true;
        previewRestartDelaySeconds_ = 0.0f;
    }
    statusMessage_ = "VFX preview inputs reset";
}
bool VfxPreviewSession::RestartPreview(std::string& outMessage) {
    StopPreview();
    return PlayPreview(outMessage);
}
bool VfxPreviewSession::SeekActivePreview(float seconds, std::string& outMessage, bool supersedePending) {
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
    const float target = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, duration);
    const VFX::VfxSeekCapacity capacity = playbackService_.GetSeekCapacity(previewHandle_, previewFrameIndex_);
    if (!capacity.handleActive || capacity.maximumSteps == 0u) {
        outMessage = "VFX preview seek has no simulation budget";
        return false;
    }

    constexpr float kSeekEpsilon = 1.0e-6f;
    const float simulationStart = target + kSeekEpsilon < capacity.currentSeconds ? 0.0f : capacity.currentSeconds;
    const float simulationSpan = (std::max)(target - simulationStart, 0.0f);
    constexpr uint32_t kMaximumEditorSeekSteps = 64u;
    const uint32_t maximumSteps = (std::min)(capacity.maximumSteps, kMaximumEditorSeekSteps);
    const float fixedStep = simulationSpan <= kSeekEpsilon ? kNominalFixedStep
                            : maximumSteps <= 1u
                                ? simulationSpan
                                : (std::max)(kNominalFixedStep, simulationSpan / static_cast<float>(maximumSteps - 1u));
    const VFX::VfxSeekResult result =
        playbackService_.Seek(previewHandle_, previewFrameIndex_, target, fixedStep, maximumSteps);
    if (!result.accepted || !result.complete) {
        outMessage = result.diagnostics.empty() ? "VFX preview seek did not complete atomically"
                                                : result.diagnostics.front().message;
        return false;
    }
    previewElapsedSeconds_ = result.currentSeconds;
    previewAccumulatedDeltaSeconds_ = 0.0f;
    const VFX::VfxRenderInputPublishMode publishMode = supersedePending
                                                           ? VFX::VfxRenderInputPublishMode::SupersedePending
                                                           : VFX::VfxRenderInputPublishMode::RequireAcknowledged;
    if (!PublishPreviewRenderInput(publishMode)) {
        outMessage = "VFX preview frame is still pending";
        return false;
    }
    outMessage.clear();
    return true;
}
bool VfxPreviewSession::SeekPreview(float seconds, std::string& outMessage) {
    if (!source_) {
        outMessage = "No VFX document is open";
        statusMessage_ = outMessage;
        return false;
    }
    NormalizePreviewTimelineRange();
    const bool wasActive = previewPlaying_ && previewHandle_.IsValid();
    const bool wasPaused = previewPaused_;
    if (!wasActive && !PlayPreview(outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    previewPaused_ = wasActive ? wasPaused : true;
    if (previewRenderMailbox_.HasPending() || previewRestartPending_) {
        // Scrubbing follows UpdatePreview, which can already have sent a
        // GPU frame. Keep the latest target until that frame is consumed.
        previewSeekTarget_ = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, GetPreviewTimelineDuration());
        outMessage = "VFX preview seek queued";
        statusMessage_ = outMessage;
        return true;
    }
    previewSeekTarget_.reset();
    if (!SeekActivePreview(seconds, outMessage, !wasActive)) {
        statusMessage_ = outMessage;
        return false;
    }
    previewPlaying_ = true;
    previewPaused_ = wasActive ? wasPaused : true;
    outMessage = "VFX preview moved to " + std::to_string(previewElapsedSeconds_) + " s";
    statusMessage_ = outMessage;
    return true;
}
void VfxPreviewSession::SetEmitterHidden(std::string_view emitterId, bool hidden) noexcept {
    if (hidden) {
        hiddenEmitterIds_.insert(std::string(emitterId));
    } else {
        hiddenEmitterIds_.erase(std::string(emitterId));
    }
    NotifyEmitterVisibilityChanged();
}
bool VfxPreviewSession::SetPreviewInput(std::string_view inputId, const VFX::VfxValue& value, std::string& outMessage) {
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
        if (!playbackService_.SetParameters(previewHandle_, {VFX::VfxParameterOverride{std::string(inputId), value}},
                                            &diagnostics)) {
            outMessage = diagnostics.empty() ? "VFX preview input was rejected" : diagnostics.front().message;
            statusMessage_ = outMessage;
            return false;
        }
    }
    outMessage = "VFX preview input updated";
    statusMessage_ = outMessage;
    return true;
}
void VfxPreviewSession::SetPreviewLoopEnabled(bool enabled) noexcept {
    previewLoopEnabled_ = enabled;
    if (enabled && previewPlaying_ &&
        (previewElapsedSeconds_ < previewLoopStartSeconds_ ||
         previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f)) {
        previewRestartPending_ = true;
        previewRestartDelaySeconds_ = 0.0f;
    }
}
void VfxPreviewSession::SetPreviewLoopRange(float startSeconds, float endSeconds) noexcept {
    constexpr float kMinimumRange = 1.0f / 60.0f;
    const float duration = GetPreviewTimelineDuration();
    previewLoopStartSeconds_ =
        std::clamp(std::isfinite(startSeconds) ? startSeconds : 0.0f, 0.0f, (std::max)(duration - kMinimumRange, 0.0f));
    previewLoopEndSeconds_ = std::clamp(std::isfinite(endSeconds) ? endSeconds : duration,
                                        previewLoopStartSeconds_ + kMinimumRange, duration);
    previewLoopRangeInitialized_ = true;
    if (previewPlaying_ &&
        (previewElapsedSeconds_ < previewLoopStartSeconds_ || previewElapsedSeconds_ > previewLoopEndSeconds_)) {
        previewRestartPending_ = true;
        previewRestartDelaySeconds_ = 0.0f;
    }
}
void VfxPreviewSession::SetPreviewNamespaceRetireCallback(PreviewNamespaceRetireCallback callback) {
    previewNamespaceRetireCallback_ = std::move(callback);
}
void VfxPreviewSession::SetPreviewPlaybackSpeed(float speed) noexcept {
    previewPlaybackSpeed_ = std::clamp(std::isfinite(speed) ? speed : 1.0f, 0.1f, 4.0f);
}
void VfxPreviewSession::SetSoloEmitter(std::string_view emitterId) noexcept {
    soloEmitterId_ = std::string(emitterId);
    NotifyEmitterVisibilityChanged();
}
bool VfxPreviewSession::StepPreviewFrame(int frameCount, std::string& outMessage) {
    if (!source_) {
        outMessage = "No VFX document is open";
        return false;
    }
    const int steps = std::clamp(frameCount == 0 ? 1 : frameCount, -120, 120);
    const float stepSeconds = 1.0f / 60.0f;
    const float playbackEnd = previewLoopEnabled_ ? previewLoopEndSeconds_ : GetPreviewTimelineDuration();
    const float current = previewSeekTarget_.value_or(previewElapsedSeconds_);
    const float target = std::clamp(current + static_cast<float>(steps) * stepSeconds,
                                    previewLoopEnabled_ ? previewLoopStartSeconds_ : 0.0f, playbackEnd);
    if (std::abs(target - current) < 1.0e-6f) {
        outMessage = "Preview is at the boundary of its playback range";
        statusMessage_ = outMessage;
        previewPaused_ = true;
        return false;
    }
    if (!SeekPreview(target, outMessage)) {
        statusMessage_ = outMessage;
        previewPaused_ = true;
        return false;
    }
    previewPaused_ = true;
    outMessage = "Preview step requested: " + std::to_string(steps) + " frame(s)";
    statusMessage_ = outMessage;
    return true;
}
void VfxPreviewSession::StopPreview() {
    previewSeekTarget_.reset();
    NormalizePreviewTimelineRange();
    if (previewHandle_.IsValid()) {
        (void)playbackService_.Stop(previewHandle_);
        playbackService_.Update(previewFrameIndex_, 0.0f);
        (void)PublishPreviewRenderInput(VFX::VfxRenderInputPublishMode::SupersedePending);
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

void VfxPreviewSession::UpdatePreview(uint64_t frameIndex, float deltaSeconds) {
    previewFrameIndex_ = frameIndex;
    NormalizePreviewTimelineRange();
    AcknowledgePreviewRenderInput();
    const float elapsed = std::isfinite(deltaSeconds) ? (std::max)(deltaSeconds, 0.0f) : 0.0f;
    // Match VfxPlaybackService's runtime delta clamp. A realtime VFX view
    // is rendered every presented frame, so this only preserves time
    // across a genuine renderer/material retry.
    constexpr float kMaximumPendingPreviewTime = 0.25f;
    if (previewPlaying_ && !previewPaused_) {
        previewAccumulatedDeltaSeconds_ =
            (std::min)(previewAccumulatedDeltaSeconds_ + elapsed * previewPlaybackSpeed_, kMaximumPendingPreviewTime);
    }
    bool simulationPublished = false;
    if (previewRestartPending_ && previewPlaying_) {
        previewRestartDelaySeconds_ = (std::max)(previewRestartDelaySeconds_ - elapsed, 0.0f);
    }
    if (previewRenderMailbox_.HasPending()) {
        return;
    }
    if (!previewSeekTarget_ && !previewRestartPending_ && previewPaused_ && previewPlaying_ &&
        previewHandle_.IsValid() && playbackService_.PublishFrozenRenderInput()) {
        // GPU 受領済みの最終状態だけを停止中の描画入力として残す。
        (void)PublishPreviewRenderInput(VFX::VfxRenderInputPublishMode::SupersedePending);
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
            if (previewPlaying_)
                previewSeekTarget_ = seekTarget;
            simulationPublished = previewPlaying_;
        }
    }
    if (previewSeekTarget_ && !previewRenderMailbox_.HasPending() && previewPlaying_ && !previewRestartPending_) {
        const float target = *previewSeekTarget_;
        previewSeekTarget_.reset();
        if (!SeekActivePreview(target, statusMessage_)) {
            previewPaused_ = true;
            return;
        }
        statusMessage_ = "VFX preview moved to " + std::to_string(previewElapsedSeconds_) + " s";
        simulationPublished = true;
    }
    if (!previewPlaying_ || (previewPaused_ && !simulationPublished)) {
        return;
    }
    if (!simulationPublished && previewPlaying_ && !previewPaused_ && previewLoopEnabled_ &&
        previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f) {
        std::string message{};
        if (!SeekActivePreview(previewLoopStartSeconds_, message)) {
            statusMessage_ = message;
            previewPaused_ = true;
        }
        simulationPublished = true;
    }
    if (!simulationPublished) {
        float simulationDelta = 0.0f;
        if (previewPlaying_ && !previewPaused_) {
            simulationDelta = (std::min)(previewAccumulatedDeltaSeconds_, kMaximumPendingPreviewTime);
            if (previewLoopEnabled_) {
                simulationDelta =
                    (std::min)(simulationDelta, (std::max)(previewLoopEndSeconds_ - previewElapsedSeconds_, 0.0f));
            }
        }
        if (simulationDelta <= 0.0f) {
            return;
        }
        playbackService_.Update(frameIndex, simulationDelta);
        previewAccumulatedDeltaSeconds_ = (std::max)(previewAccumulatedDeltaSeconds_ - simulationDelta, 0.0f);
        if (previewPlaying_ && !previewPaused_) {
            const float playbackEnd = previewLoopEnabled_ ? previewLoopEndSeconds_ : GetPreviewTimelineDuration();
            previewElapsedSeconds_ = (std::min)(previewElapsedSeconds_ + simulationDelta, playbackEnd);
        }
        if (!PublishPreviewRenderInput()) {
            statusMessage_ = "VFX preview delivery rejected an unacknowledged frame";
            return;
        }
    }
    if (previewPlaying_ && !playbackService_.IsActive(previewHandle_)) {
        ReleasePreviewNamespace();
        previewPlaying_ = false;
        previewPaused_ = false;
        previewHandle_ = {};
    }
}
void VfxPreviewSession::BindSource(const VFX::VfxAssetData& asset, const AssetGuid& guid, bool runtimeChanged) {
    source_ = std::make_shared<const VFX::VfxAssetData>(asset);
    sourceGuid_ = guid;

    if (!runtimeChanged) {
        NotifyEmitterStructureChanged();
        return;
    }
    diagnostics_ = VFX::ValidateVfxAsset(*source_);
    NotifyEmitterStructureChanged();
    previewProgramDirty_ = true;
    if (previewPlaying_) {
        if (previewPaused_ && !previewSeekTarget_)
            previewSeekTarget_ = previewElapsedSeconds_;
        if (!previewRestartPending_)
            previewRestartDelaySeconds_ = 0.04f;
        previewRestartPending_ = true;
        // 継続ドラッグでも更新が無期限に延期されないよう、短い間隔でまとめる。
    }
}

bool VfxPreviewSession::CopyFrom(const VfxPreviewSession& source, std::string& message) {
    if (!source.source_) {
        message = "No VFX document is open";
        return false;
    }
    if (this == &source)
        return true;
    Deactivate(activePreviewViewId_);
    source_ = source.source_;
    sourceGuid_ = source.sourceGuid_;
    previewProgram_ = source.previewProgram_;
    previewProgramDirty_ = source.previewProgramDirty_;
    cameraController_.ResetFromCamera(source.cameraController_.GetCamera());
    previewInputOverrides_ = source.previewInputOverrides_;
    hiddenEmitterIds_ = source.hiddenEmitterIds_;
    soloEmitterId_ = source.soloEmitterId_;
    previewBypassAdjustments_ = source.previewBypassAdjustments_;
    previewCleanView_ = source.previewCleanView_;
    showPreviewGrid_ = source.showPreviewGrid_;
    showPreviewAxes_ = source.showPreviewAxes_;
    *previewEnvironment_ = *source.previewEnvironment_;
    *previewLighting_ = *source.previewLighting_;
    previewLightYaw_ = source.previewLightYaw_;
    previewLightIntensity_ = source.previewLightIntensity_;
    previewBackdrop_ = source.previewBackdrop_;
    return true;
}

const VFX::VfxEmitterData* VfxPreviewSession::FindEmitter(std::string_view id) const {
    if (source_)
        for (const auto& emitter : source_->emitters)
            if (emitter.id == id)
                return &emitter;
    return nullptr;
}
const VFX::VfxExposedInputData* VfxPreviewSession::FindInput(std::string_view id) const {
    if (source_)
        for (const auto& input : source_->inputs)
            if (input.id == id)
                return &input;
    return nullptr;
}
} // namespace HIKARI::EDITOR
