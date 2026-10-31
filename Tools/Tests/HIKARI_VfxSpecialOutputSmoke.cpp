// VFX 特殊出力の契約: 資産 → JSON 往復 → 検証 → コンパイル → 再生サンプル →
// シーンの光源 / 画面波。GPU を使わないので CI でも走る。
#include <cmath>
#include <cstdio>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_VfxLightOutput.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxScreenOutput.h"

namespace {
using namespace HIKARI;
using namespace HIKARI::VFX;

void Require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}

VfxSpecialOutputData MakeLightOutput(float flickerFrequency, float flickerAmplitude) {
    VfxSpecialOutputData output{};
    output.id = "emitter.main.flash.light";
    output.type = VfxSpecialOutputType::Light;
    output.enabled = true;
    output.light.color = {1.0f, 1.0f, 1.0f, 1.0f};
    output.light.luminousFlux = 2400.0f;
    output.light.range = 4.5f;
    output.light.sourceRadius = 0.08f;
    output.light.flickerFrequency = flickerFrequency;
    output.light.flickerAmplitude = flickerAmplitude;
    output.light.priority = 2u;
    output.light.castsShadows = false;
    return output;
}

VfxSpecialOutputData MakeScreenOutput() {
    VfxSpecialOutputData output{};
    output.id = "emitter.main.flash.screen";
    output.type = VfxSpecialOutputType::ScreenImpulse;
    output.enabled = true;
    output.screenImpulse.durationSeconds = 0.5f;
    output.screenImpulse.maxRadius = 0.42f;
    output.screenImpulse.ringThickness = 0.09f;
    output.screenImpulse.distortion = 0.03f;
    output.screenImpulse.chromaticAberration = 0.002f;
    output.screenImpulse.flash = 0.05f;
    output.screenImpulse.intensity = 1.2f;
    output.screenImpulse.color = {0.4f, 0.8f, 1.6f, 1.0f};
    output.screenImpulse.waveFrequency = 3.0f;
    output.screenImpulse.waveAmplitude = 0.5f;
    output.screenImpulse.waveDamping = 1.4f;
    output.screenImpulse.waveCount = 2u;
    return output;
}

VfxAssetData MakeAsset(float flickerFrequency, float flickerAmplitude, bool withScreenOutput) {
    VfxAssetData asset = MakeDefaultVfxAssetData();
    asset.name = "Special output contract";
    asset.durationSeconds = 1.0f;
    VfxEmitterData& layer = asset.emitters.front();
    // 光源色は「レイヤーが実際に出している色」に合わせる。Shape レイヤーは
    // 色サンプルがそのまま描画色になるため、契約の検証に使う。
    layer.kind = VfxLayerKind::Shape;
    layer.renderShape.type = VfxRenderShapeType::Sprite;
    layer.renderCommon.blendMode = VfxBlendMode::Additive;
    layer.startDelay = 0.0f;
    layer.activeDuration = 0.5f;
    layer.specialOutputs.push_back(MakeLightOutput(flickerFrequency, flickerAmplitude));
    if (withScreenOutput) {
        layer.specialOutputs.push_back(MakeScreenOutput());
    }
    // 光源色はレイヤー色と乗算される。契約として色を固定しておく。
    layer.color.value.mode = VfxAnimatedColorMode::Fixed;
    layer.color.value.fixed = {
        VfxDistributionMode::Constant,
        {1.0f, 0.5f, 0.25f, 1.0f},
        {1.0f, 0.5f, 0.25f, 1.0f},
        0u,
    };
    return asset;
}

void CheckSourceAndProgramRejectTheSameInvalidLight() {
    const auto valid = MakeAsset(0.0f, 0.0f, false);
    const auto compiled = CompileVfxAsset(valid);
    Require(compiled.success, "valid light did not compile");
    const auto reject = [&](auto corrupt) {
        auto source = valid;
        auto program = compiled.program;
        corrupt(source.emitters.front().specialOutputs.front().light);
        corrupt(program.emitters.front().specialOutputs.front().light);
        std::string error;
        Require(HasVfxErrors(ValidateVfxAsset(source)), "source accepted invalid light");
        Require(!ValidateVfxProgram(program, error), "cooked program accepted invalid light");
    };
    reject([](auto& light) { light.range = 0; });
    reject([](auto& light) { light.sourceRadius = -1; });
    reject([](auto& light) { light.luminousFlux = std::numeric_limits<float>::infinity(); });
    reject([](auto& light) { light.color.w = std::numeric_limits<float>::quiet_NaN(); });
    reject([](auto& light) { light.flickerFrequency = std::numeric_limits<float>::quiet_NaN(); });
}

