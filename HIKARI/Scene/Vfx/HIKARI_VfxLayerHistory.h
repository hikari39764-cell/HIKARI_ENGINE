#pragma once
#include <optional>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::VFX::PLAYBACK {
    // 有効なレイヤーだけが確保する実測姿勢履歴。描画・ソース編集には依存しない。
    class LayerHistory {
    public:
        void Clear() noexcept { samples_.clear(); previous_.reset(); }
        void Project(const VfxHistoryData& settings, float time,
            RENDER3D::PARTICLES::ParticleEmitterSubmission& submission);
    private:
        struct Sample {
            float time = 0;
            RENDER3D::PARTICLES::ParticleShapePose pose{};
        };
        std::vector<Sample> samples_{};
        std::optional<Sample> previous_{};
        float nextSampleTime_ = 0;
        float sampleInterval_ = 0;
    };
}
