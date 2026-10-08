#include "Scene/Vfx/HIKARI_VfxLightOutput.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::VFX {
namespace {
std::string MakeProviderId(uint64_t playbackNamespace, const VfxLayerLightSample& sample) {
    return "vfx.light." + std::to_string(playbackNamespace) + "." + std::to_string(sample.handle.slot) + "." +
           std::to_string(sample.handle.generation) + "." + std::to_string(sample.emitterIndex) + "." +
           std::to_string(sample.outputIndex);
}

// 点滅はエフェクト内時刻で評価する。wall clock を使うとヒットストップや
// シークで位相が飛び、撮影したフレームが再現しなくなる。
float ResolveFlicker(const VfxLayerLightSample& sample) {
    if (!(sample.flickerAmplitude > 0.0f) || !(sample.flickerFrequency > 0.0f)) {
        return 1.0f;
    }
    const float phase = sample.ageSeconds * sample.flickerFrequency * 6.28318530718f;
    return (std::max)(0.0f, 1.0f + sample.flickerAmplitude * std::sin(phase));
}

bool IsPreferred(const VfxLayerLightSample& candidate, const VfxLayerLightSample& current) noexcept {
    if (candidate.priority != current.priority) {
        return candidate.priority > current.priority;
    }
    return candidate.luminousFlux > current.luminousFlux;
}
} // namespace

size_t AppendVfxLightProviders(const VfxPlaybackService& playback,
                               std::vector<RENDER3D::LIGHTING::SceneLightProvider>& outProviders,
                               std::span<const uint8_t> visibleLayers) {
    std::vector<VfxLayerLightSample> samples{};
    playback.CollectLightOutputs(samples);
    std::erase_if(samples, [visibleLayers](const auto& sample) {
        return !visibleLayers.empty() &&
               (sample.emitterIndex >= visibleLayers.size() || visibleLayers[sample.emitterIndex] == 0);
    });
    for (auto& sample : samples)
        sample.luminousFlux *= std::clamp(sample.color.w, 0.0f, 1.0f);
    if (samples.empty())
        return 0u;
    if (samples.size() > kMaximumVfxLightOutputs) {
        std::partial_sort(
            samples.begin(), samples.begin() + kMaximumVfxLightOutputs, samples.end(),
            [](const VfxLayerLightSample& left, const VfxLayerLightSample& right) { return IsPreferred(left, right); });
        samples.resize(kMaximumVfxLightOutputs);
    }

    size_t appended = 0u;
    for (const VfxLayerLightSample& sample : samples) {
        const float flicker = ResolveFlicker(sample);
        const float luminousFlux = sample.luminousFlux * flicker;
        if (!(luminousFlux > 0.0f))
            continue;
        RENDER3D::LIGHTING::SceneLightProvider provider{};
        provider.id = MakeProviderId(playback.GetPlaybackNamespace(), sample);
        provider.name = provider.id;
        provider.enabled = true;
        provider.type = RENDER3D::LIGHTING::SceneLightType::Point;
        provider.position = sample.position;
        provider.color = {
            (std::max)(sample.color.x, 0.0f),
            (std::max)(sample.color.y, 0.0f),
            (std::max)(sample.color.z, 0.0f),
        };
        provider.intensity = RENDER3D::LIGHTING::LuminousIntensityFromFlux(luminousFlux);
        provider.range = sample.range;
        provider.sourceRadius = sample.sourceRadius;
        provider.castsShadows = sample.castsShadows;
        provider.priority = static_cast<int>(sample.priority);
        outProviders.push_back(std::move(provider));
        ++appended;
    }
    return appended;
}

} // namespace HIKARI::VFX
