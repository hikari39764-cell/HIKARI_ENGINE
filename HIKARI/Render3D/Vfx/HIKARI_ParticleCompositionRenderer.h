#pragma once
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "Render3D/Vfx/HIKARI_ParticleGpuDrivenPass.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::RENDER3D::PARTICLES {
    class ParticleCompositionRenderer {
    public:
        bool Render(const GFX::Context&, const ParticleGpuDrivenDrawBindings&,
            const ParticleComposition&, const std::function<bool(uint32_t)>& drawLayer);
        void Release();
    private:
        struct Scope {
            std::array<RenderTarget2D, 2> buffers;
            uint32_t current = 0;
        };
        bool EnsureScope(uint32_t depth);
        bool RenderScope(uint32_t parent, uint32_t depth);
        bool RenderLayer(const ParticleCompositionLayer&, Scope&, uint32_t depth);
        bool RenderContent(const ParticleCompositionLayer&, Scope&, uint32_t depth);
        RenderTarget2D* ResolveMatte(uint32_t index, uint32_t depth);
        bool CompositeScope(Scope& source, Scope& destination);
        bool ResetScope(Scope&);
        bool ApplyMatte(Scope&, RenderTarget2D&, bool luminance, bool inverted);
        bool ApplyStyle(Scope&, const MaterialEffectStep&);
        bool BindInputs(RenderTarget2D&, RenderTarget2D&);
        bool EnsureTarget(RenderTarget2D&);
        void PrepareTopology(const ParticleComposition&);
        void Bind(Scope&);
        struct TargetSet {
            std::vector<std::unique_ptr<Scope>> scopes;
            std::unordered_map<uint32_t,std::unique_ptr<Scope>> mattes;
            uint64_t lastUsedFrame = 0;
        };
        // 異なる解像度の view が交互に描かれても RT を毎回作り直さない。
        std::unordered_map<uint64_t, TargetSet> targetSets_;
        TargetSet* targets_ = nullptr;
        std::unordered_set<uint32_t> renderedMattes_, activeMattes_;
        std::vector<std::pair<uint32_t, uint32_t>> topology_;
        std::unordered_map<uint32_t, size_t> layerIndices_;
        std::unordered_map<uint32_t, std::vector<size_t>> children_;
        std::unordered_set<uint32_t> matteSources_;
        POST::QuadDrawer quad_;
        std::vector<std::unique_ptr<POST::PostEffect>> effects_;
        std::vector<std::unique_ptr<POST::PostEffect>> styleEffects_;
        uint64_t frameSerial_ = UINT64_MAX;
        size_t effectCursor_ = 0;
        size_t styleCursor_ = 0;
        struct DescriptorFrame {
            std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> pages;
            uint64_t serial = UINT64_MAX;
            size_t cursor = 0;
        };
        std::array<DescriptorFrame, GFX::kFrameResourceCount> descriptorFrames_;
        bool initialized_ = false;
        GFX::Context context_{};
        ParticleGpuDrivenDrawBindings bindings_{};
        const ParticleComposition* composition_ = nullptr;
        std::function<bool(uint32_t)> drawLayer_;
    };
}
