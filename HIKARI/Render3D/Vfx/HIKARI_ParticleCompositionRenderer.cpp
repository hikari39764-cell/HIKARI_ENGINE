#include "Render3D/Vfx/HIKARI_ParticleCompositionRenderer.h"
#include "Core/HIKARI_TimeService.h"
#include <algorithm>
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::RENDER3D::PARTICLES {
void ParticleCompositionRenderer::PrepareTopology(const ParticleComposition& composition) {
    const auto& layers = composition.layers;
    bool changed = topology_.size() != layers.size();
    for (size_t i = 0; !changed && i < layers.size(); ++i)
        changed = topology_[i] != std::pair{layers[i].emitterIndex, layers[i].parentEmitterIndex};
    if (changed) {
        topology_.clear();
        layerIndices_.clear();
        children_.clear();
        topology_.reserve(layers.size());
        for (size_t i = 0; i < layers.size(); ++i) {
            topology_.emplace_back(layers[i].emitterIndex, layers[i].parentEmitterIndex);
            layerIndices_.try_emplace(layers[i].emitterIndex, i);
            children_[layers[i].parentEmitterIndex].push_back(i);
        }
    }
    matteSources_.clear();
    for (const auto& layer : layers)
        if (layer.matteSourceIndex != UINT32_MAX)
            matteSources_.insert(layer.matteSourceIndex);
}
bool ParticleCompositionRenderer::EnsureTarget(RenderTarget2D& target) {
    target.UpdateContext(context_);
    if (target.GetWidth() != int(bindings_.width) || target.GetHeight() != int(bindings_.height)) {
        target.Finalize();
        if (!target.Init(bindings_.width, bindings_.height, DXGI_FORMAT_R16G16B16A16_FLOAT))
            return false;
        target.SetDebugName("Vfx.Composition");
    }
    return true;
}
bool ParticleCompositionRenderer::ResetScope(Scope& scope) {
    if (!EnsureTarget(scope.buffers[0]))
        return false;
    scope.current = 0;
    scope.buffers[0].BeginCapture(0, 0, 0, 0);
    return true;
}
bool ParticleCompositionRenderer::EnsureScope(uint32_t depth) {
    auto& scopes = targets_->scopes;
    if (depth > composition_->layers.size() * 2u)
        return false;
    if (depth >= scopes.size())
        scopes.resize(depth + 1u);
    if (!scopes[depth])
        scopes[depth] = std::make_unique<Scope>();
    return ResetScope(*scopes[depth]);
}
void ParticleCompositionRenderer::Bind(Scope& scope) {
    auto& target = scope.buffers[scope.current];
    target.Rebind();
    const auto rtv = target.GetRtvHandle();
    context_.cmdList->OMSetRenderTargets(1, &rtv, FALSE,
                                         bindings_.compositionDepth.ptr ? &bindings_.compositionDepth : nullptr);
}
bool ParticleCompositionRenderer::BindInputs(RenderTarget2D& source, RenderTarget2D& matte) {
    constexpr size_t drawsPerPage = 128;
    auto& frame = descriptorFrames_[context_.frameIndex % GFX::kFrameResourceCount];
    const auto serial = TIME::GetFrameContext().frameIndex;
    if (frame.serial != serial) {
        frame.serial = serial;
        frame.cursor = 0;
    }
    const size_t pageIndex = frame.cursor / drawsPerPage;
    if (pageIndex == frame.pages.size()) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = UINT(drawsPerPage * 3);
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(context_.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap))))
            return false;
        frame.pages.push_back(std::move(heap));
    }
    auto* heap = frame.pages[pageIndex].Get();
    const auto stride = context_.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    const auto offset = (frame.cursor++ % drawsPerPage) * 3 * stride;
    cpu.ptr += offset;
    gpu.ptr += offset;
    for (auto* target : {&source, &matte, &matte}) {
        // Shader-visible heap はコピー元にできないため、RT の view を直接作成する。
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = target->GetFormat();
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        context_.device->CreateShaderResourceView(target->GetResource(), &view, cpu);
        cpu.ptr += stride;
    }
    quad_.SetInputTexture(heap, gpu, {gpu.ptr + stride});
    return true;
}
bool ParticleCompositionRenderer::ApplyMatte(Scope& scope, RenderTarget2D& matte, bool luminance, bool inverted) {
    // PostEffect 自身のフレーム別 draw slice を使い切ってから次のページを確保する。
    const size_t page = effectCursor_ / POST::PostEffect::DrawCapacityPerFrame();
    if (page == effects_.size()) {
        auto effect = std::make_unique<POST::PostEffect>();
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_VfxMattePS"}))
            return false;
        effects_.push_back(std::move(effect));
    }
    auto& effect = *effects_[page];
    ++effectCursor_;
    auto& source = scope.buffers[scope.current];
    source.EndCapture();
    const uint32_t destinationIndex = 1u - scope.current;
    auto& destination = scope.buffers[destinationIndex];
    if (!EnsureTarget(destination))
        return false;
    matte.EndCapture();
    // 全画素を上書きし、blend/depth/clip を使わない pass は clear 不要。
    destination.Rebind();
    if (!BindInputs(source, matte))
        return false;
    POST::CommonParams params;
    params.resolutionX = float(bindings_.width);
    params.resolutionY = float(bindings_.height);
    params.user[0] = {luminance ? 1.0f : 0.0f, inverted ? 1.0f : 0.0f, 0, 0};
    effect.ApplyCommonParams(params);
    if (!effect.BindAndDraw(quad_)) {
        destination.EndCapture();
        return false;
    }
    scope.current = destinationIndex;
    return true;
}
bool ParticleCompositionRenderer::ApplyStyle(Scope& scope, const MaterialEffectStep& step) {
    if (!step.enabled || step.mix <= 0)
        return true;
    const size_t page = styleCursor_ / POST::PostEffect::DrawCapacityPerFrame();
    if (page == styleEffects_.size()) {
        auto effect = std::make_unique<POST::PostEffect>();
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_VfxStylePS"}))
            return false;
        styleEffects_.push_back(std::move(effect));
    }
    auto& effect = *styleEffects_[page];
    ++styleCursor_;
    auto& source = scope.buffers[scope.current];
    source.EndCapture();
    const uint32_t next = 1u - scope.current;
    auto& destination = scope.buffers[next];
    if (!EnsureTarget(destination))
        return false;
    destination.Rebind();
    if (!BindInputs(source, source))
        return false;
    POST::CommonParams params;
    params.resolutionX = float(bindings_.width);
    params.resolutionY = float(bindings_.height);
    params.user[0] = {float(step.kind), step.mix, float(step.mask), step.maskInvert ? 1.0f : 0.0f};
    const auto pack = [](MATH::Vec4 v) { return DirectX::XMFLOAT4{v.x, v.y, v.z, v.w}; };
    params.user[1] = pack(step.parameters);
    params.user[2] = pack(step.parameters2);
    params.user[3] = pack(step.colorA);
    params.user[4] = pack(step.colorB);
    params.user[5] = pack(step.maskParameters);
    const size_t count = (std::min)(step.colorStops.size(), size_t(8));
    params.combo = float(count);
    float times[8]{};
    for (size_t i = 0; i < count; ++i) {
        times[i] = step.colorStops[i].time;
        params.user[8 + i] = pack(step.colorStops[i].color);
    }
    params.user[6] = {times[0], times[1], times[2], times[3]};
    params.user[7] = {times[4], times[5], times[6], times[7]};
    effect.ApplyCommonParams(params);
    if (!effect.BindAndDraw(quad_)) {
        destination.EndCapture();
        return false;
    }
    scope.current = next;
    return true;
}
bool ParticleCompositionRenderer::CompositeScope(Scope& source, Scope& destination) {
    auto& result = source.buffers[source.current];
    result.EndCapture();
    Bind(destination);
    quad_.DrawBlended(result.GetSrvHeap(), result.GetSrvGpu(), POST::BlendOption::PremultipliedAlpha, 0);
    return true;
}
RenderTarget2D* ParticleCompositionRenderer::ResolveMatte(uint32_t index, uint32_t depth) {
    const auto found = layerIndices_.find(index);
    if (found == layerIndices_.end())
        return nullptr;
    const auto* layer = &composition_->layers[found->second];
    auto& entry = targets_->mattes[index];
    if (!entry)
        entry = std::make_unique<Scope>();
    if (!renderedMattes_.contains(index)) {
        if (!activeMattes_.insert(index).second)
            return nullptr;
        if (!ResetScope(*entry) || (layer->enabled && !RenderLayer(*layer, *entry, depth)))
            return nullptr;
        activeMattes_.erase(index);
        renderedMattes_.insert(index);
    }
    auto& result = entry->buffers[entry->current];
    result.EndCapture();
    return &result;
}
bool ParticleCompositionRenderer::RenderContent(const ParticleCompositionLayer& layer, Scope& scope, uint32_t depth) {
    if (layer.kind == ParticleCompositionLayerKind::Content) {
        Bind(scope);
        if (!drawLayer_(layer.emitterIndex))
            return false;
    }
    const auto found = children_.find(layer.emitterIndex);
    const bool children =
        found != children_.end() && std::any_of(found->second.begin(), found->second.end(), [&](size_t index) {
            const auto& child = composition_->layers[index];
            return child.enabled && !child.matteOnly;
        });
    if (children) {
        if (!RenderScope(layer.emitterIndex, depth + 1u))
            return false;
        if (!CompositeScope(*targets_->scopes[depth + 1u], scope))
            return false;
    }
    return true;
}
bool ParticleCompositionRenderer::RenderLayer(const ParticleCompositionLayer& layer, Scope& scope, uint32_t depth) {
    RenderTarget2D* matte = nullptr;
    if (layer.matteSourceIndex != UINT32_MAX) {
        matte = ResolveMatte(layer.matteSourceIndex, depth);
        if (!matte)
            return false;
    }
    if (!matte)
        return RenderContent(layer, scope, depth);
    if (!EnsureScope(depth + 1u))
        return false;
    auto& isolated = *targets_->scopes[depth + 1u];
    if (!RenderContent(layer, isolated, depth + 1u))
        return false;
    if (!ApplyMatte(isolated, *matte, layer.matteLuminance, layer.matteInverted))
        return false;
    return CompositeScope(isolated, scope);
}
bool ParticleCompositionRenderer::RenderScope(uint32_t parent, uint32_t depth) {
    if (!EnsureScope(depth))
        return false;
    auto& scope = *targets_->scopes[depth];
    // 下から上へ。材質調整を終えたレイヤーの幾何遮罩だけを合成する。
    const auto found = children_.find(parent);
    if (found != children_.end())
        for (auto index = found->second.rbegin(); index != found->second.rend(); ++index) {
            const auto* it = &composition_->layers[*index];
            if (!it->enabled || it->matteOnly)
                continue;
            if (it->kind == ParticleCompositionLayerKind::Adjustment) {
                for (const auto& effect : it->effects)
                    if (!ApplyStyle(scope, effect))
                        return false;
                continue;
            }
            if (!RenderLayer(*it, scope, depth))
                return false;
        }
    scope.buffers[scope.current].EndCapture();
    return true;
}
bool ParticleCompositionRenderer::Render(const GFX::Context& context, const ParticleGpuDrivenDrawBindings& bindings,
                                         const ParticleComposition& composition,
                                         const std::function<bool(uint32_t)>& drawLayer) {
    context_ = context;
    bindings_ = bindings;
    composition_ = &composition;
    drawLayer_ = drawLayer;
    PrepareTopology(composition);
    if (!bindings.width || !bindings.height || !bindings.compositionTarget.rebind || !bindings.compositionDepth.ptr)
        return false;
    if (!initialized_) {
        if (!quad_.Init(context))
            return false;
        initialized_ = true;
    }
    quad_.UpdateContext(context);
    POST::PostEffect::UpdateContext(context);
    if (!quad_.SetOutputFormat(DXGI_FORMAT_R16G16B16A16_FLOAT))
        return false;
    const auto serial = TIME::GetFrameContext().frameIndex;
    if (frameSerial_ != serial) {
        frameSerial_ = serial;
        effectCursor_ = 0;
        styleCursor_ = 0;
    }
    std::erase_if(targetSets_, [serial](const auto& entry) {
        return serial > entry.second.lastUsedFrame && serial - entry.second.lastUsedFrame > GFX::kFrameResourceCount;
    });
    targets_ = &targetSets_[(uint64_t(bindings.width) << 32u) | bindings.height];
    targets_->lastUsedFrame = serial;
    renderedMattes_.clear();
    activeMattes_.clear();
    std::erase_if(targets_->mattes, [&](const auto& entry) { return !matteSources_.contains(entry.first); });
    const bool rendered = RenderScope(UINT32_MAX, 0);
    const bool restored = bindings.compositionTarget.Rebind() && bindings.compositionTarget.BeginDepthRead();
    if (rendered && restored) {
        auto& root = *targets_->scopes[0];
        auto& result = root.buffers[root.current];
        quad_.DrawBlended(result.GetSrvHeap(), result.GetSrvGpu(), POST::BlendOption::PremultipliedAlpha, 0);
    }
    drawLayer_ = {};
    composition_ = nullptr;
    targets_ = nullptr;
    return rendered && restored;
}
void ParticleCompositionRenderer::Release() {
    targetSets_.clear();
    targets_ = nullptr;
    topology_.clear();
    layerIndices_.clear();
    children_.clear();
    matteSources_.clear();
    for (auto& frame : descriptorFrames_) {
        for (auto& heap : frame.pages)
            GFX::RetireD3D12ObjectForFrame(heap, context_, "Vfx.CompositionInputs");
        frame.pages.clear();
        frame.cursor = 0;
        frame.serial = UINT64_MAX;
    }
    effects_.clear();
    styleEffects_.clear();
    quad_.Finalize();
    initialized_ = false;
    effectCursor_ = styleCursor_ = 0;
    frameSerial_ = UINT64_MAX;
}
} // namespace HIKARI::RENDER3D::PARTICLES
