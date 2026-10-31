#include "Vfx/Post/HIKARI_PostProcessingStage.h"
#include "Vfx/Post/HIKARI_ActionPostProfile.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "HIKARI/Project/ActionTypes.h"
#include <algorithm>

namespace HIKARI::POST {
bool IsActionPostEffectCompatible(const PostEffectDefinition& effect) noexcept {
    return !effect.screenWarp && !effect.requiresSceneDepth &&
           std::all_of(effect.passes.begin(), effect.passes.end(),
                       [](const auto& pass) { return pass.phase == PostEffectPhase::Scene; });
}
PostParameterBlock ResolveActionPostParameters(const PostEffectDefinition& effect, const PostProfileAsset& profile,
                                               const GameProjectActionPostFrameRequest& request) {
    static_assert(kPostParameterSlotCount == kGameProjectActionPostSlotCount);
    auto values = ResolvePostParameters(effect, &profile);
    for (size_t slot = 0; slot < values.size(); ++slot) {
        const auto& input = request.values[slot];
        const float channels[]{input.x, input.y, input.z, input.w};
        float* output[]{&values[slot].x, &values[slot].y, &values[slot].z, &values[slot].w};
        for (uint32_t channel = 0; channel < 4u; ++channel)
            if (request.channelMasks[slot] & (1u << channel))
                *output[channel] = channels[channel];
    }
    return values;
}

bool PostProcessingStage::SetActionPostFrame(uint64_t worldId, uint64_t frameIndex, std::string_view effectId,
                                             std::shared_ptr<const PostEffectDefinition> effect,
                                             const PostParameterBlock& values, const AssetRegistry& registry) {
    if (worldId == 0u || frameIndex != TIME::GetFrameContext().frameIndex || effectId.empty() || !effect ||
        !IsActionPostEffectCompatible(*effect)) {
        ClearActionPostFrame();
        return false;
    }
    if (actionPostWorld_ != worldId || actionPostEffectId_ != effectId || actionPostEffect_ != effect ||
        actionPostRegistry_ != &registry || actionPostRegistryId_ != registry.GetInstanceId() ||
        actionPostRegistryRevision_ != registry.GetRevision() ||
        actionPostDeviceGeneration_ != context_.deviceGeneration || actionPostPasses_.empty()) {
        ClearActionPostFrame();
        if (!BuildPostEffectPasses(*effect, &registry, actionPostPasses_)) {
            DEBUGLOG::PushRenderError("[ActionPost] Effect could not be prepared; channel skipped.");
            return false;
        }
        for (const auto& pass : actionPostPasses_)
            actionPostChain_.Add(pass.get());
        actionPostEffect_ = std::move(effect);
        actionPostEffectId_ = effectId;
        actionPostRegistry_ = &registry;
        actionPostRegistryId_ = registry.GetInstanceId();
        actionPostRegistryRevision_ = registry.GetRevision();
        actionPostDeviceGeneration_ = context_.deviceGeneration;
        actionPostWorld_ = worldId;
    }
    actionPostFrame_ = frameIndex;
    actionPostViewWorld_ = 0u;
    actionPostValues_ = values;
    return true;
}

void PostProcessingStage::SetActionPostView(uint64_t worldId, bool primaryGameView) noexcept {
    actionPostViewWorld_ = primaryGameView ? worldId : 0u;
}

bool PostProcessingStage::IsActionPostCurrent() const noexcept {
    return actionPostWorld_ != 0u && actionPostViewWorld_ == actionPostWorld_ && actionPostEffect_ &&
           actionPostFrame_ == TIME::GetFrameContext().frameIndex &&
           actionPostDeviceGeneration_ == context_.deviceGeneration && actionPostRegistry_ &&
           actionPostRegistryId_ == actionPostRegistry_->GetInstanceId() &&
           actionPostRegistryRevision_ == actionPostRegistry_->GetRevision();
}

bool PostProcessingStage::RequiresActionSurfaceFeatureMask() const noexcept {
    return IsActionPostCurrent() && actionPostEffect_->requiresSurfaceFeatureMask;
}

void PostProcessingStage::ClearActionPostFrame(uint64_t worldId) {
    if (worldId != 0u && actionPostWorld_ != worldId)
        return;
    actionPostChain_.Finalize();
    actionPostPasses_.clear();
    actionPostEffect_.reset();
    actionPostEffectId_.clear();
    actionPostValues_ = {};
    actionPostRegistry_ = nullptr;
    actionPostRegistryId_ = actionPostRegistryRevision_ = actionPostWorld_ = actionPostViewWorld_ = 0u;
    actionPostDeviceGeneration_ = 0u;
    actionPostFrame_ = (std::numeric_limits<uint64_t>::max)();
}

RenderTarget2D* PostProcessingStage::ApplyActionPost(RenderTarget2D& source, QuadDrawer& quad,
                                                     const RENDER3D::RenderTextureView* surfaceFeatureMask) {
    if (!IsActionPostCurrent()) {
        ClearActionPostFrame();
        return &source;
    }
    if (actionPostEffect_->requiresSurfaceFeatureMask &&
        (!surfaceFeatureMask || !surfaceFeatureMask->IsSampleable() || !surfaceFeatureMask->resource)) {
        DEBUGLOG::PushRenderError("[ActionPost] Required current-view SurfaceFeatureMask missing; channel skipped.");
        ClearActionPostFrame();
        return &source;
    }
    CommonParams params = commonParams_;
    params.intensity = 1.0f;
    params.combo = 0.0f;
    std::copy(actionPostValues_.begin(), actionPostValues_.end(), std::begin(params.user));
    auto* output = actionPostChain_.Execute(
        source, quad, params, 0, 0, actionPostEffect_->requiresSurfaceFeatureMask ? surfaceFeatureMask : nullptr);
    if (!output || output == &source) {
        ClearActionPostFrame();
        return &source;
    }
    return output;
}
} // namespace HIKARI::POST