void CheckSerializationRoundTrip() {
    const VfxAssetData asset = MakeAsset(0.0f, 0.0f, true);
    std::string text{};
    std::string error{};
    Require(SERIALIZATION::SerializeVfxSourceJson(asset, text, error), "serialize failed: " + error);
    VfxAssetData restored{};
    Require(SERIALIZATION::ParseVfxSourceJson(text, restored, error), "parse failed: " + error);
    Require(restored.emitters.size() == asset.emitters.size(), "emitter count changed across the round trip");
    const auto& outputs = restored.emitters.front().specialOutputs;
    Require(outputs.size() == 2u, "special outputs were not preserved");
    const VfxSpecialOutputData& light = outputs.front();
    Require(light.type == VfxSpecialOutputType::Light, "light output type changed");
    Require(std::fabs(light.light.luminousFlux - 2400.0f) < 1.0e-2f && std::fabs(light.light.range - 4.5f) < 1.0e-4f &&
                std::fabs(light.light.sourceRadius - 0.08f) < 1.0e-4f && light.light.priority == 2u &&
                !light.light.castsShadows,
            "light profile values changed across the round trip");
    const VfxSpecialOutputData& screen = outputs.back();
    Require(screen.type == VfxSpecialOutputType::ScreenImpulse, "screen output type changed");
    Require(std::fabs(screen.screenImpulse.durationSeconds - 0.5f) < 1.0e-4f &&
                std::fabs(screen.screenImpulse.distortion - 0.03f) < 1.0e-4f && screen.screenImpulse.waveCount == 2u,
            "screen profile values changed across the round trip");
    const std::vector<VfxDiagnostic> diagnostics = ValidateVfxAsset(restored);
    Require(!HasVfxErrors(diagnostics), "special output failed asset validation");
}

void CompileAndPlay(VfxPlaybackService& playback, const VfxAssetData& asset) {
    playback.Reset();
    const VfxCompileResult compiled = CompileVfxAsset(asset);
    Require(compiled.success, "special output asset did not compile");
    VfxPlayRequest request{};
    request.assetGuid = GenerateAssetGuid();
    request.program = std::make_shared<const VfxProgram>(compiled.program);
    Require(playback.PlayDetailed(request).IsAccepted(), "special output playback was rejected");
}

void CheckLightReachesSceneLighting() {
    VfxPlaybackService playback{};
    CompileAndPlay(playback, MakeAsset(0.0f, 0.0f, false));
    playback.Update(1u, 1.0f / 60.0f);
    std::vector<VfxLayerLightSample> samples{};
    playback.CollectLightOutputs(samples);
    Require(samples.size() == 1u, "active layer did not publish one light sample");
    const VfxLayerLightSample& sample = samples.front();
    Require(std::fabs(sample.luminousFlux - 2400.0f) < 1.0e-2f, "light sample flux diverged");
    Require(std::fabs(sample.range - 4.5f) < 1.0e-4f, "light sample range diverged");
    Require(std::fabs(sample.color.y - 0.5f) < 1.0e-4f && std::fabs(sample.color.z - 0.25f) < 1.0e-4f,
            "light colour was not multiplied by the layer colour");

    std::vector<RENDER3D::LIGHTING::SceneLightProvider> providers{};
    Require(AppendVfxLightProviders(playback, providers) == 1u,
            "light sample was not bridged into the scene lighting set");
    const auto& provider = providers.front();
    Require(provider.enabled && provider.type == RENDER3D::LIGHTING::SceneLightType::Point,
            "bridged provider has the wrong kind");
    // 光束 [lm] は点光源の光度 [cd] = lm / 4π で描画へ渡る。
    Require(std::fabs(provider.intensity - 2400.0f / (4.0f * 3.14159265f)) < 1.0e-2f &&
                std::fabs(provider.range - 4.5f) < 1.0e-4f && provider.priority == 2 &&
                std::fabs(provider.color.y - 0.5f) < 1.0e-4f,
            "bridged provider does not match the authored profile");
}

void CheckFlickerFollowsEffectTime() {
    // 明滅は wall clock ではなくエフェクト内時刻で評価する。
    VfxPlaybackService playback{};
    CompileAndPlay(playback, MakeAsset(12.0f, 0.5f, false));
    const auto intensityAfter = [&playback](uint64_t frameIndex, float delta) {
        playback.Update(frameIndex, delta);
        std::vector<RENDER3D::LIGHTING::SceneLightProvider> providers{};
        AppendVfxLightProviders(playback, providers);
        Require(!providers.empty(), "flickering layer produced no provider");
        return providers.front().intensity;
    };
    // 12Hz なら 1/48 秒ごとに位相が 90 度進む。
    const float first = intensityAfter(1u, 1.0f / 48.0f);
    const float second = intensityAfter(2u, 1.0f / 48.0f);
    Require(std::fabs(first - second) > 1.0e-3f, "flicker did not change with the effect time");
    Require(first > 0.0f && second > 0.0f, "flicker produced a negative or zero intensity");
}

