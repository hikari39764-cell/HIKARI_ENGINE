#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Scene/Vfx/HIKARI_VfxScreenOutput.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackTypes.h"

namespace HIKARI::VFX {

inline constexpr size_t kMaximumActiveVfxPlaybackInstances =
    RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame;
inline constexpr size_t kMaximumActiveVfxPlaybackEmitters =
    RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame;
inline constexpr size_t kMaximumParallelVfxInstancesPerSlot = 64u;

struct VfxPlaybackAdmissionStats {
    size_t activeInstances = 0u;
    size_t activeEmitterInstances = 0u;
    uint64_t acceptedPlayRequests = 0u;
    uint64_t budgetRejections = 0u;
};

struct VfxSeekCapacity {
    bool handleActive = false;
    float currentSeconds = 0.0f;
    uint32_t emitterCount = 0u;
    uint32_t remainingSubmissions = 0u;
    uint32_t maximumSteps = 0u;
};

using VfxProgramResolver = std::function<std::shared_ptr<const VfxProgram>(const AssetGuid&, std::string&)>;

class VfxPlaybackService final {
public:
    explicit VfxPlaybackService(uint64_t playbackNamespace = 0u);
    ~VfxPlaybackService();

    void SetProgramResolver(VfxProgramResolver resolver);
    void SetAttachmentResolver(VfxAttachmentResolver resolver);
    void SetReliableRenderDeliveryEnabled(bool enabled) noexcept;
    uint64_t GetPlaybackNamespace() const noexcept;

    // Resolves a typed compiled special output by stable asset GUID and
    // output id. The returned value is copied so callers never retain a
    // pointer into an evictable program cache. Empty/mismatched ids fail
    // closed; legacy numeric presentation APIs do not use this path.
    bool ResolveSpecialOutput(const AssetGuid& assetGuid, std::string_view outputId, VfxSpecialOutputType expectedType,
                              VfxSpecialOutputData& outOutput) const;

    VfxPlayResult PlayDetailed(const VfxPlayRequest& request);
    bool Stop(VfxPlaybackHandle handle);
    bool SetTimeScale(VfxPlaybackHandle handle, float scale);
    // Runtime cues queue bounded preroll until the previous GPU delivery is acknowledged.
    bool QueueSeek(VfxPlaybackHandle handle, float seconds);
    bool SetTransform(VfxPlaybackHandle handle, const VfxWorldTransform& transform);
    bool SetParameters(VfxPlaybackHandle handle, const std::vector<VfxParameterOverride>& parameters,
                       std::vector<VfxPlaybackDiagnostic>* outDiagnostics = nullptr);
    VfxSeekResult Seek(VfxPlaybackHandle handle, uint64_t frameIndex, float targetSeconds,
                       float fixedStepSeconds = 1.0f / 60.0f,
                       uint32_t maximumSteps = RENDER3D::PARTICLES::kMaximumVfxSeekSubsteps);
    VfxSeekCapacity GetSeekCapacity(VfxPlaybackHandle handle, uint64_t frameIndex) const noexcept;

