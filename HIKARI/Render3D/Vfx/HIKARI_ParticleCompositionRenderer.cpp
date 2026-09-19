#include "Render3D/Vfx/HIKARI_ParticleCompositionRenderer.h"
#include "Core/HIKARI_TimeService.h"
#include <algorithm>

namespace HIKARI::RENDER3D::PARTICLES {
    static_assert(static_cast<uint32_t>(MaterialEffectKind::Pixelate) == 17u);
    static_assert(static_cast<uint32_t>(MaterialEffectKind::Ink) == 19u);
    bool ParticleCompositionRenderer::EnsureTarget(RenderTarget2D& target) {
        target.UpdateContext(context_);
        if (target.GetWidth() != int(bindings_.width) || target.GetHeight() != int(bindings_.height)) {
            target.Finalize();
            if (!target.Init(bindings_.width, bindings_.height, DXGI_FORMAT_R16G16B16A16_FLOAT)) return false;
            target.SetDebugName("Vfx.Composition");
        }
        return true;
    }
    bool ParticleCompositionRenderer::EnsureScope(uint32_t depth) {
        auto& scopes = targets_->scopes;
        if (depth >= scopes.size()) return false;
        if (!scopes[depth]) scopes[depth] = std::make_unique<Scope>();
        for (uint32_t i = 0; i < 2; ++i)
            if (!EnsureTarget(scopes[depth]->buffers[i])) return false;
        scopes[depth]->current = 0;
        scopes[depth]->buffers[0].BeginCapture(0, 0, 0, 0);
        return true;
    }
    void ParticleCompositionRenderer::Bind(Scope& scope) {
        auto& target = scope.buffers[scope.current];
        target.Rebind();
        const auto rtv = target.GetRtvHandle();
        context_.cmdList->OMSetRenderTargets(1, &rtv, FALSE,
            bindings_.compositionDepth.ptr ? &bindings_.compositionDepth : nullptr);
    }
    bool ParticleCompositionRenderer::ApplyEffect(Scope& scope, const MaterialEffectStep& step) {
        if (!step.enabled) return true;
        const auto source = scope.current;
        if (step.kind == MaterialEffectKind::Glow) {
            // 原像を保持し、抽出/H/V の結果だけを加算する。
            if (!EnsureTarget(scope.buffers[2])) return false;
            const uint32_t horizontal = (source + 1u) % 3u, vertical = (source + 2u) % 3u;
            if (!RunPass(scope, source, horizontal, step, 1) || !RunPass(scope, horizontal, vertical, step, 2)) return false;
            auto& halo = scope.buffers[vertical];
            halo.EndCapture();
            Bind(scope);
            quad_.DrawBlended(halo.GetSrvHeap(), halo.GetSrvGpu(), POST::BlendOption::Additive, 0);
            return true;
        }
        const uint32_t destination = 1u - source;
        if (!RunPass(scope, source, destination, step, step.kind == MaterialEffectKind::Blur ? 1.0f : 0.0f)) return false;
        if (step.kind == MaterialEffectKind::Blur)
            return RunPass(scope, destination, source, step, 2);
        scope.current = destination;
        return true;
    }
    bool ParticleCompositionRenderer::RunPass(Scope& scope, uint32_t sourceIndex,
        uint32_t destinationIndex, const MaterialEffectStep& step, float phase) {
        // PostEffect 自身のフレーム別 draw slice を使い切ってから次のページを確保する。
        const size_t page = effectCursor_ / POST::PostEffect::DrawCapacityPerFrame();
        if (page == effects_.size()) {
            auto effect = std::make_unique<POST::PostEffect>();
            if (!effect->LoadBuiltinPixelShader("HIKARI.EngineShader.Post_VfxCompositionPS")) return false;
            effects_.push_back(std::move(effect));
        }
        auto& effect = *effects_[page];
        ++effectCursor_;
        auto& source = scope.buffers[sourceIndex];
        source.EndCapture();
        auto& destination = scope.buffers[destinationIndex];
        destination.BeginCapture(0, 0, 0, 0);
        quad_.SetInputTexture(source.GetSrvHeap(), source.GetSrvGpu());
        POST::CommonParams params;
        params.resolutionX = float(bindings_.width); params.resolutionY = float(bindings_.height);
        params.user[0].x = float(static_cast<uint32_t>(step.kind));
        params.user[0].y = phase;
        params.user[1] = {step.parameters.x, step.parameters.y, step.parameters.z, step.parameters.w};
        params.user[2] = {step.parameters2.x, step.parameters2.y, step.parameters2.z, step.parameters2.w};
        params.user[3] = {step.colorA.x, step.colorA.y, step.colorA.z, step.colorA.w};
        effect.ApplyCommonParams(params);
        if (!effect.BindAndDraw(quad_)) { destination.EndCapture(); return false; }
        return true;
    }
    bool ParticleCompositionRenderer::RenderScope(uint32_t parent, uint32_t depth) {
        if (!EnsureScope(depth)) return false;
        auto& scope = *targets_->scopes[depth];
        // 下から上へ。調整レイヤー到達時点の結果だけを処理する。
        for (auto it = composition_->layers.rbegin(); it != composition_->layers.rend(); ++it) {
            if (it->parentEmitterIndex != parent || !it->enabled) continue;
            if (it->kind == ParticleCompositionLayerKind::Adjustment) {
                for (const auto& effect : it->effects) if (!ApplyEffect(scope, effect)) return false;
            } else {
                if (it->kind == ParticleCompositionLayerKind::Content) {
                    Bind(scope);
                    if (!drawLayer_(it->emitterIndex)) return false;
                }
                const bool children = std::any_of(composition_->layers.begin(), composition_->layers.end(),
                    [&](const auto& child) { return child.parentEmitterIndex == it->emitterIndex && child.enabled; });
                if (children) {
                    if (!RenderScope(it->emitterIndex, depth + 1u)) return false;
                    auto& child = *targets_->scopes[depth + 1u];
                    auto& result = child.buffers[child.current];
                    result.EndCapture();
                    Bind(scope);
                    quad_.DrawBlended(result.GetSrvHeap(), result.GetSrvGpu(), POST::BlendOption::PremultipliedAlpha, 0);
                }
            }
        }
        scope.buffers[scope.current].EndCapture();
        return true;
    }
    bool ParticleCompositionRenderer::Render(const GFX::Context& context,
        const ParticleGpuDrivenDrawBindings& bindings, const ParticleComposition& composition,
        const std::function<bool(uint32_t)>& drawLayer) {
        context_ = context; bindings_ = bindings; composition_ = &composition; drawLayer_ = drawLayer;
        if (!bindings.width || !bindings.height || !bindings.compositionTarget.rebind || !bindings.compositionDepth.ptr) return false;
        if (!initialized_) { if (!quad_.Init(context)) return false; initialized_ = true; }
        quad_.UpdateContext(context);
        POST::PostEffect::UpdateContext(context);
        if (!quad_.SetOutputFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)) return false;
        const auto serial = TIME::GetFrameContext().frameIndex;
        if (frameSerial_ != serial) { frameSerial_ = serial; effectCursor_ = 0; }
        std::erase_if(targetSets_, [serial](const auto& entry) {
            return serial > entry.second.lastUsedFrame && serial - entry.second.lastUsedFrame > GFX::kFrameResourceCount;
        });
        targets_ = &targetSets_[(uint64_t(bindings.width) << 32u) | bindings.height];
        targets_->lastUsedFrame = serial;
        const bool rendered = RenderScope(UINT32_MAX, 0);
        const bool restored = bindings.compositionTarget.Rebind() && bindings.compositionTarget.BeginDepthRead();
        if (rendered && restored) {
            auto& root = *targets_->scopes[0];
            auto& result = root.buffers[root.current];
            quad_.DrawBlended(result.GetSrvHeap(), result.GetSrvGpu(), POST::BlendOption::PremultipliedAlpha, 0);
        }
        drawLayer_ = {}; composition_ = nullptr; targets_ = nullptr;
        return rendered && restored;
    }
    void ParticleCompositionRenderer::Release() {
        targetSets_.clear(); targets_ = nullptr;
        effects_.clear(); quad_.Finalize(); initialized_ = false; effectCursor_ = 0; frameSerial_ = UINT64_MAX;
    }
}
