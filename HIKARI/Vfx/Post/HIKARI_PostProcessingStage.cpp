#include "Vfx/Post/HIKARI_PostProcessingStage.h"
#include "Assets/HIKARI_AssetRegistry.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>

#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Render3D/Color/HIKARI_ColorGradingBake.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Vfx/Post/HIKARI_CombatScreenDistortionComposite.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {
namespace {
void LogCombatVfx(CORE::LogLevel level, const std::string& message) noexcept {
    try {
        CORE::Log(level, message);
    } catch (...) {
    }
}
} // namespace

void WriteToneMappingParameters(CommonParams& parameters, const ToneMappingSettings& settings, float residualExposure,
                                float radialCurvature, float radialStrength, uint32_t gradingLutSize) {
    const auto color = RENDER3D::COLOR::BuildDisplayColorFrame(
        settings.whiteBalanceTemperature, settings.whiteBalanceTint, settings.contrast, settings.saturation);
    parameters.user[0] = {static_cast<float>(settings.curve), residualExposure, (std::max)(0.01f, settings.gamma),
                          0.0f};
    parameters.user[1] = {color.whiteBalanceR, color.whiteBalanceG, color.whiteBalanceB, color.contrast};
    parameters.user[2] = {color.saturation, radialCurvature, radialStrength, static_cast<float>(gradingLutSize)};
}

void WriteFxaaParameters(CommonParams& parameters, int width, int height, const FxaaSettings& settings) {
    parameters.resolutionX = static_cast<float>((std::max)(1, width));
    parameters.resolutionY = static_cast<float>((std::max)(1, height));
    parameters.user[0] = {parameters.resolutionX, parameters.resolutionY, 1.0f / parameters.resolutionX,
                          1.0f / parameters.resolutionY};
    parameters.user[1] = {1.0f, settings.edgeThreshold, settings.edgeThresholdMin, settings.subpixelQuality};
}

PostProcessingStage::PostProcessingStage()
    : actionScreenDistortionComposite_(std::make_unique<CombatScreenDistortionComposite>()) {}
PostProcessingStage::~PostProcessingStage() = default;

void PostProcessingStage::UpdateContext(const GFX::Context& context) {
    if (context_.device != context.device || context_.deviceGeneration != context.deviceGeneration)
        ClearActionPostFrame();
    context_ = context;
    PostEffect::UpdateContext(context);
    globalChain_.UpdateContext(context);
    actionPostChain_.UpdateContext(context);
    sequenceChain_.UpdateContext(context);
    for (auto& level : bloomLevels_)
        level.UpdateContext(context);
    transitionChain_.UpdateContext(context);
    fxaaChain_.UpdateContext(context);
    actionScreenImpulseChain_.UpdateContext(context);
    if (!actionScreenDistortionComposite_) {
        actionScreenDistortionComposite_ = std::make_unique<CombatScreenDistortionComposite>();
    }
    actionScreenDistortionComposite_->UpdateContext(context);
    ldrTarget_.UpdateContext(context);
    temporalOutput_.UpdateContext(context);
    sunGlareTarget_.UpdateContext(context);
    automaticExposureMeter_.UpdateContext(context);
    globalChain_.SetDebugName("PostProcessing.Global");
    actionPostChain_.SetDebugName("PostProcessing.ProjectAction");
    sequenceChain_.SetDebugName("PostProcessing.Sequence");
    for (size_t i = 0; i < bloomLevels_.size(); ++i)
        bloomLevels_[i].SetDebugName("PostProcessing.Bloom." + std::to_string(i));
    transitionChain_.SetDebugName("PostProcessing.TransitionHDR");
    fxaaChain_.SetDebugName("PostProcessing.FXAA");
    actionScreenImpulseChain_.SetDebugName("PostProcessing.ActionScreenImpulse");
}

void PostProcessingStage::Shutdown() {
    ClearActionPostFrame();
    ClearGlobalEffects();
    ClearTransitionState();
    globalChain_.Finalize();
    sequenceChain_.Clear();
    sequenceChain_.Finalize();
    sequenceEffect_.reset();
    sequenceEffects_.clear();
    for (auto& level : bloomLevels_)
        level.Finalize();
    transitionChain_.Finalize();
    fxaaChain_.Finalize();
    actionScreenImpulseChain_.Finalize();
    bloomDownsampleEffect_.reset();
    bloomUpsampleEffect_.reset();
    actionScreenImpulseEffect_.reset();
    if (actionScreenDistortionComposite_) {
        actionScreenDistortionComposite_->Shutdown();
    }
    sunGlareEffect_.reset();
    toneMappingEffect_.reset();
    colorGradingLut_.Release(context_);
    fxaaEffect_.reset();
    ldrTarget_.Finalize();
    temporalOutput_.Finalize();
    sunGlareTarget_.Finalize();
    automaticExposureMeter_.Shutdown();
    bloomDebugStats_ = {};
    sunGlareFrame_ = {};
    sunGlareDebugStats_ = {};
    activeBloomLevelCount_ = 0;
    actionScreenImpulseProvider_.BeginFrame(0u);
    actionScreenImpulseProvider_.MutableDebugState() = {};
    lastActionProjectionRejectReason_ = 0u;
    actionCompositeFailureLogged_ = false;
    actionScreenRequestWasActive_ = false;
    dumpNextFrame_ = false;
}

void PostProcessingStage::UpdateCommonParams(float deltaTime, int width, int height) {
    commonParams_.resolutionX = static_cast<float>((std::max)(1, width));
    commonParams_.resolutionY = static_cast<float>((std::max)(1, height));
    commonParams_.deltaTime = deltaTime;
    elapsedTime_ += deltaTime;
    commonParams_.time = elapsedTime_;
    actionScreenImpulseProvider_.BeginFrame(TIME::GetFrameContext().frameIndex);
}

void PostProcessingStage::AdvanceExposure(float deltaTime, const RENDER3D::COLOR::ExposureIllumination& illumination,
                                          bool meterScene) {
    exposureState_ = automaticExposureMeter_.BeginFrame(exposureSettings_, illumination, deltaTime);
    meterScene_ = meterScene;
}

void PostProcessingStage::SetIntensity(float intensity) { commonParams_.intensity = intensity; }

void PostProcessingStage::SetCombo(float combo) { commonParams_.combo = combo; }