void CheckLightVisibilityAndFade() {
    auto asset = MakeAsset(0, 0, false);
    asset.emitters.front().color.value.fixed.minimum.w = 0.25f;
    asset.emitters.front().color.value.fixed.maximum.w = 0.25f;
    VfxPlaybackService playback{17};
    CompileAndPlay(playback, asset);
    playback.Update(1, 1.0f / 60);
    std::vector<RENDER3D::LIGHTING::SceneLightProvider> providers;
    AppendVfxLightProviders(playback, providers);
    // 不透明度 0.25 で光束 2400 lm が 600 lm、光度 600 / 4π cd になる。
    Require(providers.size() == 1 && std::abs(providers[0].intensity - 600.0f / (4.0f * 3.14159265f)) < 1e-2f,
            "Layer opacity did not fade light intensity");
    const uint8_t hidden[]{0};
    providers.clear();
    Require(AppendVfxLightProviders(playback, providers, hidden) == 0,
            "Hidden preview layer kept illuminating other layers");
}

void CheckMultipleOutputsAndActivationWindow() {
    auto asset = MakeAsset(0, 0, true);
    auto& layer = asset.emitters.front();
    layer.kind = VfxLayerKind::Particles;
    layer.startDelay = 0.2f;
    layer.activeDuration = 0.5f;
    layer.specialOutputs[1].screenImpulse.durationSeconds = 0.1f;
    auto light = layer.specialOutputs[0];
    light.id += ".second";
    layer.specialOutputs.push_back(light);
    auto wave = layer.specialOutputs[1];
    wave.id += ".second";
    layer.specialOutputs.push_back(wave);
    VfxPlaybackService playback;
    CompileAndPlay(playback, asset);
    std::vector<RENDER3D::LIGHTING::SceneLightProvider> lights;
    VfxScreenWaveBatch waves;
    playback.Update(1, 0.1f);
    Require(AppendVfxLightProviders(playback, lights) == 0 && CollectVfxScreenWaves(playback, waves) == 0,
            "special output appeared before emission start");
    playback.Update(2, 0.15f);
    Require(AppendVfxLightProviders(playback, lights) == 2 && lights[0].id != lights[1].id,
            "multiple authored lights were discarded or share a provider ID");
    Require(CollectVfxScreenWaves(playback, waves) == 2 && std::abs(waves.waves[0].ageSeconds - 0.05f) < 1e-4f,
            "multiple screen outputs or layer-relative age were lost");
    playback.Update(3, 0.1f);
    Require(CollectVfxScreenWaves(playback, waves) == 0, "screen wave duration was stretched to the layer duration");
    playback.Update(4, 0.2f);
    playback.Update(5, 0.2f);
    lights.clear();
    Require(AppendVfxLightProviders(playback, lights) == 0, "particle emitter light outlived its activation window");
}

void CheckScreenWavesReachThePostFrameSource() {
    VfxPlaybackService playback{};
    CompileAndPlay(playback, MakeAsset(0.0f, 0.0f, true));
    playback.Update(1u, 1.0f / 60.0f);
    VfxScreenWaveBatch batch{};
    Require(CollectVfxScreenWaves(playback, batch) == 1u, "screen output did not publish one wave");
    const auto& wave = batch.waves.front();
    Require(std::fabs(wave.maximumScreenRadius - 0.42f) < 1.0e-4f && std::fabs(wave.ringThickness - 0.09f) < 1.0e-4f &&
                std::fabs(wave.distortionStrength - 0.03f) < 1.0e-4f && std::fabs(wave.intensity - 1.2f) < 1.0e-4f &&
                wave.waveCount == 2u,
            "screen wave does not match the authored profile");
    Require(wave.ageSeconds > 0.0f && wave.ageSeconds < wave.durationSeconds,
            "screen wave age was not derived from the effect time");

    ActionPostReactionService reactions{};
    reactions.Tick(1.0f / 60.0f, 1u);
    Require(SubmitVfxScreenWaves(playback, reactions), "screen wave was not submitted");
    Require(reactions.GetFrameSource().count == 1u, "post frame source did not receive the VFX wave");
    reactions.Tick(1.0f / 60.0f, 2u);
    Require(reactions.GetFrameSource().count == 0u && reactions.GetActiveCount() == 0u,
            "omitted VFX submission kept the previous frame's distortion");
    Require(SubmitVfxScreenWaves(playback, reactions), "wave resubmission failed");
    // 投稿しなかったフレームでは波が残らない。
    playback.Update(2u, 1.0f / 60.0f);
    playback.Reset();
    VfxScreenWaveBatch empty{};
    Require(CollectVfxScreenWaves(playback, empty) == 0u, "retired playback still publishes screen waves");
    Require(!SubmitVfxScreenWaves(playback, reactions) && reactions.GetFrameSource().count == 0u,
            "empty VFX submission did not remove the previous wave");
}
} // namespace

int wmain() {
    try {
        CheckSerializationRoundTrip();
        CheckSourceAndProgramRejectTheSameInvalidLight();
        CheckLightReachesSceneLighting();
        CheckFlickerFollowsEffectTime();
        CheckLightVisibilityAndFade();
        CheckMultipleOutputsAndActivationWindow();
        CheckScreenWavesReachThePostFrameSource();
    } catch (const std::exception& error) {
        std::printf("[special-output] %s\n", error.what());
        return 1;
    }
    std::printf("VFX special output contracts passed.\n");
    return 0;
}
