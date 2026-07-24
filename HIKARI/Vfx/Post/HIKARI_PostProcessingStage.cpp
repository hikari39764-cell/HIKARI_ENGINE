#include "Vfx/Post/HIKARI_PostProcessingStage.h"

#include <algorithm>
#include <sstream>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {

    PostProcessingStage::PostProcessingStage() = default;
    PostProcessingStage::~PostProcessingStage() = default;

    void PostProcessingStage::UpdateContext(const GFX::Context& context) {
        context_ = context;
        PostEffect::UpdateContext(context);
        globalChain_.UpdateContext(context);
        bloomChain_.UpdateContext(context);
        fxaaChain_.UpdateContext(context);
        ldrTarget_.UpdateContext(context);
        temporalOutput_.UpdateContext(context);
        globalChain_.SetDebugName("PostProcessing.Global");
        bloomChain_.SetDebugName("PostProcessing.Bloom");
        fxaaChain_.SetDebugName("PostProcessing.FXAA");
    }

    void PostProcessingStage::Shutdown() {
        ClearGlobalProfile();
        ClearTransitionState();
        globalChain_.Finalize();
        bloomChain_.Finalize();
        fxaaChain_.Finalize();
        activeBloomEffects_.clear();
        toneMappingEffect_.reset();
        fxaaEffect_.reset();
        ldrTarget_.Finalize();
        temporalOutput_.Finalize();
        bloomDebugStats_ = {};
        activeBloomBlurPairCount_ = 0;
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
                ClearGlobalProfile();
                return false;
            }
            globalChain_.Clear();
            activeGlobalEffects_.clear();
            activeGlobalEffects_.reserve(profile.passes.size());
            for (const auto& pass : profile.passes) {
                auto effect = std::make_unique<PostEffect>();
                std::wstring path = L"HIKARI/Shaders/";
                path += std::wstring(pass.shaderId.begin(), pass.shaderId.end());
                path += L".hlsl";
                if (!effect->LoadPixelShader(path.c_str())) {
                    continue;
                }
                globalChain_.Add(effect.get());
                activeGlobalEffects_.push_back(std::move(effect));
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
    }

    void PostProcessingStage::SetToneMappingSettings(
        const ToneMappingSettings& settings) {
        toneMappingSettings_ = settings;
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
            if (!TransitionProfile::LoadById(profileId, profile)) {
                profileId = "noise_wipe";
                if (!TransitionProfile::LoadById(profileId, profile)) {
                    ClearTransitionState();
                    return;
                }
            }
            auto effect = std::make_unique<PostEffect>();
            std::wstring path = L"HIKARI/Shaders/";
            path += std::wstring(profile.shaderId.begin(), profile.shaderId.end());
            path += L".hlsl";
            if (!effect->LoadPixelShader(path.c_str())) {
                ClearTransitionState();
                return;
            }
            activeTransitionProfile_ = std::move(profile);
            activeTransitionProfileId_ = profileId;
            transitionEffect_ = std::move(effect);
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
        transitionEffect_.reset();
        transitionParams_ = CommonParams{};
    }

    const FxaaSettings& PostProcessingStage::GetFxaaSettings() const {
        return fxaaSettings_;
    }

    const BloomDebugStats& PostProcessingStage::GetBloomDebugStats() const {
        return bloomDebugStats_;
    }

    const CommonParams& PostProcessingStage::GetCommonParams() const {
        return commonParams_;
    }

    float PostProcessingStage::GetExposure() const {
        return toneMappingSettings_.exposure;
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
        if (!effect->LoadPixelShader(L"HIKARI/Shaders/Post_ToneMappingPS.hlsl")) {
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
            if (!effect->LoadPixelShader(L"HIKARI/Shaders/Post_FXAA.hlsl")) {
                LogState("FXAA effect load failed");
                return false;
            }
            fxaaEffect_ = std::move(effect);
        }
        fxaaChain_.Clear();
        fxaaChain_.Add(fxaaEffect_.get());
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
        auto load = [](const wchar_t* path) {
            auto effect = std::make_unique<PostEffect>();
            return effect->LoadPixelShader(path) ? std::move(effect) : nullptr;
        };
        auto extract = load(L"HIKARI/Shaders/Post_BloomExtractPS.hlsl");
        if (!extract) return false;
        bloomChain_.Add(extract.get());
        activeBloomEffects_.push_back(std::move(extract));
        for (uint32_t i = 0; i < blurPairCount; ++i) {
            auto horizontal = load(L"HIKARI/Shaders/Post_BloomBlurHPS.hlsl");
            auto vertical = load(L"HIKARI/Shaders/Post_BloomBlurVPS.hlsl");
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
        if (!bloomSettings_.enabled || bloomSettings_.intensity <= 0.0f) return nullptr;
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
            (std::max)(source.GetWidth() / 4, 32);
        const int bloomHeight =
            (std::max)(source.GetHeight() / 4, 32);
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
        bloomDebugStats_.initialized = true;
        bloomDebugStats_.passCount = 1u + bloomDebugStats_.downsampleCount * 2u;
        return bloomChain_.Execute(source, quad, params, bloomWidth, bloomHeight);
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
        bool lightingEnabled) {
        RenderTarget2D* output = &source;
        commonParams_.resolutionX = static_cast<float>((std::max)(1, output->GetWidth()));
        commonParams_.resolutionY = static_cast<float>((std::max)(1, output->GetHeight()));
        if (!debugOutput && globalChain_.HasAny()) {
            output = globalChain_.Execute(*output, quad, commonParams_);
        }
        RenderTarget2D* bloom = debugOutput ? nullptr : ApplyBloom(*output, quad);
        if (bloom && bloom->GetResource()) {
            output->Rebind();
            if (quad.SetOutputFormat(output->GetFormat())) {
                quad.DrawBlended(bloom->GetSrvHeap(), bloom->GetSrvGpu(), BlendOption::Additive);
            }
            output->EndCapture();
        }
        if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
            const auto& quality = RENDER3D::GetRenderQualitySettings();
            HIKARI_LOG_INFO(std::string("[PostProcessing][HDR] global=") +
                (globalChain_.HasAny() ? "on" : "off") + " bloom=" +
                (bloom && bloom->GetResource() ? "on" : "off") +
                " temporal=" + (temporalBackend ? temporalBackend : "off") +
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
        const bool useTransition = transitionActive_ && transitionEffect_;
        if (!useTransition && !EnsureToneMappingEffect()) return nullptr;
        ldrTarget_.BeginCapture(0, 0, 0, 1);
        if (!quad.SetOutputFormat(ldrTarget_.GetFormat())) {
            ldrTarget_.EndCapture();
            return nullptr;
        }
        quad.SetInputTexture(source.GetSrvHeap(), source.GetSrvGpu());
        PostEffect* effect = nullptr;
        if (useTransition) {
            transitionEffect_->ApplyCommonParams(transitionParams_);
            effect = transitionEffect_.get();
        } else {
            toneMappingParams_ = commonParams_;
            toneMappingParams_.user[0] = {
                toneMappingSettings_.enabled ? 1.0f : 0.0f,
                (std::max)(0.0f, toneMappingSettings_.exposure),
                (std::max)(0.01f, toneMappingSettings_.gamma),
                static_cast<float>(toneMappingSettings_.mode)
            };
            toneMappingEffect_->ApplyCommonParams(toneMappingParams_);
            effect = toneMappingEffect_.get();
        }
        if (!effect->BindAndDraw(quad)) {
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
            << " toneMapping=" << toneMappingSettings_.enabled
            << " transition=" << transitionActive_
            << "\n  " << ldrTarget_.DumpState()
            << "\n  " << temporalOutput_.DumpState()
            << "\n  " << globalChain_.DumpState()
            << "\n  " << bloomChain_.DumpState()
            << "\n  " << fxaaChain_.DumpState();
        return stream.str();
    }

    void PostProcessingStage::LogState(const char* reason) const {
        DEBUGLOG::PushRenderError(std::string("[PostProcessingStage][DUMP] reason=") +
            (reason ? reason : "") + "\n" + DumpState());
    }

} // namespace HIKARI::POST