bool PostProcessingStage::SetGlobalEffects(std::span<const ResolvedPostLayer> layers, const AssetRegistry* registry) {
    // 資産の再読み込みで定義が差し替わった時は pass を作り直す。
    const uint64_t registryId = registry ? registry->GetInstanceId() : 0u;
    const uint64_t registryRevision = registry ? registry->GetRevision() : 0u;
    const bool sameRegistry =
        activeGlobalRegistryId_ == registryId && activeGlobalRegistryRevision_ == registryRevision;
    std::vector<ActiveGlobalLayer> next;
    next.reserve(layers.size());
    for (const ResolvedPostLayer& layer : layers) {
        if (layer.effectId.empty() || !layer.effect) {
            continue;
        }
        ActiveGlobalLayer active{layer.effectId, layer.effect, {}, layer.values};
        const auto reusable = sameRegistry ? std::find_if(activeGlobalLayers_.begin(), activeGlobalLayers_.end(),
                                                          [&](const ActiveGlobalLayer& candidate) {
                                                              return !candidate.passes.empty() &&
                                                                     candidate.effect == layer.effect &&
                                                                     candidate.effectId == layer.effectId;
                                                          })
                                           : activeGlobalLayers_.end();
        if (reusable != activeGlobalLayers_.end()) {
            active.passes = std::move(reusable->passes);
        } else if (!BuildPostEffectPasses(*layer.effect, registry, active.passes)) {
            continue;
        }
        next.push_back(std::move(active));
    }
    activeGlobalLayers_ = std::move(next);
    activeGlobalRegistryId_ = registryId;
    activeGlobalRegistryRevision_ = registryRevision;
    return !activeGlobalLayers_.empty();
}

RenderTarget2D* PostProcessingStage::ExecuteGlobalLayers(PostEffectPhase phase, RenderTarget2D* output,
                                                         QuadDrawer& quad, RenderTarget2D* sceneDepthOwner,
                                                         const RENDER3D::RenderTextureView* surfaceFeatureMask) {
    for (const ActiveGlobalLayer& layer : activeGlobalLayers_) {
        if (layer.effect->phase != phase) {
            continue;
        }
        const bool mask = layer.effect->requiresSurfaceFeatureMask;
        const bool depth = layer.effect->requiresSceneDepth;
        if ((mask && !surfaceFeatureMask) ||
            (depth && (sceneDepthOwner == nullptr || !sceneDepthOwner->GetDepthResource() ||
                       !sceneDepthOwner->BeginDepthShaderRead()))) {
            continue;
        }
        globalChain_.Clear();
        for (const auto& pass : layer.passes) {
            globalChain_.Add(pass.get());
        }
        output = globalChain_.Execute(*output, quad, ResolveGlobalFrameParams(layer), 0, 0,
                                      mask ? surfaceFeatureMask : nullptr,
                                      depth ? sceneDepthOwner->GetDepthResource() : nullptr);
        if (depth) {
            sceneDepthOwner->EndDepthShaderRead();
        }
    }
    globalChain_.Clear();
    return output;
}

bool PostProcessingStage::RequiresSurfaceFeatureMask() const noexcept {
    return std::any_of(activeGlobalLayers_.begin(), activeGlobalLayers_.end(),
                       [](const ActiveGlobalLayer& layer) { return layer.effect->requiresSurfaceFeatureMask; });
}

bool PostProcessingStage::SubmitEffectParameter(std::string_view effectId, uint32_t index, uint32_t channelMask,
                                                const DirectX::XMFLOAT4& value, int32_t priority, uint64_t sourceId) {
    const bool active = std::any_of(activeGlobalLayers_.begin(), activeGlobalLayers_.end(),
                                    [effectId](const ActiveGlobalLayer& layer) { return layer.effectId == effectId; });
    if (effectId.empty() || !active || index >= kPostParameterSlotCount || channelMask == 0u ||
        (channelMask & ~15u) != 0u || sourceId == 0u)
        return false;
    const float values[]{value.x, value.y, value.z, value.w};
    for (uint32_t channel = 0; channel < 4u; ++channel)
        if ((channelMask & (1u << channel)) && !std::isfinite(values[channel]))
            return false;
    const auto frame = TIME::GetFrameContext().frameIndex;
    if (effectOverrideFrame_ != frame || effectOverrideId_ != effectId) {
        effectOverrides_ = {};
        effectOverrideFrame_ = frame;
        effectOverrideId_ = effectId;
    }
    for (uint32_t channel = 0; channel < 4u; ++channel) {
        if (!(channelMask & (1u << channel)))
            continue;
        auto& selected = effectOverrides_[index][channel];
        if (selected.sourceId == 0u || priority > selected.priority ||
            (priority == selected.priority && sourceId <= selected.sourceId))
            selected = {values[channel], priority, sourceId};
    }
    return true;
}

CommonParams PostProcessingStage::ResolveGlobalFrameParams(const ActiveGlobalLayer& layer) const noexcept {
    CommonParams params = commonParams_;
    std::copy(layer.values.begin(), layer.values.end(), std::begin(params.user));
    if (effectOverrideFrame_ != TIME::GetFrameContext().frameIndex || effectOverrideId_ != layer.effectId)
        return params;
    for (size_t index = 0; index < effectOverrides_.size(); ++index) {
        auto& value = params.user[index];
        float* channels[]{&value.x, &value.y, &value.z, &value.w};
        for (size_t channel = 0; channel < 4; ++channel)
            if (effectOverrides_[index][channel].sourceId)
                *channels[channel] = effectOverrides_[index][channel].value;
    }
    return params;
}

void PostProcessingStage::GetRadialScreenWarp(float& curvature, float& strength) const noexcept {
    curvature = strength = 0.0f;
    // 画面の曲げは最初に曲げを宣言した層のパラメーターから読む。SDK の一時上書きも反映する。
    const auto layer = std::find_if(activeGlobalLayers_.begin(), activeGlobalLayers_.end(),
                                    [](const ActiveGlobalLayer& candidate) { return candidate.effect->screenWarp; });
    if (layer == activeGlobalLayers_.end())
        return;
    const CommonParams params = ResolveGlobalFrameParams(*layer);
    const PostScreenWarpBinding& warp = *layer->effect->screenWarp;
    const auto strengthValue = ReadPostParameter(*layer->effect, params.user, warp.strengthParam);
    const auto curvatureValue = ReadPostParameter(*layer->effect, params.user, warp.curvatureParam);
    if (!strengthValue || !curvatureValue)
        return;
    const float value = *strengthValue * params.intensity;
    if (!std::isfinite(*curvatureValue) || !std::isfinite(value))
        return;
    curvature = (std::max)(*curvatureValue, 0.0f);
    strength = std::clamp(value, 0.0f, 1.0f);
}

