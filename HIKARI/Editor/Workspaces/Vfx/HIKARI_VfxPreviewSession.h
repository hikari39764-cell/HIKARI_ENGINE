#pragma once
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxRenderInputMailbox.h"
#include <cstdint>
#include <functional>
#include <json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace HIKARI {
struct SceneEnvironment;
}
namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}
namespace HIKARI::RENDER3D::LIGHTING {
struct SceneLightingSet;
}
namespace HIKARI::EDITOR {
struct EditorViewInstance;
class VfxWorkspaceController;
class VfxPreviewSession final {
public:
    using PreviewNamespaceRetireCallback = std::function<void(uint64_t)>;
    VfxPreviewSession();
    ~VfxPreviewSession();
    void Deactivate(RENDER3D::RenderViewId viewId);
    void BindSource(const VFX::VfxAssetData&, const AssetGuid&, bool runtimeChanged);
    bool CopyFrom(const VfxPreviewSession&, std::string& message);
    void AcknowledgePreviewRenderInput();
    float GetRequestedPreviewSeconds() const noexcept { return previewSeekTarget_.value_or(previewElapsedSeconds_); }
    RENDER3D::RenderViewId GetActivePreviewViewId() const noexcept { return activePreviewViewId_; }

    void ClearSoloEmitter() noexcept;
    nlohmann::json DescribeBackgroundPreview() const;
    bool FocusPreviewAsset(std::string_view emitterId = {});
    float GetPreviewElapsedSeconds() const noexcept;
    float GetPreviewLoopEndSeconds() const noexcept;
    float GetPreviewLoopStartSeconds() const noexcept;
    float GetPreviewPlaybackSpeed() const noexcept;
    float GetPreviewTimelineDuration() const noexcept;
    const std::string& GetSoloEmitterId() const noexcept;
    bool IsBackgroundPreviewSettled() const noexcept;
    bool IsEmitterHidden(std::string_view emitterId) const noexcept;
    bool IsEmitterPreviewVisible(std::string_view emitterId) const noexcept;
    bool IsPreviewLoopEnabled() const noexcept;
    bool IsPreviewPaused() const noexcept;
    bool IsPreviewPlaying() const noexcept;
    void NormalizePreviewTimelineRange() noexcept;

    void NotifyEmitterVisibilityChanged() noexcept;
    void PausePreview(bool paused);
    bool PlayPreview(std::string& outMessage);
    bool PrepareBackgroundPreview(EditorViewInstance& view, float timeSeconds, std::string& outMessage);

    void ResetPreviewInputs();
    bool RestartPreview(std::string& outMessage);

    bool SeekPreview(float seconds, std::string& outMessage);
    void SetEmitterHidden(std::string_view emitterId, bool hidden) noexcept;
    bool SetPreviewInput(std::string_view inputId, const VFX::VfxValue& value, std::string& outMessage);
    void SetPreviewLoopEnabled(bool enabled) noexcept;
    void SetPreviewLoopRange(float startSeconds, float endSeconds) noexcept;
    void SetPreviewNamespaceRetireCallback(PreviewNamespaceRetireCallback callback);
    void SetPreviewPlaybackSpeed(float speed) noexcept;
    void SetSoloEmitter(std::string_view emitterId) noexcept;
    bool StepPreviewFrame(int frameCount, std::string& outMessage);
    void StopPreview();
    void SubmitPreviewRequest(EditorViewInstance& previewView, bool canvasVisible);
    // 作業用の主光源（固定の太陽）を向きと明るさから作り直す。
    void UpdatePreviewKeyLight();
    void TickBackgroundPreview(EditorViewInstance& view, uint64_t frameIndex);
    void UpdatePreview(uint64_t frameIndex, float deltaSeconds);

private:
    friend class VfxWorkspaceController;
    std::vector<VFX::VfxParameterOverride> BuildPreviewParameters() const;
    bool PublishPreviewRenderInput(
        VFX::VfxRenderInputPublishMode mode = VFX::VfxRenderInputPublishMode::RequireAcknowledged);
    bool RebuildPreviewProgram(std::string& outMessage);
    void ReleasePreviewNamespace();
    bool SeekActivePreview(float seconds, std::string& outMessage, bool supersedePending = false);
    void NotifyEmitterStructureChanged() noexcept;
    const VFX::VfxEmitterData* FindEmitter(std::string_view id) const;
    const VFX::VfxExposedInputData* FindInput(std::string_view id) const;
    std::shared_ptr<const VFX::VfxAssetData> source_;
    AssetGuid sourceGuid_;
    std::string statusMessage_;
    VFX::VfxPlaybackService playbackService_{};
    VFX::VfxPlaybackHandle previewHandle_{};
    std::shared_ptr<const VFX::VfxProgram> previewProgram_{};
    VFX::VfxRenderInputMailbox previewRenderMailbox_{};
    std::unordered_map<std::string, VFX::VfxValue> previewInputOverrides_{};
    bool previewProgramDirty_ = true;
    uint64_t previewFrameIndex_ = 0u;
    float previewAccumulatedDeltaSeconds_ = 0.0f;
    float previewElapsedSeconds_ = 0.0f;
    std::optional<float> previewSeekTarget_{};
    float previewPlaybackSpeed_ = 1.0f;
    bool previewPlaying_ = false;
    bool previewPaused_ = false;
    // 停止中に同じ内容を毎フレーム描き直すと bloom がわずかに揺れる。
    // 変化があったフレームだけ再描画するためのマーカー。
    bool previewNeedsRedraw_ = true;
    bool backgroundFrozenPrepared_ = false;
    uint64_t previewContentRevision_ = 0u;
    bool previewRestartPending_ = false;
    float previewRestartDelaySeconds_ = 0.0f;
    bool previewLoopEnabled_ = true;
    bool previewLoopRangeInitialized_ = false;
    float previewLoopStartSeconds_ = 0.0f;
    float previewLoopEndSeconds_ = 5.0f;
    EditorDirectorCameraController cameraController_{};
    uint64_t cameraRevision_ = 1u;
    bool cameraCutPending_ = true;
    bool previewCleanView_ = false;
    bool previewBypassAdjustments_ = false;
    bool showPreviewGrid_ = true;
    bool showPreviewAxes_ = true;
    RENDER3D::RenderViewId activePreviewViewId_{};
    std::unique_ptr<RENDER3D::GPUDRIVEN::GpuDrivenSceneSource> emptySceneSource_{};
    std::unique_ptr<SceneEnvironment> previewEnvironment_{};
    std::unique_ptr<RENDER3D::LIGHTING::SceneLightingSet> previewLighting_{};
    // 预览の流体域の発光の回読（描画が足し、次に描く前に再生側へ渡す。流体の発光に付いて行く灯が読む）。
    std::shared_ptr<std::vector<RENDER3D::FLUID::FluidGlowReport>> previewGlowReports_ =
        std::make_shared<std::vector<RENDER3D::FLUID::FluidGlowReport>>();
    PreviewNamespaceRetireCallback previewNamespaceRetireCallback_{};
    float previewLightYaw_ = -35.0f;
    // 1 で表示の白に当たる照度の倍率。
    float previewLightIntensity_ = 2.0f;
    // 一様な背景の明るさ（表示参照）。
    float previewBackdrop_ = 0.025f;
    std::unordered_set<std::string> hiddenEmitterIds_{};
    std::string soloEmitterId_{};
    std::vector<VFX::VfxDiagnostic> diagnostics_{};
};
} // namespace HIKARI::EDITOR