    void Update(uint64_t frameIndex, float deltaSeconds);
    const RENDER3D::PARTICLES::ParticleRenderInput& GetRenderInput() const noexcept;
    // 現在の描画入力から「時間を進めない」スナップショットを配信する。
    // 一時停止中のプレビューが同じ入力を毎フレーム再描画しても粒子が
    // 動き続けないようにするため (エディタのちらつき対策)。
    bool PublishFrozenRenderInput() noexcept;
    bool AcknowledgeRenderInput(RENDER3D::PARTICLES::ParticleRenderDeliveryToken token) noexcept;
    bool HasPendingRenderInput() const noexcept;
    bool IsActive(VfxPlaybackHandle handle) const noexcept;
    bool GetLayerPoseAtTime(VfxPlaybackHandle handle, std::string_view layerId, float seconds, VfxWorldTransform& world,
                            VfxWorldTransform& animationFrame) const;
    bool GetLayerPose(VfxPlaybackHandle handle, std::string_view layerId, VfxWorldTransform& world,
                      VfxWorldTransform& animationFrame) const;
    size_t GetActiveInstanceCount() const noexcept;
    const VfxPlaybackAdmissionStats& GetAdmissionStats() const noexcept;
    // 有効な Light 特殊出力を world 空間のサンプルとして書き出す。
    // 呼び出し側はそのフレームの照明セットへ渡すだけで、GPU 資源も
    // ライフタイムも持たない。
    size_t CollectLightOutputs(std::vector<VfxLayerLightSample>& outSamples) const;
    // 有効な ScreenImpulse 特殊出力を画面波バッチとして書き出す。
    size_t CollectScreenWaves(VfxScreenWaveBatch& outBatch) const;
    // 有効な Mesh 出力レイヤーを world サンプルとして書き出す。
    size_t CollectMeshLayers(std::vector<VfxLayerMeshSample>& outSamples) const;
    void Reset();

private:
    VfxSeekResult SeekInternal(VfxPlaybackHandle handle, uint64_t frameIndex, float targetSeconds,
                               float fixedStepSeconds, uint32_t maximumSteps, bool publish);
    struct HandleSlot;
    struct EmitterState;
    struct Instance;

    VfxPlaybackHandle AllocateHandle(size_t instanceIndex);
    void ReleaseHandle(VfxPlaybackHandle handle) noexcept;
    Instance* Find(VfxPlaybackHandle handle) noexcept;
    const Instance* Find(VfxPlaybackHandle handle) const noexcept;
    void RetireInstanceAt(size_t index);
    void ProjectComposition(const Instance& instance, float submittedAge);
    void ProjectAdjustmentParameters(const Instance&, const VfxProgramEmitter&, float submittedAge,
                                     RENDER3D::PARTICLES::ParticleEmitterRenderParameters&);
    void RetireEmitterKeys(const Instance& instance);
    void ResetInstanceSimulation(Instance& instance) noexcept;
    size_t AppendInstanceStep(Instance& instance, float deltaSeconds, uint32_t simulationOrdinal,
                              bool drawAfterSimulation, bool wrapLoop);
    bool ApplyParameters(Instance& instance, const std::vector<VfxParameterOverride>& parameters,
                         std::vector<VfxPlaybackDiagnostic>* outDiagnostics);
    void PublishRenderInputDelivery() noexcept;
    // 接続ターゲットの変換を適用する。全レイヤーの変換が確定した後に
    // 一度だけ呼ぶ。
    void ApplyConnectTarget(Instance& instance, uint32_t emitterIndex) const noexcept;

    uint64_t playbackNamespace_ = 0u;
    VfxProgramResolver resolver_{};
    VfxAttachmentResolver attachmentResolver_{};
    uint64_t lastUpdateFrame_ = 0u;
    bool hasUpdatedFrame_ = false;
    std::vector<HandleSlot> handleSlots_{};
    std::vector<uint32_t> freeHandleSlots_{};
    std::vector<Instance> instances_{};
    std::vector<RENDER3D::PARTICLES::ParticleEmitterInstanceKey> pendingRetiredEmitters_{};
    RENDER3D::PARTICLES::ParticleRenderInput renderInput_{};
    bool reliableRenderDeliveryEnabled_ = false;
    uint64_t nextRenderDeliverySequence_ = 0u;
    uint64_t pendingRenderDeliverySequence_ = 0u;
    float pendingRenderDeltaSeconds_ = 0.0f;
    uint64_t seekBatchFrameIndex_ = (std::numeric_limits<uint64_t>::max)();
    uint32_t nextSeekSimulationOrdinal_ = 0u;
    VfxPlaybackAdmissionStats admissionStats_{};
};

} // namespace HIKARI::VFX
