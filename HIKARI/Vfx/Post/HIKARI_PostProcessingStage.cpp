#include "Vfx/Post/HIKARI_PostProcessingStage.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>

#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Vfx/Post/HIKARI_CombatScreenDistortionComposite.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {
    namespace {
        void WriteCombatVfxLog(const std::string& message) noexcept {
            try {
                DEBUGLOG::WriteRenderLogLine(message);
            }
            catch (...) {
            }
        }
    }

    PostProcessingStage::PostProcessingStage()
        : actionScreenDistortionComposite_(
            std::make_unique<CombatScreenDistortionComposite>()) {}
    PostProcessingStage::~PostProcessingStage() = default;

    void PostProcessingStage::UpdateContext(const GFX::Context& context) {
        context_ = context;
        PostEffect::UpdateContext(context);
        globalChain_.UpdateContext(context);
        bloomChain_.UpdateContext(context);
        transitionChain_.UpdateContext(context);
        fxaaChain_.UpdateContext(context);
        actionScreenImpulseChain_.UpdateContext(context);
        if (!actionScreenDistortionComposite_) {
            actionScreenDistortionComposite_ =
                std::make_unique<CombatScreenDistortionComposite>();
        }
        actionScreenDistortionComposite_->UpdateContext(context);
        ldrTarget_.UpdateContext(context);
        temporalOutput_.UpdateContext(context);
        sunGlareTarget_.UpdateContext(context);
        automaticExposureMeter_.UpdateContext(context);
        globalChain_.SetDebugName("PostProcessing.Global");
        bloomChain_.SetDebugName("PostProcessing.Bloom");
        transitionChain_.SetDebugName("PostProcessing.TransitionHDR");
        fxaaChain_.SetDebugName("PostProcessing.FXAA");
        actionScreenImpulseChain_.SetDebugName(
            "PostProcessing.ActionScreenImpulse");
    }

    void PostProcessingStage::Shutdown() {
        ClearGlobalProfile();
        ClearTransitionState();
        globalChain_.Finalize();
        bloomChain_.Finalize();
        transitionChain_.Finalize();
        fxaaChain_.Finalize();
        actionScreenImpulseChain_.Finalize();
        activeBloomEffects_.clear();
        actionScreenImpulseEffect_.reset();
        if (actionScreenDistortionComposite_) {
            actionScreenDistortionComposite_->Shutdown();
        }
        sunGlareEffect_.reset();
        toneMappingEffect_.reset();
        fxaaEffect_.reset();
        ldrTarget_.Finalize();
        temporalOutput_.Finalize();
        sunGlareTarget_.Finalize();
        automaticExposureMeter_.Shutdown();
        bloomDebugStats_ = {};
        sunGlareFrame_ = {};
        sunGlareDebugStats_ = {};
        activeBloomBlurPairCount_ = 0;
        actionScreenImpulseProvider_.BeginFrame(0u);
        actionScreenImpulseProvider_.MutableDebugState() = {};
        lastActionProjectionRejectReason_ = 0u;
        actionCompositeFailureLogged_ = false;
        actionScreenRequestWasActive_ = false;
        dumpNextFrame_ = false;
    }

    void PostProcessingStage::UpdateCommonParams(
        float deltaTime,
        int width,
        int height) {
        commonParams_.resolutionX = static_cast<float>((std::max)(1, width));
        commonParams_.resolutionY = static_cast<float>((std::max)(1, height));
        commonParams_.deltaTime = deltaTime;
        elapsedTime_ += deltaTime;
        commonParams_.time = elapsedTime_;
        actionScreenImpulseProvider_.BeginFrame(
            TIME::GetFrameContext().frameIndex);
        exposureState_ = automaticExposureMeter_.BeginFrame(
            toneMappingSettings_.exposureMode,
            toneMappingSettings_.exposure,
            toneMappingSettings_.automaticExposure,
            deltaTime);
    }

    void PostProcessingStage::SetIntensity(float intensity) {
        commonParams_.intensity = intensity;
    }

    void PostProcessingStage::SetCombo(float combo) {
        commonParams_.combo = combo;
    }

    void PostProcessingStage::ClearEffects() {
        globalChain_.Clear();
    }

    void PostProcessingStage::AddEffect(PostEffect* effect) {
        globalChain_.Add(effect);
    }

    bool PostProcessingStage::SetGlobalProfile(
        const std::string& profileId,
        const DirectX::XMFLOAT4(&values)[16]) {
        if (profileId.empty()) {
            ClearGlobalProfile();
            return false;
        }

        const bool rebuild =
            activeGlobalProfileId_ != profileId || activeGlobalEffects_.empty();
        if (rebuild) {
            PostProfile profile{};
            if (!PostProfile::LoadById(profileId, profile)) {
                return false;
            }
            std::vector<std::unique_ptr<PostEffect>> effects{};
            effects.reserve(profile.passes.size());
            for (const auto& pass : profile.passes) {
                auto effect = std::make_unique<PostEffect>();
                if (!effect->LoadBuiltinPixelShader(
                        pass.programId,
                        pass.entryId)) {
                    return false;
                }
                effects.push_back(std::move(effect));
            }
            globalChain_.Clear();
            activeGlobalEffects_ = std::move(effects);
            for (const auto& effect : activeGlobalEffects_) {
                globalChain_.Add(effect.get());
            }
            activeGlobalProfile_ = std::move(profile);
            activeGlobalProfileId_ = profileId;
        }

        activeGlobalProfile_.ResetValuesFromDefaults();
        for (size_t i = 0; i < activeGlobalProfile_.values.size(); ++i) {
            activeGlobalProfile_.values[i] = values[i];
        }
        activeGlobalProfile_.ApplyToCommonParams(commonParams_);
        return globalChain_.HasAny();
    }

    void PostProcessingStage::ClearGlobalProfile() {
        activeGlobalProfileId_.clear();
        activeGlobalProfile_ = PostProfile{};
        globalChain_.Clear();
        activeGlobalEffects_.clear();
    }

    void PostProcessingStage::SetBloomSettings(const BloomSettings& settings) {
        bloomSettings_ = settings;
        const BloomSettings defaults{};
        if (!std::isfinite(bloomSettings_.threshold)) {
            bloomSettings_.threshold = defaults.threshold;
        }
        if (!std::isfinite(bloomSettings_.intensity)) {
            bloomSettings_.intensity = defaults.intensity;
        }
        if (!std::isfinite(bloomSettings_.radius)) {
            bloomSettings_.radius = defaults.radius;
        }
        bloomSettings_.threshold = std::clamp(
            bloomSettings_.threshold,
            0.0f,
            10.0f);
        bloomSettings_.intensity = std::clamp(
            bloomSettings_.intensity,
            0.0f,
            5.0f);
        bloomSettings_.radius = std::clamp(
            bloomSettings_.radius,
            0.0f,
            8.0f);
        bloomSettings_.downsampleCount = std::clamp<uint32_t>(
            bloomSettings_.downsampleCount,
            1u,
            5u);
    }

    void PostProcessingStage::SetToneMappingSettings(
        const ToneMappingSettings& settings) {
        const bool exposureModeChanged =
            toneMappingSettings_.exposureMode != settings.exposureMode;
        toneMappingSettings_ = settings;
        if (exposureModeChanged) {
            automaticExposureMeter_.Reset(settings.exposure);
        }
        if (settings.exposureMode ==
            RENDER3D::COLOR::ExposureMode::Manual) {
            exposureState_ =
                RENDER3D::COLOR::BuildManualExposureFrame(
                    settings.exposure);
        }
    }

    void PostProcessingStage::SetFxaaSettings(const FxaaSettings& settings) {
        fxaaSettings_ = settings;
        fxaaSettings_.edgeThreshold =
            std::clamp(fxaaSettings_.edgeThreshold, 0.0312f, 0.333f);
        fxaaSettings_.edgeThresholdMin =
            std::clamp(fxaaSettings_.edgeThresholdMin, 0.0f, 0.0833f);
        fxaaSettings_.subpixelQuality =
            std::clamp(fxaaSettings_.subpixelQuality, 0.0f, 1.0f);
    }

    void PostProcessingStage::SetSunGlareFrame(
        const SunGlareFrame& frame) {
        const SunGlareFrame defaults{};
        const auto finiteOr = [](float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        };
        sunGlareFrame_ = frame;
        sunGlareFrame_.sunUvX = finiteOr(frame.sunUvX, defaults.sunUvX);
        sunGlareFrame_.sunUvY = finiteOr(frame.sunUvY, defaults.sunUvY);
        sunGlareFrame_.coreRadius = std::clamp(
            finiteOr(frame.coreRadius, defaults.coreRadius),
            0.0005f,
            0.25f);
        sunGlareFrame_.colorR = std::clamp(
            finiteOr(frame.colorR, defaults.colorR), 0.0f, 64.0f);
        sunGlareFrame_.colorG = std::clamp(
            finiteOr(frame.colorG, defaults.colorG), 0.0f, 64.0f);
        sunGlareFrame_.colorB = std::clamp(
            finiteOr(frame.colorB, defaults.colorB), 0.0f, 64.0f);
        sunGlareFrame_.intensity = std::clamp(
            finiteOr(frame.intensity, defaults.intensity), 0.0f, 64.0f);
        sunGlareFrame_.haloRadius = std::clamp(
            finiteOr(frame.haloRadius, defaults.haloRadius),
            sunGlareFrame_.coreRadius,
            1.0f);
        sunGlareFrame_.haloFalloff = std::clamp(
            finiteOr(frame.haloFalloff, defaults.haloFalloff), 0.25f, 16.0f);
        sunGlareFrame_.streakIntensity = std::clamp(
            finiteOr(frame.streakIntensity, defaults.streakIntensity),
            0.0f,
            4.0f);
        sunGlareFrame_.streakLength = std::clamp(
            finiteOr(frame.streakLength, defaults.streakLength), 0.001f, 2.0f);
        sunGlareFrame_.occlusionRadius = std::clamp(
            finiteOr(frame.occlusionRadius, defaults.occlusionRadius),
            0.001f,
            0.25f);
        sunGlareFrame_.edgeFade = std::clamp(
            finiteOr(frame.edgeFade, defaults.edgeFade), 0.001f, 0.5f);
        sunGlareFrame_.maxRadiance = std::clamp(
            finiteOr(frame.maxRadiance, defaults.maxRadiance), 0.1f, 4096.0f);
        sunGlareFrame_.enabled = frame.enabled &&
            sunGlareFrame_.intensity > 0.0f &&
            (sunGlareFrame_.colorR > 0.0f ||
                sunGlareFrame_.colorG > 0.0f ||
                sunGlareFrame_.colorB > 0.0f);
    }

    void PostProcessingStage::ClearSunGlareFrame() {
        sunGlareFrame_ = {};
        sunGlareDebugStats_ = {};
    }

    void PostProcessingStage::SetActionScreenImpulseFrame(
        const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source,
        const MATH::Mat4& viewProj,
        uint64_t frameIndex) {
        actionScreenImpulseProvider_.Submit(
            frameIndex,
            viewProj,
            source);
    }

    void PostProcessingStage::SetActionScreenDistortionMask(
        const PostProcessingStage::ActionScreenDistortionMask& mask) {
        actionScreenImpulseProvider_.SubmitMask(mask);
    }

    void PostProcessingStage::SetActionScreenDistortionAccent(
        const PostProcessingStage::ActionScreenDistortionAccent& accent) {
        actionScreenImpulseProvider_.SubmitAccent(accent);
    }

    void PostProcessingStage::SetTransitionState(
        const TransitionVisualState& state) {
        if (!state.active) {
            ClearTransitionState();
            return;
        }
        std::string profileId = state.profileId.empty()
            ? "noise_wipe"
            : state.profileId;
        if (activeTransitionProfileId_ != profileId || !transitionEffect_) {
            TransitionProfile profile{};
            bool profileReady =
                TransitionProfile::LoadById(profileId, profile);
            if (!profileReady) {
                profileId = "noise_wipe";
                profileReady =
                    TransitionProfile::LoadById(profileId, profile);
            }
            std::unique_ptr<PostEffect> effect{};
            if (profileReady) {
                effect = std::make_unique<PostEffect>();
                profileReady = effect->LoadBuiltinPixelShader(
                    profile.programId,
                    profile.entryId);
            }
            if (profileReady) {
                activeTransitionProfile_ = std::move(profile);
                activeTransitionProfileId_ = profileId;
                transitionEffect_ = std::move(effect);
                transitionChain_.Clear();
                transitionChain_.Add(transitionEffect_.get());
            }
            else if (!transitionEffect_) {
                transitionActive_ = false;
                return;
            }
        }
        transitionActive_ = true;
        transitionParams_ = commonParams_;
        activeTransitionProfile_.ApplyToCommonParams(transitionParams_);
        transitionParams_.user[14] = {
            std::clamp(state.progress, 0.0f, 1.0f),
            state.isTransitionIn ? 1.0f : 0.0f,
            0.0f,
            0.0f
        };
    }

    void PostProcessingStage::ClearTransitionState() {
        transitionActive_ = false;
        activeTransitionProfileId_.clear();
        activeTransitionProfile_ = TransitionProfile{};
        transitionChain_.Clear();
        transitionEffect_.reset();
        transitionParams_ = CommonParams{};
    }

    const FxaaSettings& PostProcessingStage::GetFxaaSettings() const {
        return fxaaSettings_;
    }

    const BloomDebugStats& PostProcessingStage::GetBloomDebugStats() const {
        return bloomDebugStats_;
    }

    const SunGlareDebugStats&
    PostProcessingStage::GetSunGlareDebugStats() const {
        return sunGlareDebugStats_;
    }

    const ActionScreenImpulseProvider::DebugState&
        PostProcessingStage::GetActionScreenImpulseDebugState() const {
        return actionScreenImpulseProvider_.GetDebugState();
    }

    const CommonParams& PostProcessingStage::GetCommonParams() const {
        return commonParams_;
    }

    const RENDER3D::COLOR::ExposureFrameState&
        PostProcessingStage::GetExposureState() const {

        return exposureState_;
    }

    const RENDER3D::COLOR::AutomaticExposureDebugState&
        PostProcessingStage::GetAutomaticExposureDebugState() const {

        return automaticExposureMeter_.GetDebugState();
    }

    bool PostProcessingStage::IsDumpRequested() const {
        return dumpNextFrame_;
    }

    void PostProcessingStage::RequestFrameDump() {
        dumpNextFrame_ = true;
    }

    bool PostProcessingStage::EnsureToneMappingEffect() {
        if (toneMappingEffect_) return true;
        auto effect = std::make_unique<PostEffect>();
        if (!effect->LoadBuiltinPixelShader(
                "HIKARI.EngineShader.Post_ToneMappingPS")) {
            LogState("ToneMapping effect load failed");
            return false;
        }
        toneMappingEffect_ = std::move(effect);
        return true;
    }

    bool PostProcessingStage::EnsureFxaaEffect() {
        if (fxaaEffect_ && fxaaChain_.HasAny()) return true;
        if (!fxaaEffect_) {
            auto effect = std::make_unique<PostEffect>();
            if (!effect->LoadBuiltinPixelShader(
                    "HIKARI.EngineShader.Post_FXAA")) {
                LogState("FXAA effect load failed");
                return false;
            }
            fxaaEffect_ = std::move(effect);
        }
        fxaaChain_.Clear();
        fxaaChain_.Add(fxaaEffect_.get());
        return true;
    }

    bool PostProcessingStage::EnsureSunGlareEffect() {
        if (sunGlareEffect_) return true;
        auto effect = std::make_unique<PostEffect>();
        if (!effect->LoadBuiltinPixelShader(
                "HIKARI.EngineShader.Post_SunGlarePS")) {
            LogState("Sun glare effect load failed");
            return false;
        }
        sunGlareEffect_ = std::move(effect);
        return true;
    }

    bool PostProcessingStage::EnsureActionScreenImpulseEffect() {
        if (actionScreenImpulseEffect_ &&
            actionScreenImpulseChain_.HasAny()) {
            return true;
        }
        if (!actionScreenImpulseEffect_) {
            auto effect = std::make_unique<PostEffect>();
            if (!effect->LoadBuiltinPixelShader(
                    "HIKARI.EngineShader.Post_ActionScreenImpulse")) {
                LogState("Action screen impulse effect load failed");
                return false;
            }
            actionScreenImpulseEffect_ = std::move(effect);
        }
        actionScreenImpulseChain_.Clear();
        actionScreenImpulseChain_.Add(actionScreenImpulseEffect_.get());
        return true;
    }

    bool PostProcessingStage::EnsureBloomEffects(uint32_t blurPairCount) {
        blurPairCount = std::clamp<uint32_t>(blurPairCount, 1u, 5u);
        if (activeBloomBlurPairCount_ == blurPairCount &&
            !activeBloomEffects_.empty() && bloomChain_.HasAny()) {
            return true;
        }
        bloomChain_.Clear();
        activeBloomEffects_.clear();
        activeBloomBlurPairCount_ = 0;
        auto load = [](std::string_view programId) {
            auto effect = std::make_unique<PostEffect>();
            return effect->LoadBuiltinPixelShader(programId)
                ? std::move(effect)
                : nullptr;
        };
        auto extract = load("HIKARI.EngineShader.Post_BloomExtractPS");
        if (!extract) return false;
        bloomChain_.Add(extract.get());
        activeBloomEffects_.push_back(std::move(extract));
        for (uint32_t i = 0; i < blurPairCount; ++i) {
            auto horizontal = load(
                "HIKARI.EngineShader.Post_BloomBlurHPS");
            auto vertical = load(
                "HIKARI.EngineShader.Post_BloomBlurVPS");
            if (!horizontal || !vertical) {
                bloomChain_.Clear();
                activeBloomEffects_.clear();
                return false;
            }
            bloomChain_.Add(horizontal.get());
            activeBloomEffects_.push_back(std::move(horizontal));
            bloomChain_.Add(vertical.get());
            activeBloomEffects_.push_back(std::move(vertical));
        }
        activeBloomBlurPairCount_ = blurPairCount;
        return true;
    }

    bool PostProcessingStage::EnsureLdrTarget(
        int width,
        int height,
        DXGI_FORMAT format) {
        if (width <= 0 || height <= 0 || format == DXGI_FORMAT_UNKNOWN) return false;
        ldrTarget_.UpdateContext(context_);
        const bool invalid = !ldrTarget_.GetResource() ||
            ldrTarget_.GetWidth() != width || ldrTarget_.GetHeight() != height ||
            ldrTarget_.GetFormat() != format || ldrTarget_.HasDepth();
        if (!invalid) return true;
        ldrTarget_.Finalize();
        ldrTarget_.SetDebugName("PostProcessing.ToneMappedLDR");
        return ldrTarget_.Init(width, height, format, false, { 0, 0, 0, 1 });
    }

    bool PostProcessingStage::EnsureTemporalOutput(
        int width,
        int height,
        DXGI_FORMAT format) {
        if (width <= 0 || height <= 0 || format == DXGI_FORMAT_UNKNOWN) return false;
        temporalOutput_.UpdateContext(context_);
        const bool invalid = !temporalOutput_.GetResource() ||
            temporalOutput_.GetWidth() != width || temporalOutput_.GetHeight() != height ||
            temporalOutput_.GetFormat() != format || temporalOutput_.HasDepth();
        if (!invalid) return true;
        temporalOutput_.Finalize();
        temporalOutput_.SetDebugName("PostProcessing.TemporalOutput");
        return temporalOutput_.Init(width, height, format, false, { 0, 0, 0, 0 });
    }

    bool PostProcessingStage::EnsureSunGlareTarget(
        int width,
        int height) {
        if (width <= 0 || height <= 0) return false;
        const int targetWidth = (std::max)((width + 3) / 4, 1);
        const int targetHeight = (std::max)((height + 3) / 4, 1);
        sunGlareTarget_.UpdateContext(context_);
        const bool invalid = !sunGlareTarget_.GetResource() ||
            sunGlareTarget_.GetWidth() != targetWidth ||
            sunGlareTarget_.GetHeight() != targetHeight ||
            sunGlareTarget_.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT ||
            sunGlareTarget_.HasDepth();
        if (!invalid) return true;
        sunGlareTarget_.Finalize();
        sunGlareTarget_.SetDebugName("PostProcessing.SunGlareQuarterRes");
        return sunGlareTarget_.Init(
            targetWidth,
            targetHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            false,
            { 0, 0, 0, 0 });
    }

    bool PostProcessingStage::ApplySunGlare(
        RenderTarget2D& source,
        RenderTarget2D& sceneDepthOwner,
        QuadDrawer& quad) {
        sunGlareDebugStats_ = {};
        sunGlareDebugStats_.requested = sunGlareFrame_.enabled;
        sunGlareDebugStats_.sunUvX = sunGlareFrame_.sunUvX;
        sunGlareDebugStats_.sunUvY = sunGlareFrame_.sunUvY;
        sunGlareDebugStats_.intensity = sunGlareFrame_.intensity;
        if (!sunGlareFrame_.enabled || !source.GetResource() ||
            !sceneDepthOwner.GetDepthResource() ||
            sceneDepthOwner.GetDepthSrvGpu().ptr == 0) {
            return false;
        }
        if (!EnsureSunGlareEffect() ||
            !EnsureSunGlareTarget(source.GetWidth(), source.GetHeight())) {
            sunGlareDebugStats_.failed = true;
            return false;
        }

        sunGlareDebugStats_.textureWidth = sunGlareTarget_.GetWidth();
        sunGlareDebugStats_.textureHeight = sunGlareTarget_.GetHeight();
        if (!sceneDepthOwner.BeginDepthShaderRead()) {
            sunGlareDebugStats_.failed = true;
            return false;
        }

        sunGlareTarget_.BeginCapture(0, 0, 0, 0);
        bool rendered = quad.SetOutputFormat(sunGlareTarget_.GetFormat());
        if (rendered) {
            quad.SetInputTexture(
                sceneDepthOwner.GetSrvHeap(),
                sceneDepthOwner.GetDepthSrvGpu());
            sunGlareParams_ = commonParams_;
            sunGlareParams_.resolutionX =
                static_cast<float>(sunGlareTarget_.GetWidth());
            sunGlareParams_.resolutionY =
                static_cast<float>(sunGlareTarget_.GetHeight());
            sunGlareParams_.user[0] = {
                sunGlareFrame_.sunUvX,
                sunGlareFrame_.sunUvY,
                sunGlareFrame_.coreRadius,
                sunGlareFrame_.intensity
            };
            sunGlareParams_.user[1] = {
                sunGlareFrame_.colorR,
                sunGlareFrame_.colorG,
                sunGlareFrame_.colorB,
                sunGlareFrame_.maxRadiance
            };
            sunGlareParams_.user[2] = {
                sunGlareFrame_.haloRadius,
                sunGlareFrame_.haloFalloff,
                sunGlareFrame_.streakIntensity,
                sunGlareFrame_.streakLength
            };
            sunGlareParams_.user[3] = {
                sunGlareFrame_.occlusionRadius,
                sunGlareFrame_.edgeFade,
                static_cast<float>(sceneDepthOwner.GetWidth()),
                static_cast<float>(sceneDepthOwner.GetHeight())
            };
            sunGlareParams_.user[4] = {
                sunGlareFrame_.shaftIntensity,
                sunGlareFrame_.shaftLength,
                sunGlareFrame_.shaftDecay,
                0.0f
            };
            sunGlareEffect_->ApplyCommonParams(sunGlareParams_);
            rendered = sunGlareEffect_->BindAndDraw(quad);
        }
        sunGlareTarget_.EndCapture();
        sceneDepthOwner.EndDepthShaderRead();
        if (!rendered) {
            sunGlareDebugStats_.failed = true;
            return false;
        }

        source.Rebind();
        if (!quad.SetOutputFormat(source.GetFormat())) {
            source.EndCapture();
            sunGlareDebugStats_.failed = true;
            return false;
        }
        quad.DrawBlended(
            sunGlareTarget_.GetSrvHeap(),
            sunGlareTarget_.GetSrvGpu(),
            BlendOption::Additive);
        source.EndCapture();
        sunGlareDebugStats_.active = true;
        sunGlareDebugStats_.initialized = true;
        return true;
    }

    RenderTarget2D* PostProcessingStage::NormalizeTemporalOutput(
        RenderTarget2D& source,
        uint32_t width,
        uint32_t height,
        QuadDrawer& quad) {
        if (!EnsureTemporalOutput(
                static_cast<int>(width),
                static_cast<int>(height),
                source.GetFormat())) return nullptr;
        temporalOutput_.BeginCapture(0, 0, 0, 0);
        const bool ready = quad.SetOutputFormat(temporalOutput_.GetFormat());
        if (ready) quad.DrawFullscreen(source.GetSrvHeap(), source.GetSrvGpu());
        temporalOutput_.EndCapture();
        return ready ? &temporalOutput_ : nullptr;
    }

    RenderTarget2D* PostProcessingStage::ApplyBloom(
        RenderTarget2D& source,
        QuadDrawer& quad) {
        bloomDebugStats_ = {};
        bloomDebugStats_.enabled = bloomSettings_.enabled;
        bloomDebugStats_.threshold = bloomSettings_.threshold;
        bloomDebugStats_.intensity = bloomSettings_.intensity;
        bloomDebugStats_.radius = bloomSettings_.radius;
        bloomDebugStats_.downsampleCount =
            std::clamp<uint32_t>(bloomSettings_.downsampleCount, 1u, 5u);
        bloomDebugStats_.textureWidth = source.GetWidth();
        bloomDebugStats_.textureHeight = source.GetHeight();
        if (!source.GetResource() || !bloomSettings_.enabled ||
            bloomSettings_.intensity <= 0.0f) {
            return nullptr;
        }
        if (!EnsureBloomEffects(bloomDebugStats_.downsampleCount)) {
            bloomDebugStats_.failed = true;
            return nullptr;
        }
        // Bloom チェーンは 1/4 解像度で回す。先頭の extract がフル解像度 SRV を
        // 低解像度 ping へ描くことでダウンサンプルを兼ねる。5-tap ブラーの
        // pass あたりピクセルコストは 1/16 になり、テクセル幅が広がるぶん
        // 拡散も自然に太くなる。合成側 (加算ブレンド) は linear サンプルで
        // アップサンプルされる。
        const int bloomWidth =
            (std::max)((source.GetWidth() + 3) / 4, 1);
        const int bloomHeight =
            (std::max)((source.GetHeight() + 3) / 4, 1);
        bloomDebugStats_.textureWidth = bloomWidth;
        bloomDebugStats_.textureHeight = bloomHeight;
        CommonParams params = commonParams_;
        params.user[0] = {
            (std::max)(0.0f, bloomSettings_.threshold),
            (std::max)(0.0f, bloomSettings_.intensity),
            (std::max)(0.0f, bloomSettings_.radius),
            static_cast<float>(bloomDebugStats_.downsampleCount)
        };
        params.user[1] = {
            1.0f / static_cast<float>(bloomWidth),
            1.0f / static_cast<float>(bloomHeight), 0, 0
        };
        RenderTarget2D* bloomOutput = bloomChain_.Execute(
            source,
            quad,
            params,
            bloomWidth,
            bloomHeight);
        if (bloomOutput == nullptr || bloomOutput == &source ||
            bloomOutput->GetResource() == nullptr) {
            bloomDebugStats_.failed = true;
            return nullptr;
        }
        bloomDebugStats_.initialized = true;
        bloomDebugStats_.passCount =
            1u + bloomDebugStats_.downsampleCount * 2u;
        return bloomOutput;
    }

    RenderTarget2D* PostProcessingStage::ApplyActionScreenImpulse(
        RenderTarget2D& source,
        QuadDrawer& quad) {
        auto& debug = actionScreenImpulseProvider_.MutableDebugState();
        const uint64_t absoluteFrame =
            TIME::GetFrameContext().frameIndex;
        debug.frameIndex = absoluteFrame;
        debug.gfxFrameSlot = context_.frameIndex;
        debug.requested = actionScreenImpulseProvider_.IsCurrent(
            absoluteFrame);
        debug.activeWaveCount = actionScreenImpulseProvider_.GetCount();
        debug.sourceFrameIndex = actionScreenImpulseProvider_.GetSourceFrameIndex();
        debug.submitFrameIndex = actionScreenImpulseProvider_.GetFrameIndex();
        const auto& mask = actionScreenImpulseProvider_.GetMask();
        debug.maskProvided = mask.texture.resource != nullptr;
        const auto& cinematicGrade =
            actionScreenImpulseProvider_.GetCinematicGrade();
        const auto& accent = actionScreenImpulseProvider_.GetAccent();
        debug.accentProvided = accent.texture.resource != nullptr;
        debug.cinematicGradeProvided = cinematicGrade.IsActive();
        debug.cinematicGradeWeight = cinematicGrade.weight;
        debug.boostGradeProvided =
            cinematicGrade.boostWeight > 0.0f &&
            cinematicGrade.boostEdgeIntensity > 0.0f;
        debug.boostGradeWeight = cinematicGrade.boostWeight;
        const bool maskForFrame = mask.IsValid(absoluteFrame);
        const bool accentForFrame = accent.IsValid(absoluteFrame);
        debug.maskCompatible = maskForFrame &&
            source.GetResource() != nullptr;
        debug.accentCompatible = accentForFrame &&
            source.GetResource() != nullptr;
        if (!debug.requested || source.GetResource() == nullptr) {
            debug.bypassed = true;
            if (debug.sourceWaveCount != 0u &&
                debug.projectionRejectCount != 0u &&
                debug.lastRejectReason !=
                    lastActionProjectionRejectReason_) {
                std::ostringstream stream;
                stream << "[CombatVfx][ScreenDistortion] wave projection rejected"
                    << " reason=" << debug.lastRejectReason
                    << " sourceCount=" << debug.sourceWaveCount
                    << " rejected=" << debug.projectionRejectCount
                    << " frame=" << absoluteFrame;
                WriteCombatVfxLog(stream.str());
                lastActionProjectionRejectReason_ =
                    debug.lastRejectReason;
            }
            if (!debug.requested) {
                actionScreenRequestWasActive_ = false;
                if (debug.sourceWaveCount == 0u) {
                    lastActionProjectionRejectReason_ = 0u;
                    actionCompositeFailureLogged_ = false;
                }
            }
            return &source;
        }
        lastActionProjectionRejectReason_ = 0u;
        debug.providerReady = true;

        CommonParams params = commonParams_;
        actionScreenImpulseProvider_.ApplyParams(params);

        // This pass is deliberately separate from the one-SRV general post
        // chain: it binds scene+t1 mask+t2 accent in a frame-local heap and
        // therefore supports guard-only waves and the same-path trail layers.
        if (actionScreenDistortionComposite_) {
            RenderTarget2D* compositeOutput = nullptr;
            bool maskApplied = false;
            bool accentApplied = false;
            if (actionScreenDistortionComposite_->Execute(
                    source,
                    debug.maskCompatible ? &mask : nullptr,
                    debug.accentCompatible ? &accent : nullptr,
                    cinematicGrade,
                    params,
                    absoluteFrame,
                    compositeOutput,
                    maskApplied,
                    accentApplied)) {
                debug.pipelineReady = true;
                debug.executed = compositeOutput != nullptr &&
                    compositeOutput != &source &&
                    compositeOutput->GetResource() != nullptr;
                debug.maskApplied = maskApplied;
                debug.accentApplied = accentApplied;
                debug.cinematicGradeApplied =
                    debug.executed && cinematicGrade.weight > 0.0f;
                debug.boostGradeApplied = debug.executed &&
                    cinematicGrade.boostWeight > 0.0f &&
                    cinematicGrade.boostEdgeIntensity > 0.0f;
                debug.composited = debug.executed;
                if (debug.composited) {
                    if (!actionScreenRequestWasActive_) {
                        std::ostringstream stream;
                        stream << "[CombatVfx][ScreenDistortion] composited"
                            << " waves=" << debug.activeWaveCount
                            << " mask=" << debug.maskApplied
                            << " accent=" << debug.accentApplied
                            << " grade=" << debug.cinematicGradeApplied
                            << " boost=" << debug.boostGradeApplied
                            << " frame=" << absoluteFrame;
                        WriteCombatVfxLog(stream.str());
                    }
                    actionScreenRequestWasActive_ = true;
                    actionCompositeFailureLogged_ = false;
                    return compositeOutput;
                }
                debug.failed = true;
            }
        }

        // A one-SRV fallback keeps the guard reaction visible if the
        // optional two-texture path cannot initialize (for example while a
        // device is being recreated). It intentionally cannot reproduce the
        // mask or persistent cinematic grade.
        if ((debug.maskCompatible || cinematicGrade.IsActive()) &&
            !actionCompositeFailureLogged_) {
            WriteCombatVfxLog(
                "[CombatVfx][ScreenDistortion] dedicated composite failed; "
                "trail mask/cinematic grade cannot use the guard-only fallback");
            actionCompositeFailureLogged_ = true;
        }
        if (!EnsureActionScreenImpulseEffect()) {
            debug.failed = true;
            return &source;
        }
        debug.pipelineReady = true;

        RenderTarget2D* output = actionScreenImpulseChain_.Execute(
            source,
            quad,
            params);
        debug.executed = output != nullptr && output != &source &&
            output->GetResource() != nullptr;
        debug.maskApplied = false;
        debug.composited = debug.executed;
        if (!debug.composited) {
            debug.failed = true;
            return &source;
        }
        if (!actionScreenRequestWasActive_) {
            std::ostringstream stream;
            stream << "[CombatVfx][ScreenDistortion] guard fallback composited"
                << " waves=" << debug.activeWaveCount
                << " frame=" << absoluteFrame;
            WriteCombatVfxLog(stream.str());
        }
        actionScreenRequestWasActive_ = true;
        return output;
    }

    RenderTarget2D* PostProcessingStage::ApplyFxaa(
        RenderTarget2D& source,
        QuadDrawer& quad) {
        if (!source.GetResource() || !EnsureFxaaEffect()) return nullptr;
        const float width = static_cast<float>((std::max)(1, source.GetWidth()));
        const float height = static_cast<float>((std::max)(1, source.GetHeight()));
        fxaaParams_ = commonParams_;
        fxaaParams_.resolutionX = width;
        fxaaParams_.resolutionY = height;
        fxaaParams_.user[0] = { width, height, 1.0f / width, 1.0f / height };
        fxaaParams_.user[1] = {
            1.0f, fxaaSettings_.edgeThreshold,
            fxaaSettings_.edgeThresholdMin, fxaaSettings_.subpixelQuality
        };
        return fxaaChain_.Execute(source, quad, fxaaParams_);
    }

    RenderTarget2D* PostProcessingStage::ResolveHdr(
        RenderTarget2D& source,
        QuadDrawer& quad,
        bool debugOutput,
        const char* temporalBackend,
        bool lightingEnabled,
        RenderTarget2D* sceneDepthOwner) {
        RenderTarget2D* output = &source;
        commonParams_.resolutionX = static_cast<float>((std::max)(1, output->GetWidth()));
        commonParams_.resolutionY = static_cast<float>((std::max)(1, output->GetHeight()));
        if (!debugOutput && sceneDepthOwner != nullptr) {
            (void)ApplySunGlare(*output, *sceneDepthOwner, quad);
        } else {
            sunGlareDebugStats_ = {};
            sunGlareDebugStats_.requested = sunGlareFrame_.enabled;
        }
        if (!debugOutput && globalChain_.HasAny()) {
            output = globalChain_.Execute(*output, quad, commonParams_);
        }
        if (!debugOutput) {
            (void)automaticExposureMeter_.QueueMeasurement(*output);
            // This is deliberately after temporal/global HDR processing and
            // after exposure measurement, but before bloom so the bright ring
            // and flash participate in the same bloom extraction as scene HDR.
            output = ApplyActionScreenImpulse(*output, quad);
        }
        else {
            auto& actionDebug =
                actionScreenImpulseProvider_.MutableDebugState();
            const uint64_t absoluteFrame =
                TIME::GetFrameContext().frameIndex;
            actionDebug.frameIndex = absoluteFrame;
            actionDebug.gfxFrameSlot = context_.frameIndex;
            actionDebug.requested =
                actionScreenImpulseProvider_.IsCurrent(absoluteFrame);
            actionDebug.bypassed = actionDebug.requested;
        }
        RenderTarget2D* bloom = debugOutput ? nullptr : ApplyBloom(*output, quad);
        if (bloom && bloom->GetResource()) {
            output->Rebind();
            if (quad.SetOutputFormat(output->GetFormat())) {
                quad.DrawBlended(bloom->GetSrvHeap(), bloom->GetSrvGpu(), BlendOption::Additive);
            }
            output->EndCapture();
        }
        // Scene transitions operate in scene-linear HDR. The single LDR resolve below
        // remains the owner of exposure, tone curve, and display transfer.
        if (!debugOutput && transitionActive_ && transitionEffect_) {
            transitionParams_.resolutionX =
                static_cast<float>((std::max)(1, output->GetWidth()));
            transitionParams_.resolutionY =
                static_cast<float>((std::max)(1, output->GetHeight()));
            output = transitionChain_.Execute(*output, quad, transitionParams_);
        }
        if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
            const auto& quality = RENDER3D::GetRenderQualitySettings();
            HIKARI_LOG_INFO(std::string("[PostProcessing][HDR] global=") +
                (globalChain_.HasAny() ? "on" : "off") + " bloom=" +
                (bloom && bloom->GetResource() ? "on" : "off") +
                " temporal=" + (temporalBackend ? temporalBackend : "off") +
                " sunGlare=" + (sunGlareDebugStats_.active ? "on" : "off") +
                " fxaa=" + (RENDER3D::IsFxaaAntiAliasingMode(quality.antiAliasingMode) ? "on" : "off") +
                " lighting=" + (lightingEnabled ? "on" : "off"));
        }
        return output;
    }

    RenderTarget2D* PostProcessingStage::ResolveDebugLdr(
        RenderTarget2D& source,
        DXGI_FORMAT outputFormat,
        QuadDrawer& quad) {
        if (!EnsureLdrTarget(source.GetWidth(), source.GetHeight(), outputFormat)) {
            return nullptr;
        }

        ldrTarget_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
        const bool ready = quad.SetOutputFormat(ldrTarget_.GetFormat());
        if (ready) {
            quad.DrawFullscreen(source.GetSrvHeap(), source.GetSrvGpu());
        }
        ldrTarget_.EndCapture();
        return ready ? &ldrTarget_ : nullptr;
    }

    RenderTarget2D* PostProcessingStage::ResolveLdr(
        RenderTarget2D& source,
        DXGI_FORMAT outputFormat,
        QuadDrawer& quad,
        RenderTarget2D* lightTarget) {
        if (!EnsureLdrTarget(source.GetWidth(), source.GetHeight(), outputFormat)) return nullptr;
        if (!EnsureToneMappingEffect()) return nullptr;
        ldrTarget_.BeginCapture(0, 0, 0, 1);
        if (!quad.SetOutputFormat(ldrTarget_.GetFormat())) {
            ldrTarget_.EndCapture();
            return nullptr;
        }
        quad.SetInputTexture(source.GetSrvHeap(), source.GetSrvGpu());
        toneMappingParams_ = commonParams_;
        toneMappingParams_.user[0] = {
            toneMappingSettings_.enabled ? 1.0f : 0.0f,
            exposureState_.exposureMultiplier,
            (std::max)(0.01f, toneMappingSettings_.gamma),
            static_cast<float>(toneMappingSettings_.mode)
        };
        const auto displayColor =
            RENDER3D::COLOR::BuildDisplayColorFrame(
                toneMappingSettings_.whiteBalanceTemperature,
                toneMappingSettings_.whiteBalanceTint,
                toneMappingSettings_.contrast,
                toneMappingSettings_.saturation);
        toneMappingParams_.user[1] = {
            displayColor.whiteBalanceR,
            displayColor.whiteBalanceG,
            displayColor.whiteBalanceB,
            displayColor.contrast
        };
        toneMappingParams_.user[2] = {
            displayColor.saturation,
            0.0f,
            0.0f,
            0.0f
        };
        toneMappingEffect_->ApplyCommonParams(toneMappingParams_);
        if (!toneMappingEffect_->BindAndDraw(quad)) {
            ldrTarget_.EndCapture();
            return nullptr;
        }
        if (lightTarget && lightTarget->GetResource()) {
            quad.DrawBlended(lightTarget->GetSrvHeap(), lightTarget->GetSrvGpu(), BlendOption::Multiply);
        }
        ldrTarget_.EndCapture();
        RenderTarget2D* output = &ldrTarget_;
        const auto& quality = RENDER3D::GetRenderQualitySettings();
        if (RENDER3D::IsFxaaAntiAliasingMode(quality.antiAliasingMode)) {
            RenderTarget2D* fxaa = ApplyFxaa(*output, quad);
            if (fxaa && fxaa->GetResource()) output = fxaa;
        }
        if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
            LogState(dumpNextFrame_ ? "Requested frame dump" : "Verbose post log");
            dumpNextFrame_ = false;
        }
        return output;
    }

    std::string PostProcessingStage::DumpState() const {
        std::ostringstream stream;
        stream << "[PostProcessingStage] global=" << globalChain_.HasAny()
            << " bloom=" << bloomSettings_.enabled
            << " bloomPasses=" << bloomDebugStats_.passCount
            << " sunGlare=" << sunGlareDebugStats_.active
            << " sunGlareRequested=" << sunGlareDebugStats_.requested
            << " actionScreenImpulse="
            << actionScreenImpulseProvider_.GetDebugState().composited
            << " actionScreenImpulseRequested="
            << actionScreenImpulseProvider_.GetDebugState().requested
            << " actionScreenImpulseWaves="
            << actionScreenImpulseProvider_.GetDebugState().activeWaveCount
            << " actionScreenImpulseSourceFrame="
            << actionScreenImpulseProvider_.GetDebugState().sourceFrameIndex
            << " actionScreenImpulseSubmitFrame="
            << actionScreenImpulseProvider_.GetDebugState().submitFrameIndex
            << " actionScreenImpulseGfxSlot="
            << actionScreenImpulseProvider_.GetDebugState().gfxFrameSlot
            << " actionScreenImpulseProjected="
            << actionScreenImpulseProvider_.GetDebugState().projectedWaveCount
            << " actionScreenImpulseDropped="
            << actionScreenImpulseProvider_.GetDebugState().droppedWaveCount
            << " actionScreenImpulseRejects="
            << actionScreenImpulseProvider_.GetDebugState().projectionRejectCount
            << " actionScreenImpulseRejectReason="
            << actionScreenImpulseProvider_.GetDebugState().lastRejectReason
            << " actionScreenImpulseMask="
            << actionScreenImpulseProvider_.GetDebugState().maskApplied
            << " actionScreenImpulseMaskProvided="
            << actionScreenImpulseProvider_.GetDebugState().maskProvided
            << " actionScreenImpulseMaskCompatible="
            << actionScreenImpulseProvider_.GetDebugState().maskCompatible
            << " actionScreenImpulseGrade="
            << actionScreenImpulseProvider_.GetDebugState().cinematicGradeApplied
            << " actionScreenImpulseGradeWeight="
            << actionScreenImpulseProvider_.GetDebugState().cinematicGradeWeight
            << " actionScreenImpulseFailed="
            << actionScreenImpulseProvider_.GetDebugState().failed
            << " actionScreenImpulseBypassed="
            << actionScreenImpulseProvider_.GetDebugState().bypassed
            << " toneCurve=" << toneMappingSettings_.enabled
            << " exposureMode="
            << RENDER3D::COLOR::ToString(exposureState_.mode)
            << " exposure=" << exposureState_.exposureMultiplier
            << " exposureTarget="
            << exposureState_.targetExposureMultiplier
            << " meteredLuminance="
            << exposureState_.meteredLuminance
            << " meteringValid=" << exposureState_.meteringValid
            << " preExposure=" << exposureState_.preExposure
            << " transition=" << transitionActive_
            << "\n  " << ldrTarget_.DumpState()
            << "\n  " << temporalOutput_.DumpState()
            << "\n  " << sunGlareTarget_.DumpState()
            << "\n  " << globalChain_.DumpState()
            << "\n  " << bloomChain_.DumpState()
            << "\n  " << transitionChain_.DumpState()
            << "\n  " << fxaaChain_.DumpState();
        return stream.str();
    }

    void PostProcessingStage::LogState(const char* reason) const {
        DEBUGLOG::PushRenderError(std::string("[PostProcessingStage][DUMP] reason=") +
            (reason ? reason : "") + "\n" + DumpState());
    }

} // namespace HIKARI::POST
