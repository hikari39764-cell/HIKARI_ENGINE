#pragma once
#include <span>
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"

#include <cstdint>
#include <array>
#include <limits>
#include <string_view>
#include <memory>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_AutomaticExposure.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_ActionScreenImpulseProvider.h"
#include "Vfx/Post/HIKARI_ColorGradingLut.h"
#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Vfx/Post/HIKARI_PostProcessingTypes.h"
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Vfx/Transition/HIKARI_TransitionProfile.h"

namespace HIKARI {
class AssetRegistry;
} // namespace HIKARI
namespace HIKARI::POST {

class PostEffect;
class QuadDrawer;
class CombatScreenDistortionComposite;

class PostProcessingStage {
public:
    PostProcessingStage();
    ~PostProcessingStage();

    void UpdateContext(const GFX::Context& context);
    void Shutdown();
    void UpdateCommonParams(float deltaTime, int width, int height);
    // フレームの露出を一度だけ進める（P と P_prev が決まる）。HDR へ書く前に呼ぶ。illumination は場面を照らす光の量。
    // meterScene が false のフレームは主ビューの画面を測らない。
    void AdvanceExposure(float deltaTime, const RENDER3D::COLOR::ExposureIllumination& illumination, bool meterScene);
    void SetIntensity(float intensity);
    void SetSequenceEffects(std::span<const SEQUENCER::ScreenEffectSample> effects);
    void SetCombo(float combo);

    // 全体後処理の層を並びの順に重ねる。同じ効果の層は前のフレームの pass を引き継ぐ。
    bool SetGlobalEffects(std::span<const ResolvedPostLayer> layers, const AssetRegistry* registry = nullptr);
    bool RequiresSurfaceFeatureMask() const noexcept;
    bool SubmitEffectParameter(std::string_view effectId, uint32_t index, uint32_t channelMask,
                               const DirectX::XMFLOAT4& value, int32_t priority, uint64_t sourceId);
    void ClearGlobalEffects();
    bool SetActionPostFrame(uint64_t worldId, uint64_t frameIndex, std::string_view effectId,
                            std::shared_ptr<const PostEffectDefinition> effect, const PostParameterBlock& values,
                            const AssetRegistry& registry);
    void ClearActionPostFrame(uint64_t worldId = 0u);
    void SetActionPostView(uint64_t worldId, bool primaryGameView) noexcept;
    bool RequiresActionSurfaceFeatureMask() const noexcept;
    void GetRadialScreenWarp(float& curvature, float& strength) const noexcept;
    void SetBloomSettings(const BloomSettings& settings);
    void SetToneMappingSettings(const ToneMappingSettings& settings);
    void SetColorGrading(const ColorGradingSettings& settings, std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> lut);
    ID3D12Resource* GetColorGradingLut() const noexcept { return colorGradingLut_.GetCurrent(); }
    void SetExposureSettings(const RENDER3D::COLOR::ExposureSettings& settings);
    void SetFxaaSettings(const FxaaSettings& settings);
    void SetSunGlareFrame(const SunGlareFrame& frame);
    void ClearSunGlareFrame();
    void SetActionScreenImpulseFrame(const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source,
                                     const MATH::Mat4& viewProj, uint64_t frameIndex);
    // Optional frame-local mask for weapon trails/impact accents.  The
    // composite copies this SRV into its own descriptor heap and never
    // retains the caller's resource beyond the current HDR resolve.
    using ActionScreenDistortionMask = ActionScreenImpulseProvider::Mask;
    void SetActionScreenDistortionMask(const ActionScreenDistortionMask& mask);
    // Frame-local scene-linear HDR accent emitted by the same weapon
    // path geometry as the distortion mask.
    using ActionScreenDistortionAccent = ActionScreenImpulseProvider::Mask;
    void SetActionScreenDistortionAccent(const ActionScreenDistortionAccent& accent);
    void SetTransitionState(const TransitionVisualState& state);
    void ClearTransitionState();

