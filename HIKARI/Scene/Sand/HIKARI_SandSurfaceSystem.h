#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class World;

    namespace SAND {
        class SandSurfaceQueryService;
        struct SandSurfaceRuntimeState;
        using SandSurfaceRuntimeStatePtr =
            std::shared_ptr<SandSurfaceRuntimeState>;
    }

    // 可変 World component を CPU 高さグリッドへ投影し、球体の接触を
    // stamp として適用する。レンダリングはこのグリッドの snapshot だけを
    // 読むため、システムが唯一の変異経路になる。
    class SandSurfaceSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "SandSurfaceSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void PreRender(
            World& world,
            const FrameContext& frame) override;

    private:
        void Refresh(World& world);
        void ApplySphereContactStamps(
            World& world,
            float fixedDt);
        void RelaxState(
            SAND::SandSurfaceRuntimeState& state,
            float fixedDt);
        void ApplyStamp(
            SAND::SandSurfaceRuntimeState& state,
            float localX,
            float localZ,
            float radius,
            float depth);

        SAND::SandSurfaceQueryService* queryService_ = nullptr;
        std::unordered_map<uint64_t, SAND::SandSurfaceRuntimeStatePtr>
            states_{};
    };

} // namespace HIKARI
