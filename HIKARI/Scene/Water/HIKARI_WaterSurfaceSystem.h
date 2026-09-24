#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

class World;
struct FrameContext;

namespace WATER {
class WaterSurfaceQueryService;
}

// World の mutable Component 群を、物理・ゲームプレイが安全に読める
// WaterSurfaceQueryService の値スナップショットへ投影する。
class WaterSurfaceSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "WaterSurfaceSystem"; }

    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void PreFixedUpdate(World& world, const FrameContext& frame) override;
    void PreRender(World& world, const FrameContext& frame) override;

private:
    void Refresh(World& world);

    WATER::WaterSurfaceQueryService* queryService_ = nullptr;
};

} // namespace HIKARI