void PostProcessingStage::ClearGlobalEffects() {
    effectOverrides_ = {};
    effectOverrideId_.clear();
    effectOverrideFrame_ = (std::numeric_limits<uint64_t>::max)();
    activeGlobalRegistryId_ = activeGlobalRegistryRevision_ = 0u;
    globalChain_.Clear();
    activeGlobalLayers_.clear();
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
    bloomSettings_.threshold = std::clamp(bloomSettings_.threshold, 0.0f, 10.0f);
    bloomSettings_.intensity = std::clamp(bloomSettings_.intensity, 0.0f, 5.0f);
    bloomSettings_.radius = std::clamp(bloomSettings_.radius, 0.0f, 8.0f);
    bloomSettings_.downsampleCount =
        std::clamp<uint32_t>(bloomSettings_.downsampleCount, 1u, static_cast<uint32_t>(bloomLevels_.size()));
}

void PostProcessingStage::SetColorGrading(const ColorGradingSettings& settings,
                                          std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> lut) {
    colorGradingLut_.SetSource(settings, std::move(lut));
}

void PostProcessingStage::SetToneMappingSettings(const ToneMappingSettings& settings) {
    toneMappingSettings_ = settings;
}

void PostProcessingStage::SetExposureSettings(const RENDER3D::COLOR::ExposureSettings& settings) {
    exposureSettings_ = settings;
}

void PostProcessingStage::SetFxaaSettings(const FxaaSettings& settings) {
    fxaaSettings_ = settings;
    fxaaSettings_.edgeThreshold = std::clamp(fxaaSettings_.edgeThreshold, 0.0312f, 0.333f);
    fxaaSettings_.edgeThresholdMin = std::clamp(fxaaSettings_.edgeThresholdMin, 0.0f, 0.0833f);
    fxaaSettings_.subpixelQuality = std::clamp(fxaaSettings_.subpixelQuality, 0.0f, 1.0f);
}

void PostProcessingStage::SetSunGlareFrame(const SunGlareFrame& frame) {
    const SunGlareFrame defaults{};
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    sunGlareFrame_ = frame;
    sunGlareFrame_.sunUvX = finiteOr(frame.sunUvX, defaults.sunUvX);
    sunGlareFrame_.sunUvY = finiteOr(frame.sunUvY, defaults.sunUvY);
    sunGlareFrame_.coreRadius = std::clamp(finiteOr(frame.coreRadius, defaults.coreRadius), 0.0005f, 0.25f);
    sunGlareFrame_.colorR = std::clamp(finiteOr(frame.colorR, defaults.colorR), 0.0f, 64.0f);
    sunGlareFrame_.colorG = std::clamp(finiteOr(frame.colorG, defaults.colorG), 0.0f, 64.0f);
    sunGlareFrame_.colorB = std::clamp(finiteOr(frame.colorB, defaults.colorB), 0.0f, 64.0f);
    sunGlareFrame_.intensity = std::clamp(finiteOr(frame.intensity, defaults.intensity), 0.0f, 64.0f);
    sunGlareFrame_.haloRadius =
        std::clamp(finiteOr(frame.haloRadius, defaults.haloRadius), sunGlareFrame_.coreRadius, 1.0f);
    sunGlareFrame_.haloFalloff = std::clamp(finiteOr(frame.haloFalloff, defaults.haloFalloff), 0.25f, 16.0f);
    sunGlareFrame_.streakIntensity = std::clamp(finiteOr(frame.streakIntensity, defaults.streakIntensity), 0.0f, 4.0f);
    sunGlareFrame_.streakLength = std::clamp(finiteOr(frame.streakLength, defaults.streakLength), 0.001f, 2.0f);
    sunGlareFrame_.occlusionRadius =
        std::clamp(finiteOr(frame.occlusionRadius, defaults.occlusionRadius), 0.001f, 0.25f);
    sunGlareFrame_.edgeFade = std::clamp(finiteOr(frame.edgeFade, defaults.edgeFade), 0.001f, 0.5f);
    sunGlareFrame_.maxRadiance = std::clamp(finiteOr(frame.maxRadiance, defaults.maxRadiance), 0.1f, 4096.0f);
    sunGlareFrame_.enabled =
        frame.enabled && sunGlareFrame_.intensity > 0.0f &&
        (sunGlareFrame_.colorR > 0.0f || sunGlareFrame_.colorG > 0.0f || sunGlareFrame_.colorB > 0.0f);
}

void PostProcessingStage::ClearSunGlareFrame() {
    sunGlareFrame_ = {};
    sunGlareDebugStats_ = {};
}

void PostProcessingStage::SetActionScreenImpulseFrame(
    const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source, const MATH::Mat4& viewProj,
    uint64_t frameIndex) {
    actionScreenImpulseProvider_.Submit(frameIndex, viewProj, source);
}

void PostProcessingStage::SetActionScreenDistortionMask(const PostProcessingStage::ActionScreenDistortionMask& mask) {
    actionScreenImpulseProvider_.SubmitMask(mask);
}

void PostProcessingStage::SetActionScreenDistortionAccent(
    const PostProcessingStage::ActionScreenDistortionAccent& accent) {
    actionScreenImpulseProvider_.SubmitAccent(accent);
}

