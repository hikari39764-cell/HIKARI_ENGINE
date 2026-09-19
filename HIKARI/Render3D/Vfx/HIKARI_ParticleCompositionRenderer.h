#pragma once
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
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
            std::array<RenderTarget2D, 3> buffers;
            uint32_t current = 0;
        };
        bool EnsureScope(uint32_t depth);
        bool RenderScope(uint32_t parent, uint32_t depth);
        bool ApplyEffect(Scope&, const MaterialEffectStep&);
        bool RunPass(Scope&, uint32_t source, uint32_t destination, const MaterialEffectStep&, float phase = 0);
        bool EnsureTarget(RenderTarget2D&);
        void Bind(Scope&);
        struct TargetSet {
            std::array<std::unique_ptr<Scope>, 8> scopes{};
            uint64_t lastUsedFrame = 0;
        };
        // 異なる解像度の view が交互に描かれても RT を毎回作り直さない。
        std::unordered_map<uint64_t, TargetSet> targetSets_;
        TargetSet* targets_ = nullptr;
        POST::QuadDrawer quad_;
        std::vector<std::unique_ptr<POST::PostEffect>> effects_;
        uint64_t frameSerial_ = UINT64_MAX;
        size_t effectCursor_ = 0;
        bool initialized_ = false;
        GFX::Context context_{};
        ParticleGpuDrivenDrawBindings bindings_{};
        const ParticleComposition* composition_ = nullptr;
        std::function<bool(uint32_t)> drawLayer_;
    };
}