    const FxaaSettings& GetFxaaSettings() const;
    const BloomDebugStats& GetBloomDebugStats() const;
    const SunGlareDebugStats& GetSunGlareDebugStats() const;
    const ActionScreenImpulseProvider::DebugState& GetActionScreenImpulseDebugState() const;
    const RENDER3D::COLOR::ExposureFrameState& GetExposureState() const;
    // 測光を持たない用途（ツールのプレビュー）が、描画に使ったビューの露出を渡す。
    void SetExposureFrame(const RENDER3D::COLOR::ExposureFrameState& state) noexcept { exposureState_ = state; }
    const RENDER3D::COLOR::AutomaticExposureDebugState& GetAutomaticExposureDebugState() const;
    void RequestFrameDump();

    RenderTarget2D* NormalizeTemporalOutput(RenderTarget2D& source, uint32_t width, uint32_t height, QuadDrawer& quad);
    RenderTarget2D* ResolveHdr(RenderTarget2D& source, QuadDrawer& quad, bool debugOutput, const char* temporalBackend,
                               bool lightingEnabled, RenderTarget2D* sceneDepthOwner,
                               const RENDER3D::RenderTextureView* surfaceFeatureMask = nullptr,
                               RenderTarget2D* preservedVfx = nullptr);
    // Bloom だけを適用して source へ加算合成する。エディタのツールプレビュー
    // 用: 本編と同じ bloom を見せつつ、ゲーム状態に紐づく後処理
    // (画面衝撃・自動露出計測・サングレア・遷移) を引き込まない。
    // 毎フレーム同じ入力なら同じ結果になる (プレビューのちらつき防止)。
    RenderTarget2D* ResolveHdrBloomOnly(RenderTarget2D& source, QuadDrawer& quad);
    RenderTarget2D* ResolveDebugLdr(RenderTarget2D& source, DXGI_FORMAT outputFormat, QuadDrawer& quad);
    RenderTarget2D* ResolveLdr(RenderTarget2D& source, DXGI_FORMAT outputFormat, QuadDrawer& quad,
                               RenderTarget2D* lightTarget);

    std::string DumpState() const;

private:
    struct ActiveGlobalLayer {
        std::string effectId;
        std::shared_ptr<const PostEffectDefinition> effect;
        std::vector<std::unique_ptr<PostEffect>> passes;
        PostParameterBlock values{};
    };
    // phase の層を並びの順に通す。特性マスクや深度を要る層は、それが無いフレームには飛ばす。
    RenderTarget2D* ExecuteGlobalLayers(PostEffectPhase phase, RenderTarget2D* output, QuadDrawer& quad,
                                        RenderTarget2D* sceneDepthOwner,
                                        const RENDER3D::RenderTextureView* surfaceFeatureMask);
    // 層のパラメーターに、このフレームの SDK の一時上書き（同じ効果の層すべて）を重ねる。
    CommonParams ResolveGlobalFrameParams(const ActiveGlobalLayer& layer) const noexcept;
    bool IsActionPostCurrent() const noexcept;
    RenderTarget2D* ApplyActionPost(RenderTarget2D& source, QuadDrawer& quad,
                                    const RENDER3D::RenderTextureView* surfaceFeatureMask);
    struct EffectChannelOverride {
        float value = 0.f;
        int32_t priority = 0;
        uint64_t sourceId = 0u;
    };
    std::array<std::array<EffectChannelOverride, 4>, kPostParameterSlotCount> effectOverrides_{};
    uint64_t effectOverrideFrame_ = (std::numeric_limits<uint64_t>::max)();
    std::string effectOverrideId_;
    bool EnsureToneMappingEffect();
    bool EnsureFxaaEffect();
    bool EnsureBloomEffects(uint32_t levelCount);
    bool EnsureSunGlareEffect();
    bool EnsureActionScreenImpulseEffect();
    bool EnsureSunGlareTarget(int width, int height);
    bool EnsureLdrTarget(int width, int height, DXGI_FORMAT format);
    bool EnsureTemporalOutput(int width, int height, DXGI_FORMAT format);
    bool ApplySunGlare(RenderTarget2D& source, RenderTarget2D& sceneDepthOwner, QuadDrawer& quad);
    RenderTarget2D* ApplyActionScreenImpulse(RenderTarget2D& source, QuadDrawer& quad);
    RenderTarget2D* ApplyBloom(RenderTarget2D& source, QuadDrawer& quad);
    RenderTarget2D* ApplyFxaa(RenderTarget2D& source, QuadDrawer& quad);
    void LogState(const char* reason) const;