void PostProcessingStage::SetTransitionState(const TransitionVisualState& state) {
    if (!state.active) {
        ClearTransitionState();
        return;
    }
    std::string profileId = state.profileId.empty() ? "noise_wipe" : state.profileId;
    if (activeTransitionProfileId_ != profileId || !transitionEffect_) {
        TransitionProfile profile{};
        bool profileReady = TransitionProfile::LoadById(profileId, profile);
        if (!profileReady) {
            profileId = "noise_wipe";
            profileReady = TransitionProfile::LoadById(profileId, profile);
        }
        std::unique_ptr<PostEffect> effect{};
        if (profileReady) {
            effect = std::make_unique<PostEffect>();
            profileReady = effect->LoadPixelShader({.programId = profile.programId, .entryId = profile.entryId});
        }
        if (profileReady) {
            activeTransitionProfile_ = std::move(profile);
            activeTransitionProfileId_ = profileId;
            transitionEffect_ = std::move(effect);
            transitionChain_.Clear();
            transitionChain_.Add(transitionEffect_.get());
        } else if (!transitionEffect_) {
            transitionActive_ = false;
            return;
        }
    }
    transitionActive_ = true;
    transitionParams_ = commonParams_;
    activeTransitionProfile_.ApplyToCommonParams(transitionParams_);
    transitionParams_.user[14] = {std::clamp(state.progress, 0.0f, 1.0f), state.isTransitionIn ? 1.0f : 0.0f, 0.0f,
                                  0.0f};
}

void PostProcessingStage::ClearTransitionState() {
    transitionActive_ = false;
    activeTransitionProfileId_.clear();
    activeTransitionProfile_ = TransitionProfile{};
    transitionChain_.Clear();
    transitionEffect_.reset();
    transitionParams_ = CommonParams{};
}

const FxaaSettings& PostProcessingStage::GetFxaaSettings() const { return fxaaSettings_; }

const BloomDebugStats& PostProcessingStage::GetBloomDebugStats() const { return bloomDebugStats_; }

const SunGlareDebugStats& PostProcessingStage::GetSunGlareDebugStats() const { return sunGlareDebugStats_; }

const ActionScreenImpulseProvider::DebugState& PostProcessingStage::GetActionScreenImpulseDebugState() const {
    return actionScreenImpulseProvider_.GetDebugState();
}

const RENDER3D::COLOR::ExposureFrameState& PostProcessingStage::GetExposureState() const { return exposureState_; }

const RENDER3D::COLOR::AutomaticExposureDebugState& PostProcessingStage::GetAutomaticExposureDebugState() const {
    return automaticExposureMeter_.GetDebugState();
}

void PostProcessingStage::RequestFrameDump() { dumpNextFrame_ = true; }

bool PostProcessingStage::EnsureToneMappingEffect() {
    if (toneMappingEffect_)
        return true;
    auto effect = std::make_unique<PostEffect>();
    if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_ToneMappingPS"})) {
        LogState("ToneMapping effect load failed");
        return false;
    }
    toneMappingEffect_ = std::move(effect);
    return true;
}

bool PostProcessingStage::EnsureFxaaEffect() {
    if (fxaaEffect_ && fxaaChain_.HasAny())
        return true;
    if (!fxaaEffect_) {
        auto effect = std::make_unique<PostEffect>();
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_FXAA"})) {
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
    if (sunGlareEffect_)
        return true;
    auto effect = std::make_unique<PostEffect>();
    if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_SunGlarePS"})) {
        LogState("Sun glare effect load failed");
        return false;
    }
    sunGlareEffect_ = std::move(effect);
    return true;
}

bool PostProcessingStage::EnsureActionScreenImpulseEffect() {
    if (actionScreenImpulseEffect_ && actionScreenImpulseChain_.HasAny()) {
        return true;
    }
    if (!actionScreenImpulseEffect_) {
        auto effect = std::make_unique<PostEffect>();
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_ActionScreenImpulse"})) {
            LogState("Action screen impulse effect load failed");
            return false;
        }
        actionScreenImpulseEffect_ = std::move(effect);
    }
    actionScreenImpulseChain_.Clear();
    actionScreenImpulseChain_.Add(actionScreenImpulseEffect_.get());
    return true;
}

