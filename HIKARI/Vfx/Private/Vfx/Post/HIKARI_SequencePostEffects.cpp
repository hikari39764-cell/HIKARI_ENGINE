#include "Core/HIKARI_TimeService.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostProcessingStage.h"

namespace HIKARI::POST {
void PostProcessingStage::SetSequenceEffects(std::span<const SEQUENCER::ScreenEffectSample> effects) {
    sequenceEffects_.assign(effects.begin(), effects.end());
    sequenceEffectsFrame_ = TIME::GetFrameContext().frameIndex;
}

RenderTarget2D* PostProcessingStage::ApplySequenceEffects(RenderTarget2D& source, QuadDrawer& quad) {
    if (sequenceEffects_.empty() || sequenceEffectsFrame_ != TIME::GetFrameContext().frameIndex)
        return &source;
    if (!sequenceEffect_) {
        auto effect = std::make_unique<PostEffect>();
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_SequenceEffectPS", .entryId = "Main"}))
            return &source;
        sequenceEffect_ = std::move(effect);
        sequenceChain_.Add(sequenceEffect_.get());
    }
    RenderTarget2D* output = &source;
    for (const auto& e : sequenceEffects_) {
        CommonParams params = commonParams_;
        params.user[0] = {static_cast<float>(e.kind), e.amount, e.weight, 0};
        params.user[1] = {e.color.x, e.color.y, e.color.z, 0};
        output = sequenceChain_.Execute(*output, quad, params);
    }
    return output;
}
} // namespace HIKARI::POST
