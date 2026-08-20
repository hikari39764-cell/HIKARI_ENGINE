#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_AutomaticExposure.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_ActionScreenImpulseProvider.h"
#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Vfx/Post/HIKARI_PostProcessingTypes.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Transition/HIKARI_TransitionProfile.h"

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
        void SetIntensity(float intensity);
        void SetCombo(float combo);

        void ClearEffects();
        void AddEffect(PostEffect* effect);
        bool SetGlobalProfile(
            const std::string& profileId,
            const DirectX::XMFLOAT4(&values)[16]);
        void ClearGlobalProfile();
        void SetBloomSettings(const BloomSettings& settings);
        void SetToneMappingSettings(const ToneMappingSettings& settings);
        void SetFxaaSettings(const FxaaSettings& settings);
        void SetSunGlareFrame(const SunGlareFrame& frame);
        void ClearSunGlareFrame();
        void SetActionScreenImpulseFrame(
            const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source,
            const MATH::Mat4& viewProj,
            uint64_t frameIndex);
        // Optional frame-local mask for weapon trails/impact accents.  The
        // composite copies this SRV into its own descriptor heap and never
        // retains the caller's resource beyond the current HDR resolve.
        using ActionScreenDistortionMask =
            ActionScreenImpulseProvider::Mask;
        void SetActionScreenDistortionMask(
            const ActionScreenDistortionMask& mask);
        void SetTransitionState(const TransitionVisualState& state);
        void ClearTransitionState();

        const FxaaSettings& GetFxaaSettings() const;
        const BloomDebugStats& GetBloomDebugStats() const;
        const SunGlareDebugStats& GetSunGlareDebugStats() const;
        const ActionScreenImpulseProvider::DebugState&
            GetActionScreenImpulseDebugState() const;
        const CommonParams& GetCommonParams() const;
        const RENDER3D::COLOR::ExposureFrameState&
            GetExposureState() const;
        const RENDER3D::COLOR::AutomaticExposureDebugState&
            GetAutomaticExposureDebugState() const;
        bool IsDumpRequested() const;
        void RequestFrameDump();

        RenderTarget2D* NormalizeTemporalOutput(
            RenderTarget2D& source,
            uint32_t width,
            uint32_t height,
            QuadDrawer& quad);
        RenderTarget2D* ResolveHdr(
            RenderTarget2D& source,
            QuadDrawer& quad,
            bool debugOutput,
            const char* temporalBackend,
            bool lightingEnabled,
            RenderTarget2D* sceneDepthOwner);
        RenderTarget2D* ResolveDebugLdr(
            RenderTarget2D& source,
            DXGI_FORMAT outputFormat,
            QuadDrawer& quad);
        RenderTarget2D* ResolveLdr(
            RenderTarget2D& source,
            DXGI_FORMAT outputFormat,
            QuadDrawer& quad,
            RenderTarget2D* lightTarget);

        std::string DumpState() const;

    private:
        bool EnsureToneMappingEffect();
        bool EnsureFxaaEffect();
        bool EnsureBloomEffects(uint32_t blurPairCount);
        bool EnsureSunGlareEffect();
        bool EnsureActionScreenImpulseEffect();
        bool EnsureSunGlareTarget(int width, int height);
        bool EnsureLdrTarget(int width, int height, DXGI_FORMAT format);
        bool EnsureTemporalOutput(int width, int height, DXGI_FORMAT format);
        bool ApplySunGlare(
            RenderTarget2D& source,
            RenderTarget2D& sceneDepthOwner,
            QuadDrawer& quad);
        RenderTarget2D* ApplyActionScreenImpulse(
            RenderTarget2D& source,
            QuadDrawer& quad);
        RenderTarget2D* ApplyBloom(RenderTarget2D& source, QuadDrawer& quad);
        RenderTarget2D* ApplyFxaa(RenderTarget2D& source, QuadDrawer& quad);
        void LogState(const char* reason) const;

        GFX::Context context_{};
        PostChain globalChain_{};
        PostChain bloomChain_{};
        PostChain transitionChain_{};
        PostChain fxaaChain_{};
        PostChain actionScreenImpulseChain_{};
        RenderTarget2D ldrTarget_{};
        RenderTarget2D temporalOutput_{};
        RenderTarget2D sunGlareTarget_{};
        CommonParams commonParams_{};
        float elapsedTime_ = 0.0f;

        std::string activeGlobalProfileId_{};
        PostProfile activeGlobalProfile_{};
        std::vector<std::unique_ptr<PostEffect>> activeGlobalEffects_{};
        std::vector<std::unique_ptr<PostEffect>> activeBloomEffects_{};
        BloomSettings bloomSettings_{};
        ToneMappingSettings toneMappingSettings_{};
        RENDER3D::COLOR::AutomaticExposureMeter automaticExposureMeter_{};
        RENDER3D::COLOR::ExposureFrameState exposureState_{};
        FxaaSettings fxaaSettings_{};
        BloomDebugStats bloomDebugStats_{};
        SunGlareFrame sunGlareFrame_{};
        SunGlareDebugStats sunGlareDebugStats_{};
        uint32_t activeBloomBlurPairCount_ = 0;
        std::unique_ptr<PostEffect> sunGlareEffect_{};
        CommonParams sunGlareParams_{};
        std::unique_ptr<PostEffect> toneMappingEffect_{};
        CommonParams toneMappingParams_{};
        std::unique_ptr<PostEffect> fxaaEffect_{};
        CommonParams fxaaParams_{};
        std::unique_ptr<PostEffect> actionScreenImpulseEffect_{};
        ActionScreenImpulseProvider actionScreenImpulseProvider_{};
        std::unique_ptr<CombatScreenDistortionComposite>
            actionScreenDistortionComposite_{};
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