    GFX::Context context_{};
    PostChain actionPostChain_{};
    std::vector<std::unique_ptr<PostEffect>> actionPostPasses_{};
    std::shared_ptr<const PostEffectDefinition> actionPostEffect_{};
    std::string actionPostEffectId_;
    PostParameterBlock actionPostValues_{};
    const AssetRegistry* actionPostRegistry_ = nullptr;
    uint64_t actionPostRegistryId_ = 0u;
    uint64_t actionPostRegistryRevision_ = 0u;
    uint64_t actionPostWorld_ = 0u;
    uint64_t actionPostViewWorld_ = 0u;
    uint64_t actionPostFrame_ = (std::numeric_limits<uint64_t>::max)();
    uint64_t actionPostDeviceGeneration_ = 0u;
    PostChain globalChain_{};
    // Bloom のピラミッドの各段（縮小した結果を段ごとに持つ）。
    std::array<PostChain, 6> bloomLevels_{};
    RenderTarget2D* ApplySequenceEffects(RenderTarget2D& source, QuadDrawer& quad);
    PostChain sequenceChain_{};
    std::unique_ptr<PostEffect> sequenceEffect_{};
    std::vector<SEQUENCER::ScreenEffectSample> sequenceEffects_;
    uint64_t sequenceEffectsFrame_ = 0;
    PostChain transitionChain_{};
    PostChain fxaaChain_{};
    PostChain actionScreenImpulseChain_{};
    RenderTarget2D ldrTarget_{};
    RenderTarget2D temporalOutput_{};
    RenderTarget2D sunGlareTarget_{};
    CommonParams commonParams_{};
    float elapsedTime_ = 0.0f;

    std::vector<ActiveGlobalLayer> activeGlobalLayers_{};
    // Display の層が読む、このフレームの HDR 解決で渡された深度と特性マスク。ResolveLdr が使って消す。
    RenderTarget2D* displaySceneDepthOwner_ = nullptr;
    const RENDER3D::RenderTextureView* displaySurfaceFeatureMask_ = nullptr;
    uint64_t activeGlobalRegistryId_ = 0u;
    uint64_t activeGlobalRegistryRevision_ = 0u;
    std::unique_ptr<PostEffect> bloomDownsampleEffect_{};
    std::unique_ptr<PostEffect> bloomUpsampleEffect_{};
    BloomSettings bloomSettings_{};
    ToneMappingSettings toneMappingSettings_{};
    RENDER3D::COLOR::ExposureSettings exposureSettings_{};
    RENDER3D::COLOR::AutomaticExposureMeter automaticExposureMeter_{};
    RENDER3D::COLOR::ExposureFrameState exposureState_{};
    bool meterScene_ = true;
    FxaaSettings fxaaSettings_{};
    BloomDebugStats bloomDebugStats_{};
    SunGlareFrame sunGlareFrame_{};
    SunGlareDebugStats sunGlareDebugStats_{};
    uint32_t activeBloomLevelCount_ = 0;
    std::unique_ptr<PostEffect> sunGlareEffect_{};
    std::unique_ptr<PostEffect> toneMappingEffect_{};
    ColorGradingLut colorGradingLut_{};
    std::unique_ptr<PostEffect> fxaaEffect_{};
    std::unique_ptr<PostEffect> actionScreenImpulseEffect_{};
    ActionScreenImpulseProvider actionScreenImpulseProvider_{};
    std::unique_ptr<CombatScreenDistortionComposite> actionScreenDistortionComposite_{};
    uint32_t lastActionProjectionRejectReason_ = 0u;
    bool actionCompositeFailureLogged_ = false;
    bool actionScreenRequestWasActive_ = false;
    bool dumpNextFrame_ = false;
    bool transitionActive_ = false;
    std::string activeTransitionProfileId_{};
    TransitionProfile activeTransitionProfile_{};
    std::unique_ptr<PostEffect> transitionEffect_{};
    CommonParams transitionParams_{};
};

} // namespace HIKARI::POST