bool PostProcessingStage::EnsureBloomEffects(uint32_t levelCount) {
    if (!bloomDownsampleEffect_ || !bloomUpsampleEffect_) {
        auto downsample = std::make_unique<PostEffect>();
        auto upsample = std::make_unique<PostEffect>();
        if (!downsample->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_BloomDownsamplePS"}) ||
            !upsample->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_BloomUpsamplePS"}))
            return false;
        bloomDownsampleEffect_ = std::move(downsample);
        bloomUpsampleEffect_ = std::move(upsample);
        activeBloomLevelCount_ = 0;
    }
    if (activeBloomLevelCount_ != levelCount) {
        // 全段が同じ縮小 shader を共有する。PostEffect は draw ごとの定数を保持する。
        for (uint32_t i = 0; i < bloomLevels_.size(); ++i) {
            bloomLevels_[i].Clear();
            if (i < levelCount)
                bloomLevels_[i].Add(bloomDownsampleEffect_.get());
        }
        activeBloomLevelCount_ = levelCount;
    }
    return true;
}

bool PostProcessingStage::EnsureLdrTarget(int width, int height, DXGI_FORMAT format) {
    if (width <= 0 || height <= 0 || format == DXGI_FORMAT_UNKNOWN)
        return false;
    ldrTarget_.UpdateContext(context_);
    const bool invalid = !ldrTarget_.GetResource() || ldrTarget_.GetWidth() != width ||
                         ldrTarget_.GetHeight() != height || ldrTarget_.GetFormat() != format || ldrTarget_.HasDepth();
    if (!invalid)
        return true;
    ldrTarget_.Finalize();
    ldrTarget_.SetDebugName("PostProcessing.ToneMappedLDR");
    return ldrTarget_.Init(width, height, format, false, {0, 0, 0, 1});
}

bool PostProcessingStage::EnsureTemporalOutput(int width, int height, DXGI_FORMAT format) {
    if (width <= 0 || height <= 0 || format == DXGI_FORMAT_UNKNOWN)
        return false;
    temporalOutput_.UpdateContext(context_);
    const bool invalid = !temporalOutput_.GetResource() || temporalOutput_.GetWidth() != width ||
                         temporalOutput_.GetHeight() != height || temporalOutput_.GetFormat() != format ||
                         temporalOutput_.HasDepth();
    if (!invalid)
        return true;
    temporalOutput_.Finalize();
    temporalOutput_.SetDebugName("PostProcessing.TemporalOutput");
    return temporalOutput_.Init(width, height, format, false, {0, 0, 0, 0});
}

bool PostProcessingStage::EnsureSunGlareTarget(int width, int height) {
    if (width <= 0 || height <= 0)
        return false;
    const int targetWidth = (std::max)((width + 3) / 4, 1);
    const int targetHeight = (std::max)((height + 3) / 4, 1);
    sunGlareTarget_.UpdateContext(context_);
    const bool invalid = !sunGlareTarget_.GetResource() || sunGlareTarget_.GetWidth() != targetWidth ||
                         sunGlareTarget_.GetHeight() != targetHeight ||
                         sunGlareTarget_.GetFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT || sunGlareTarget_.HasDepth();
    if (!invalid)
        return true;
    sunGlareTarget_.Finalize();
    sunGlareTarget_.SetDebugName("PostProcessing.SunGlareQuarterRes");
    return sunGlareTarget_.Init(targetWidth, targetHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false, {0, 0, 0, 0});
}

bool PostProcessingStage::ApplySunGlare(RenderTarget2D& source, RenderTarget2D& sceneDepthOwner, QuadDrawer& quad) {
    sunGlareDebugStats_ = {};
    sunGlareDebugStats_.requested = sunGlareFrame_.enabled;
    sunGlareDebugStats_.sunUvX = sunGlareFrame_.sunUvX;
    sunGlareDebugStats_.sunUvY = sunGlareFrame_.sunUvY;
    sunGlareDebugStats_.intensity = sunGlareFrame_.intensity;
    if (!sunGlareFrame_.enabled || !source.GetResource() || !sceneDepthOwner.GetDepthResource() ||
        sceneDepthOwner.GetDepthSrvGpu().ptr == 0) {
        return false;
    }
    if (!EnsureSunGlareEffect() || !EnsureSunGlareTarget(source.GetWidth(), source.GetHeight())) {
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
        quad.SetInputTexture(sceneDepthOwner.GetSrvHeap(), sceneDepthOwner.GetDepthSrvGpu());
        CommonParams parameters = commonParams_;
        parameters.resolutionX = static_cast<float>(sunGlareTarget_.GetWidth());
        parameters.resolutionY = static_cast<float>(sunGlareTarget_.GetHeight());
        // 光芒は表示参照の明るさ。前露出した HDR 目標へは P/E を掛けて足す。
        const float displayToScene = RENDER3D::COLOR::MakeViewExposureConstants(exposureState_).z;
        parameters.user[0] = {sunGlareFrame_.sunUvX, sunGlareFrame_.sunUvY, sunGlareFrame_.coreRadius,
                              sunGlareFrame_.intensity * displayToScene};
        parameters.user[1] = {sunGlareFrame_.colorR, sunGlareFrame_.colorG, sunGlareFrame_.colorB,
                              sunGlareFrame_.maxRadiance * displayToScene};
        parameters.user[2] = {sunGlareFrame_.haloRadius, sunGlareFrame_.haloFalloff, sunGlareFrame_.streakIntensity,
                              sunGlareFrame_.streakLength};
        parameters.user[3] = {sunGlareFrame_.occlusionRadius, sunGlareFrame_.edgeFade,
                              static_cast<float>(sceneDepthOwner.GetWidth()),
                              static_cast<float>(sceneDepthOwner.GetHeight())};
        parameters.user[4] = {sunGlareFrame_.shaftIntensity, sunGlareFrame_.shaftLength, sunGlareFrame_.shaftDecay,
                              sunGlareFrame_.ghostIntensity};
        sunGlareEffect_->ApplyCommonParams(parameters);
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
    quad.DrawBlended(sunGlareTarget_.GetSrvHeap(), sunGlareTarget_.GetSrvGpu(), BlendOption::Additive);
    source.EndCapture();
    sunGlareDebugStats_.active = true;
    sunGlareDebugStats_.initialized = true;
    return true;
}

RenderTarget2D* PostProcessingStage::NormalizeTemporalOutput(RenderTarget2D& source, uint32_t width, uint32_t height,
                                                             QuadDrawer& quad) {
    if (!EnsureTemporalOutput(static_cast<int>(width), static_cast<int>(height), source.GetFormat()))
        return nullptr;
    temporalOutput_.BeginCapture(0, 0, 0, 0);
    const bool ready = quad.SetOutputFormat(temporalOutput_.GetFormat());
    if (ready)
        quad.DrawFullscreen(source.GetSrvHeap(), source.GetSrvGpu());
    temporalOutput_.EndCapture();
    return ready ? &temporalOutput_ : nullptr;
}

RenderTarget2D* PostProcessingStage::ApplyBloom(RenderTarget2D& source, QuadDrawer& quad) {
    bloomDebugStats_ = {};
    bloomDebugStats_.enabled = bloomSettings_.enabled;
    bloomDebugStats_.threshold = bloomSettings_.threshold;
    bloomDebugStats_.intensity = bloomSettings_.intensity;
    bloomDebugStats_.radius = bloomSettings_.radius;
    bloomDebugStats_.downsampleCount =
        std::clamp<uint32_t>(bloomSettings_.downsampleCount, 1u, static_cast<uint32_t>(bloomLevels_.size()));
    bloomDebugStats_.textureWidth = source.GetWidth();
    bloomDebugStats_.textureHeight = source.GetHeight();
    if (!source.GetResource() || !bloomSettings_.enabled || bloomSettings_.intensity <= 0.0f) {
        return nullptr;
    }
    if (!EnsureBloomEffects(bloomDebugStats_.downsampleCount)) {
        bloomDebugStats_.failed = true;
        return nullptr;
    }
    // 半解像度から 1/2 ずつ縮小し、小さい段から順にテントで広げて一つ上の段へ足し戻す。
    // 各段は同じ明るさを持つので、全段の和が intensity 倍になるよう先頭の段で段数で割る。
    const uint32_t count = bloomDebugStats_.downsampleCount;
    // 閾値は露出後の明るさ。縮小 shader が場面色の空間へ P/E で戻す。
    const float displayToScene = RENDER3D::COLOR::MakeViewExposureConstants(exposureState_).z;
    std::array<RenderTarget2D*, std::tuple_size_v<decltype(bloomLevels_)>> outputs{};
    RenderTarget2D* input = &source;
    int width = (std::max)((source.GetWidth() + 1) / 2, 1);
    int height = (std::max)((source.GetHeight() + 1) / 2, 1);
    bloomDebugStats_.textureWidth = width;
    bloomDebugStats_.textureHeight = height;
    for (uint32_t i = 0; i < count; ++i) {
        CommonParams params = commonParams_;
        params.user[0] = {bloomSettings_.threshold, bloomSettings_.intensity / float(count), i == 0 ? 1.0f : 0.0f, 0};
        params.user[1] = {1.0f / float(input->GetWidth()), 1.0f / float(input->GetHeight()), displayToScene, 0};
        outputs[i] = bloomLevels_[i].Execute(*input, quad, params, width, height);
        if (outputs[i] == nullptr || outputs[i] == input || !outputs[i]->GetResource()) {
            bloomDebugStats_.failed = true;
            return nullptr;
        }
        input = outputs[i];
        width = (std::max)((width + 1) / 2, 1);
        height = (std::max)((height + 1) / 2, 1);
    }
    for (uint32_t i = count - 1; i > 0; --i) {
        CommonParams params = commonParams_;
        params.user[0] = {bloomSettings_.radius, 0, 0, 0};
        params.user[1] = {1.0f / float(outputs[i]->GetWidth()), 1.0f / float(outputs[i]->GetHeight()), 0, 0};
        outputs[i - 1]->Rebind();
        if (!quad.SetOutputFormat(outputs[i - 1]->GetFormat())) {
            outputs[i - 1]->EndCapture();
            bloomDebugStats_.failed = true;
            return nullptr;
        }
        quad.SetInputTexture(outputs[i]->GetSrvHeap(), outputs[i]->GetSrvGpu());
        bloomUpsampleEffect_->ApplyCommonParams(params);
        const bool drawn = bloomUpsampleEffect_->BindAndDraw(quad, true);
        outputs[i - 1]->EndCapture();
        if (!drawn) {
            bloomDebugStats_.failed = true;
            return nullptr;
        }
    }
    bloomDebugStats_.initialized = true;
    bloomDebugStats_.passCount = 2u * count - 1u;
    return outputs[0];
}

RenderTarget2D* PostProcessingStage::ApplyActionScreenImpulse(RenderTarget2D& source, QuadDrawer& quad) {
    auto& debug = actionScreenImpulseProvider_.MutableDebugState();
    const uint64_t absoluteFrame = TIME::GetFrameContext().frameIndex;
    debug.frameIndex = absoluteFrame;
    debug.gfxFrameSlot = context_.frameIndex;
    debug.requested = actionScreenImpulseProvider_.IsCurrent(absoluteFrame);
    debug.activeWaveCount = actionScreenImpulseProvider_.GetCount();
    debug.sourceFrameIndex = actionScreenImpulseProvider_.GetSourceFrameIndex();
    debug.submitFrameIndex = actionScreenImpulseProvider_.GetFrameIndex();
    const auto& mask = actionScreenImpulseProvider_.GetMask();
    debug.maskProvided = mask.texture.resource != nullptr;
    const auto& cinematicGrade = actionScreenImpulseProvider_.GetCinematicGrade();
    const auto& accent = actionScreenImpulseProvider_.GetAccent();
    debug.accentProvided = accent.texture.resource != nullptr;
    debug.cinematicGradeProvided = cinematicGrade.IsActive();
    debug.cinematicGradeWeight = cinematicGrade.weight;
    debug.boostGradeProvided = cinematicGrade.boostWeight > 0.0f && cinematicGrade.boostEdgeIntensity > 0.0f;
    debug.boostGradeWeight = cinematicGrade.boostWeight;
    const bool maskForFrame = mask.IsValid(absoluteFrame);
    const bool accentForFrame = accent.IsValid(absoluteFrame);
    debug.maskCompatible = maskForFrame && source.GetResource() != nullptr;
    debug.accentCompatible = accentForFrame && source.GetResource() != nullptr;
    if (!debug.requested || source.GetResource() == nullptr) {
        debug.bypassed = true;
        if (debug.sourceWaveCount != 0u && debug.projectionRejectCount != 0u &&
            debug.lastRejectReason != lastActionProjectionRejectReason_) {
            std::ostringstream stream;
            stream << "[CombatVfx][ScreenDistortion] wave projection rejected"
                   << " reason=" << debug.lastRejectReason << " sourceCount=" << debug.sourceWaveCount
                   << " rejected=" << debug.projectionRejectCount << " frame=" << absoluteFrame;
            LogCombatVfx(CORE::LogLevel::Info, stream.str());
            lastActionProjectionRejectReason_ = debug.lastRejectReason;
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
                source, debug.maskCompatible ? &mask : nullptr, debug.accentCompatible ? &accent : nullptr,
                cinematicGrade, params, absoluteFrame, compositeOutput, maskApplied, accentApplied)) {
            debug.pipelineReady = true;
            debug.executed =
                compositeOutput != nullptr && compositeOutput != &source && compositeOutput->GetResource() != nullptr;
            debug.maskApplied = maskApplied;
            debug.accentApplied = accentApplied;
            debug.cinematicGradeApplied = debug.executed && cinematicGrade.weight > 0.0f;
            debug.boostGradeApplied =
                debug.executed && cinematicGrade.boostWeight > 0.0f && cinematicGrade.boostEdgeIntensity > 0.0f;
            debug.composited = debug.executed;
            if (debug.composited) {
                if (!actionScreenRequestWasActive_) {
                    std::ostringstream stream;
                    stream << "[CombatVfx][ScreenDistortion] composited"
                           << " waves=" << debug.activeWaveCount << " mask=" << debug.maskApplied
                           << " accent=" << debug.accentApplied << " grade=" << debug.cinematicGradeApplied
                           << " boost=" << debug.boostGradeApplied << " frame=" << absoluteFrame;
                    LogCombatVfx(CORE::LogLevel::Info, stream.str());
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
    if ((debug.maskCompatible || cinematicGrade.IsActive()) && !actionCompositeFailureLogged_) {
        LogCombatVfx(CORE::LogLevel::Warn, "[CombatVfx][ScreenDistortion] dedicated composite failed; "
                                           "trail mask/cinematic grade cannot use the guard-only fallback");
        actionCompositeFailureLogged_ = true;
    }
    if (!EnsureActionScreenImpulseEffect()) {
        debug.failed = true;
        return &source;
    }
    debug.pipelineReady = true;

    RenderTarget2D* output = actionScreenImpulseChain_.Execute(source, quad, params);
    debug.executed = output != nullptr && output != &source && output->GetResource() != nullptr;
    debug.maskApplied = false;
    debug.composited = debug.executed;
    if (!debug.composited) {
        debug.failed = true;
        return &source;
    }
    if (!actionScreenRequestWasActive_) {
        std::ostringstream stream;
        stream << "[CombatVfx][ScreenDistortion] guard fallback composited"
               << " waves=" << debug.activeWaveCount << " frame=" << absoluteFrame;
        LogCombatVfx(CORE::LogLevel::Info, stream.str());
    }
    actionScreenRequestWasActive_ = true;
    return output;
}

RenderTarget2D* PostProcessingStage::ApplyFxaa(RenderTarget2D& source, QuadDrawer& quad) {
    if (!source.GetResource() || !EnsureFxaaEffect())
        return nullptr;
    CommonParams parameters = commonParams_;
    WriteFxaaParameters(parameters, source.GetWidth(), source.GetHeight(), fxaaSettings_);
    return fxaaChain_.Execute(source, quad, parameters);
}

RenderTarget2D* PostProcessingStage::ResolveHdr(RenderTarget2D& source, QuadDrawer& quad, bool debugOutput,
                                                const char* temporalBackend, bool lightingEnabled,
                                                RenderTarget2D* sceneDepthOwner,
                                                const RENDER3D::RenderTextureView* surfaceFeatureMask,
                                                RenderTarget2D* preservedVfx) {
    RenderTarget2D* output = &source;
    commonParams_.resolutionX = static_cast<float>((std::max)(1, output->GetWidth()));
    commonParams_.resolutionY = static_cast<float>((std::max)(1, output->GetHeight()));
    if (!debugOutput && sceneDepthOwner != nullptr) {
        (void)ApplySunGlare(*output, *sceneDepthOwner, quad);
    } else {
        sunGlareDebugStats_ = {};
        sunGlareDebugStats_.requested = sunGlareFrame_.enabled;
    }
    // 測光は芸術的な全体後処理の前に行う。後処理の色調が露出を引き戻さない。
    if (!debugOutput && meterScene_) {
        (void)automaticExposureMeter_.QueueMeasurement(*output, exposureState_.preExposure,
                                                       TIME::GetFrameContext().frameIndex);
    }
    displaySceneDepthOwner_ = debugOutput ? nullptr : sceneDepthOwner;
    displaySurfaceFeatureMask_ = debugOutput ? nullptr : surfaceFeatureMask;
    if (!debugOutput) {
        output = ExecuteGlobalLayers(PostEffectPhase::Scene, output, quad, sceneDepthOwner, surfaceFeatureMask);
    }
    if (!debugOutput) {
        // Scene-linear PMA (including additive RGB with zero coverage) enters once,
        // after metering/artistic Global Post and before sequence/action/bloom/output.
        if (preservedVfx && preservedVfx->GetResource()) {
            if (!quad.SetOutputFormat(output->GetFormat()))
                return nullptr;
            output->Rebind();
            quad.DrawBlended(preservedVfx->GetSrvHeap(), preservedVfx->GetSrvGpu(), BlendOption::PremultipliedAlpha, 0);
            output->EndCapture();
        }
        output = ApplySequenceEffects(*output, quad);
        // This is deliberately after temporal/global HDR processing and
        // after exposure measurement, but before bloom so the bright ring
        // and flash participate in the same bloom extraction as scene HDR.
        output = ApplyActionScreenImpulse(*output, quad);
        output = ApplyActionPost(*output, quad, surfaceFeatureMask);
    } else {
        auto& actionDebug = actionScreenImpulseProvider_.MutableDebugState();
        const uint64_t absoluteFrame = TIME::GetFrameContext().frameIndex;
        actionDebug.frameIndex = absoluteFrame;
        actionDebug.gfxFrameSlot = context_.frameIndex;
        actionDebug.requested = actionScreenImpulseProvider_.IsCurrent(absoluteFrame);
        actionDebug.bypassed = actionDebug.requested;
    }
    RenderTarget2D* bloom = debugOutput ? nullptr : ApplyBloom(*output, quad);
    if (bloom && bloom->GetResource()) {
        output->Rebind();
        if (quad.SetOutputFormat(output->GetFormat())) {
            float bloomCurvature = 0.0f;
            float bloomStrength = 0.0f;
            GetRadialScreenWarp(bloomCurvature, bloomStrength);
            quad.DrawBlended(bloom->GetSrvHeap(), bloom->GetSrvGpu(), BlendOption::Additive,
                             bloomCurvature * bloomStrength);
        }
        output->EndCapture();
    }
    // Scene transitions operate in scene-linear HDR. The single LDR resolve below
    // remains the owner of exposure, tone curve, and display transfer.
    if (!debugOutput && transitionActive_ && transitionEffect_) {
        transitionParams_.resolutionX = static_cast<float>((std::max)(1, output->GetWidth()));
        transitionParams_.resolutionY = static_cast<float>((std::max)(1, output->GetHeight()));
        output = transitionChain_.Execute(*output, quad, transitionParams_);
    }
    if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
        const auto& quality = RENDER3D::GetRenderQualitySettings();
        HIKARI_LOG_INFO(std::string("[PostProcessing][HDR] global=") + std::to_string(activeGlobalLayers_.size()) +
                        " bloom=" + (bloom && bloom->GetResource() ? "on" : "off") +
                        " temporal=" + (temporalBackend ? temporalBackend : "off") +
                        " sunGlare=" + (sunGlareDebugStats_.active ? "on" : "off") +
                        " fxaa=" + (RENDER3D::IsFxaaAntiAliasingMode(quality.antiAliasingMode) ? "on" : "off") +
                        " lighting=" + (lightingEnabled ? "on" : "off"));
    }
    return output;
}

RenderTarget2D* PostProcessingStage::ResolveDebugLdr(RenderTarget2D& source, DXGI_FORMAT outputFormat,
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

RenderTarget2D* PostProcessingStage::ResolveHdrBloomOnly(RenderTarget2D& source, QuadDrawer& quad) {
    commonParams_.resolutionX = static_cast<float>((std::max)(1, source.GetWidth()));
    commonParams_.resolutionY = static_cast<float>((std::max)(1, source.GetHeight()));
    RenderTarget2D* bloom = ApplyBloom(source, quad);
    if (bloom != nullptr && bloom->GetResource() != nullptr) {
        source.Rebind();
        if (quad.SetOutputFormat(source.GetFormat())) {
            float bloomCurvature = 0.0f;
            float bloomStrength = 0.0f;
            GetRadialScreenWarp(bloomCurvature, bloomStrength);
            quad.DrawBlended(bloom->GetSrvHeap(), bloom->GetSrvGpu(), BlendOption::Additive,
                             bloomCurvature * bloomStrength);
        }
        source.EndCapture();
    }
    return &source;
}

RenderTarget2D* PostProcessingStage::ResolveLdr(RenderTarget2D& source, DXGI_FORMAT outputFormat, QuadDrawer& quad,
                                                ID3D12Resource* sceneDepth, RenderTarget2D* lightTarget) {
    if (!EnsureLdrTarget(source.GetWidth(), source.GetHeight(), outputFormat))
        return nullptr;
    if (!EnsureToneMappingEffect())
        return nullptr;
    ldrTarget_.BeginCapture(0, 0, 0, 1);
    if (!quad.SetOutputFormat(ldrTarget_.GetFormat())) {
        ldrTarget_.EndCapture();
        return nullptr;
    }
    ID3D12Resource* gradingLut = colorGradingLut_.Prepare(context_, toneMappingSettings_.gamma);
    const ToneMappingInputs inputs(context_, quad, source, gradingLut, sceneDepth);
    if (!inputs) {
        quad.SetInputTexture(source.GetSrvHeap(), source.GetSrvGpu());
    }
    CommonParams parameters = commonParams_;
    float radialCurvature = 0.0f;
    float radialStrength = 0.0f;
    GetRadialScreenWarp(radialCurvature, radialStrength);
    WriteToneMappingParameters(parameters, toneMappingSettings_, RENDER3D::COLOR::ResidualExposure(exposureState_),
                               radialCurvature, radialStrength,
                               inputs && gradingLut ? RENDER3D::COLOR::kColorGradingLutSize : 0u);
    toneMappingEffect_->ApplyCommonParams(parameters);
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
        if (fxaa && fxaa->GetResource())
            output = fxaa;
    }
    // 表示の色の層は FXAA の後に通し、画素や網点の縁を均さない。HDR の層の一時目標はもう読まれないので共有する。
    output = ExecuteGlobalLayers(PostEffectPhase::Display, output, quad, displaySceneDepthOwner_,
                                 displaySurfaceFeatureMask_);
    displaySceneDepthOwner_ = nullptr;
    displaySurfaceFeatureMask_ = nullptr;
    if (dumpNextFrame_ || GFX::GetGfxDebugConfig().verbosePostLog) {
        LogState(dumpNextFrame_ ? "Requested frame dump" : "Verbose post log");
        dumpNextFrame_ = false;
    }
    return output;
}

std::string PostProcessingStage::DumpState() const {
    std::ostringstream stream;
    stream << "[PostProcessingStage] global=" << activeGlobalLayers_.size() << " bloom=" << bloomSettings_.enabled
           << " bloomPasses=" << bloomDebugStats_.passCount << " sunGlare=" << sunGlareDebugStats_.active
           << " sunGlareRequested=" << sunGlareDebugStats_.requested
           << " actionScreenImpulse=" << actionScreenImpulseProvider_.GetDebugState().composited
           << " actionScreenImpulseRequested=" << actionScreenImpulseProvider_.GetDebugState().requested
           << " actionScreenImpulseWaves=" << actionScreenImpulseProvider_.GetDebugState().activeWaveCount
           << " actionScreenImpulseSourceFrame=" << actionScreenImpulseProvider_.GetDebugState().sourceFrameIndex
           << " actionScreenImpulseSubmitFrame=" << actionScreenImpulseProvider_.GetDebugState().submitFrameIndex
           << " actionScreenImpulseGfxSlot=" << actionScreenImpulseProvider_.GetDebugState().gfxFrameSlot
           << " actionScreenImpulseProjected=" << actionScreenImpulseProvider_.GetDebugState().projectedWaveCount
           << " actionScreenImpulseDropped=" << actionScreenImpulseProvider_.GetDebugState().droppedWaveCount
           << " actionScreenImpulseRejects=" << actionScreenImpulseProvider_.GetDebugState().projectionRejectCount
           << " actionScreenImpulseRejectReason=" << actionScreenImpulseProvider_.GetDebugState().lastRejectReason
           << " actionScreenImpulseMask=" << actionScreenImpulseProvider_.GetDebugState().maskApplied
           << " actionScreenImpulseMaskProvided=" << actionScreenImpulseProvider_.GetDebugState().maskProvided
           << " actionScreenImpulseMaskCompatible=" << actionScreenImpulseProvider_.GetDebugState().maskCompatible
           << " actionScreenImpulseGrade=" << actionScreenImpulseProvider_.GetDebugState().cinematicGradeApplied
           << " actionScreenImpulseGradeWeight=" << actionScreenImpulseProvider_.GetDebugState().cinematicGradeWeight
           << " actionScreenImpulseFailed=" << actionScreenImpulseProvider_.GetDebugState().failed
           << " actionScreenImpulseBypassed=" << actionScreenImpulseProvider_.GetDebugState().bypassed
           << " toneCurve=" << static_cast<int>(toneMappingSettings_.curve)
           << " exposureMode=" << TEXT::EnumToName(RENDER3D::COLOR::kExposureModeNames, exposureState_.mode)
           << " exposure=" << exposureState_.exposure << " exposureTarget=" << exposureState_.targetExposure
           << " meteredLuminance=" << exposureState_.meteredLuminance
           << " meteringValid=" << exposureState_.meteringValid << " preExposure=" << exposureState_.preExposure
           << " transition=" << transitionActive_ << "\n  " << ldrTarget_.DumpState() << "\n  "
           << temporalOutput_.DumpState() << "\n  " << sunGlareTarget_.DumpState() << "\n  " << globalChain_.DumpState()
           << "\n  " << bloomLevels_[0].DumpState() << "\n  " << transitionChain_.DumpState() << "\n  "
           << fxaaChain_.DumpState();
    return stream.str();
}

void PostProcessingStage::LogState(const char* reason) const {
    DEBUGLOG::PushRenderError(std::string("[PostProcessingStage][DUMP] reason=") + (reason ? reason : "") + "\n" +
                              DumpState());
}

} // namespace HIKARI::POST
