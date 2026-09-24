#pragma once
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"
#include <algorithm>
#include <vector>

namespace HIKARI::SEQUENCER {
// Scene が所有する一時値。SceneEnvironment と profile 資産は変更しない。
class SequencePresentationState {
public:
    void Set(CameraOverrideToken token, std::vector<ScreenEffectSample> effects) {
        Erase(token);
        entries_.push_back({token, std::move(effects)});
    }
    void Erase(CameraOverrideToken token) {
        std::erase_if(entries_,
                      [&](const auto& e) { return e.token.value == token.value && e.token.epoch == token.epoch; });
    }
    void Resolve(const CameraDirector& director, BloomSettings& bloom, ToneMappingSettings& tone,
                 std::vector<ScreenEffectSample>& output) const {
        output.clear();
        for (const auto& entry : entries_) {
            if (!director.IsWinningOverride(entry.token))
                continue;
            for (const auto& e : entry.effects) {
                const auto blend = [&](float value) { return value + (e.amount - value) * e.weight; };
                switch (e.kind) {
                case ScreenEffectKind::BloomIntensity:
                    bloom.intensity = (std::max)(0.0f, blend(bloom.enabled ? bloom.intensity : 0.0f));
                    bloom.enabled = bloom.intensity > 0;
                    break;
                case ScreenEffectKind::BloomThreshold:
                    bloom.threshold = (std::max)(0.0f, blend(bloom.threshold));
                    break;
                case ScreenEffectKind::Exposure:
                    tone.exposure = (std::max)(0.001f, blend(tone.exposure));
                    tone.exposureMode = RENDER3D::COLOR::ExposureMode::Manual;
                    break;
                case ScreenEffectKind::Saturation: tone.saturation = (std::max)(0.0f, blend(tone.saturation)); break;
                case ScreenEffectKind::Contrast: tone.contrast = (std::max)(0.0f, blend(tone.contrast)); break;
                case ScreenEffectKind::Temperature:
                    tone.whiteBalanceTemperature = std::clamp(blend(tone.whiteBalanceTemperature), 1000.0f, 40000.0f);
                    break;
                default: output.push_back(e); break;
                }
            }
        }
    }

private:
    struct Entry {
        CameraOverrideToken token;
        std::vector<ScreenEffectSample> effects;
    };
    std::vector<Entry> entries_;
};
} // namespace HIKARI::SEQUENCER
