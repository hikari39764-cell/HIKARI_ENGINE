#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Editor/Authoring/HIKARI_VfxAssetAuthoringService.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {

    bool VfxWorkspaceController::RefreshPresetPreviewMaterials(
        const AssetRegistry& assetRegistry,
        std::string& outMessage) {

        const auto& recipes = document_.GetPendingPresetMaterials();
        if (recipes.empty()) {
            presetPreviewMaterials_.clear();
            presetPreviewMaterialRecipes_.clear();
            presetPreviewMaterialRegistryInstanceId_ = 0u;
            presetPreviewMaterialRegistryRevision_ = 0u;
            outMessage.clear();
            return true;
        }
        if (presetPreviewMaterialRegistryInstanceId_ ==
                assetRegistry.GetInstanceId() &&
            presetPreviewMaterialRegistryRevision_ ==
                assetRegistry.GetRevision() &&
            presetPreviewMaterialRecipes_ == recipes &&
            presetPreviewMaterials_.size() == recipes.size()) {
            outMessage.clear();
            return true;
        }

        std::unordered_map<std::string, std::shared_ptr<const Material>>
            rebuilt{};
        rebuilt.reserve(recipes.size());
        MaterialRuntimeBuilder runtimeBuilder{};
        for (const VFX::PRESETS::VfxPresetMaterialRecipe& recipe :
             recipes) {
            if (recipe.emitterId.empty() ||
                rebuilt.contains(recipe.emitterId)) {
                outMessage =
                    "VFX preset preview contains duplicate or empty emitter material bindings";
                return false;
            }
            if (assetRegistry.FindAs<TextureAssetDescriptor>(
                    AssetId{ recipe.textureAssetId.value }) == nullptr) {
                outMessage =
                    "VFX preset preview texture is not registered: " +
                    recipe.textureAssetId.value;
                return false;
            }

            MaterialAssetData source{};
            if (!BuildVfxPresetMaterialSource(
                    recipe, source, outMessage)) {
                return false;
            }
            auto runtimeMaterial = std::make_shared<Material>();
            RENDER3D::MATERIAL::MaterialProgramResolution resolution{};
            if (!runtimeBuilder.BuildRuntimeMaterial(
                    source,
                    assetRegistry,
                    RENDER3D::MATERIAL::
                        GetRuntimeMaterialProgramRegistry(),
                    *runtimeMaterial,
                    "VFX preset preview/" +
                        recipe.textureAssetId.value + "/" +
                        recipe.emitterId,
                    &resolution) ||
                resolution.effectiveProgram.domain !=
                    MaterialDomain::Particle) {
                outMessage =
                    "Failed to build Particle-domain preview material for emitter " +
                    recipe.emitterId;
                return false;
            }
            rebuilt.emplace(
                recipe.emitterId,
                std::move(runtimeMaterial));
        }

        presetPreviewMaterials_ = std::move(rebuilt);
        presetPreviewMaterialRecipes_ = recipes;
        presetPreviewMaterialRegistryInstanceId_ =
            assetRegistry.GetInstanceId();
        presetPreviewMaterialRegistryRevision_ =
            assetRegistry.GetRevision();
        outMessage.clear();
        return true;
    }

    void VfxWorkspaceController::SetPreviewPlaybackSpeed(
        float speed) noexcept {

        previewPlaybackSpeed_ = std::clamp(
            std::isfinite(speed) ? speed : 1.0f,
            0.1f,
            4.0f);
    }

    void VfxWorkspaceController::NormalizePreviewTimelineRange() noexcept {
        constexpr float kMinimumRange = 1.0f / 60.0f;
        const float duration = document_.IsOpen()
            ? (std::max)(
                document_.Asset().durationSeconds,
                kMinimumRange)
            : kMinimumRange;
        if (!previewLoopRangeInitialized_) {
            previewLoopEnabled_ = document_.IsOpen()
                ? document_.Asset().loop
                : true;
            previewLoopStartSeconds_ = 0.0f;
            previewLoopEndSeconds_ = duration;
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
    }

    void VfxWorkspaceController::SetPreviewLoopRange(
        float startSeconds,
        float endSeconds) noexcept {

        constexpr float kMinimumRange = 1.0f / 60.0f;
        const float duration = document_.IsOpen()
            ? (std::max)(
                document_.Asset().durationSeconds,
                kMinimumRange)
            : kMinimumRange;
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
        previewRestartPending_ = false;
        if (!SeekActivePreview(
                previewLoopStartSeconds_, outMessage)) {
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
        if (!paused && previewLoopEnabled_ &&
            previewElapsedSeconds_ >= previewLoopEndSeconds_ - 1e-5f) {
            previewRestartPending_ = true;
        }
        statusMessage_ = paused
            ? "VFX preview paused"
            : "VFX preview resumed";
    }

    void VfxWorkspaceController::StopPreview() {
        NormalizePreviewTimelineRange();
        if (previewHandle_.IsValid()) {
            (void)playbackService_.Stop(previewHandle_);
            playbackService_.Update(previewFrameIndex_, 0.0f);
            PublishPreviewRenderInput();
        }
        ReleasePreviewNamespace();
        previewHandle_ = {};
        previewPlaying_ = false;
        previewPaused_ = false;
        previewRestartPending_ = false;
        previewElapsedSeconds_ = previewLoopStartSeconds_;
    }

    bool VfxWorkspaceController::RestartPreview(
        std::string& outMessage) {

        StopPreview();
        return PlayPreview(outMessage);
    }

    bool VfxWorkspaceController::SeekActivePreview(
        float seconds,
        std::string& outMessage) {

        if (!previewHandle_.IsValid()) {
            outMessage = "VFX preview is not active";
            return false;
        }

        constexpr float kNominalFixedStep = 1.0f / 60.0f;
        const float duration = (std::max)(
            document_.Asset().durationSeconds,
            kNominalFixedStep);
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
        if (simulationSpan >
            static_cast<float>(capacity.maximumSteps)) {
            outMessage =
                "VFX preview seek exceeds the safe per-frame simulation budget";
            return false;
        }
        const float fixedStep = simulationSpan <= kSeekEpsilon
            ? kNominalFixedStep
            : (std::max)(
                kNominalFixedStep,
                simulationSpan /
                    static_cast<float>(capacity.maximumSteps));
        const VFX::VfxSeekResult result = playbackService_.Seek(
            previewHandle_,
            previewFrameIndex_,
            target,
            fixedStep,
            capacity.maximumSteps);
        if (!result.accepted || !result.complete) {
            outMessage = result.diagnostics.empty()
                ? "VFX preview seek did not complete atomically"
                : result.diagnostics.front().message;
            return false;
        }
        previewElapsedSeconds_ = result.currentSeconds;
        PublishPreviewRenderInput();
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
        if (!SeekActivePreview(seconds, outMessage)) {
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
            outMessage = result.diagnostics.empty()
                ? "VFX preview compile failed"
                : result.diagnostics.front().message;
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
        bool simulationPublished = false;
        if (previewRestartPending_ && previewPlaying_) {
            const bool wasPaused = previewPaused_;
            std::string message{};
            if (!RestartPreview(message)) {
                statusMessage_ = message;
            } else if (wasPaused) {
                PausePreview(true);
            }
            simulationPublished = previewPlaying_;
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
                simulationDelta = std::clamp(
                    deltaSeconds * previewPlaybackSpeed_,
                    0.0f,
                    0.1f);
                if (previewLoopEnabled_) {
                    simulationDelta = (std::min)(
                        simulationDelta,
                        (std::max)(
                            previewLoopEndSeconds_ -
                                previewElapsedSeconds_,
                            0.0f));
                }
            }
            playbackService_.Update(frameIndex, simulationDelta);
            if (previewPlaying_ && !previewPaused_) {
                previewElapsedSeconds_ = (std::min)(
                    previewElapsedSeconds_ + simulationDelta,
                    previewLoopEndSeconds_);
            }
            PublishPreviewRenderInput();
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

    void VfxWorkspaceController::PublishPreviewRenderInput() {
        RENDER3D::PARTICLES::ParticleRenderInput input =
            playbackService_.GetRenderInput();
        if (!presetPreviewMaterials_.empty() && document_.IsOpen()) {
            const auto& emitters = document_.Asset().emitters;
            for (RENDER3D::PARTICLES::ParticleEmitterSubmission& submission :
                 input.emitters) {
                const std::size_t index = submission.key.emitterIndex;
                if (index >= emitters.size() ||
                    !submission.render.materialAssetGuid.empty()) {
                    continue;
                }
                const auto material = presetPreviewMaterials_.find(
                    emitters[index].id);
                if (material != presetPreviewMaterials_.end()) {
                    submission.render.runtimeMaterialOverride =
                        material->second;
                }
            }
        }
        if (!hiddenEmitterIds_.empty() || !soloEmitterId_.empty()) {
            const auto& emitters = document_.IsOpen()
                ? document_.Asset().emitters
                : std::vector<VFX::VfxEmitterData>{};
            std::erase_if(
                input.emitters,
                [this, &emitters](
                    const RENDER3D::PARTICLES::ParticleEmitterSubmission&
                        submission) {
                    const std::size_t index =
                        submission.key.emitterIndex;
                    if (index >= emitters.size()) return false;
                    return !IsEmitterPreviewVisible(emitters[index].id);
                });
        }
        previewRenderInput_ = std::make_shared<const
            RENDER3D::PARTICLES::ParticleRenderInput>(std::move(input));
        ++previewRenderRevision_;
        if (previewRenderRevision_ == 0u) {
            previewRenderRevision_ = 1u;
        }
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
        request.viewId = previewView.renderViewId;
        request.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
        request.cameraFrame.camera = cameraController_.GetCamera();
        request.cameraFrame.revision = cameraRevision_;
        request.cameraFrame.cameraCut = cameraCutPending_;
        request.cameraFrame.valid = true;
        request.width = previewView.extent.width;
        request.height = previewView.extent.height;
        request.sceneRevision = previewRenderRevision_;
        request.visible = canvasVisible;
        request.drawDebug = false;
        request.schedulingPriority = 110u;
        request.shadingMode =
            RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Neutral;
        request.displayExposure =
            previewView.visualization.displayExposure;
        request.sceneSourceOverride = emptySceneSource_.get();
        request.environmentOverride = previewEnvironment_.get();
        request.lightingOverride = previewLighting_.get();
        request.particleInputOverride = previewRenderInput_;
        request.guideOverlay.showGroundGrid = showPreviewGrid_;
        request.guideOverlay.showWorldAxes = showPreviewAxes_;
        request.guideOverlay.gridHalfCount = 12;
        request.guideOverlay.gridSpacing = 0.5f;
        request.guideOverlay.axisLength = 1.25f;
        request.suppressSpatialLightingProviders = true;
        request.useDefaultSkyIbl = false;
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
