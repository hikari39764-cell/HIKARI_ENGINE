#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/Vfx/HIKARI_FluidTypes.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

class FluidObstacleComponent;
class GameObject;

// 解いた障碍物 1 つ。instance は同じ物体の中で形を見分ける番号（作者の形は 1 から、碰撞器の形は
// kFluidObstacleColliderInstanceBase から）。
struct FluidObstacleInstance {
    uint64_t instance = 0u;
    RENDER3D::FLUID::FluidInteraction interaction{};
};

inline constexpr uint64_t kFluidObstacleColliderInstanceBase = 0x10000u;

// 物体の障碍物の形を世界へ解く（障碍物の系と編集器の枠が共用）。形は付け先（物体か模型の节点）に付き、付け先の拡縮を
// 碰撞器と同じ規則で受ける。付け先の解けない形は飛ばす。
void ResolveFluidObstacles(const World& world, const GameObject& object, const FluidObstacleComponent& component,
                           std::vector<FluidObstacleInstance>& out);

// FluidObstacleComponent の形を毎フレーム世界の位置へ解き、FluidInteractionService へ障碍物として出す。
// 動画の姿勢が決まった後の PreRender で解く（編集中・一時停止中の視口でも走る）。
class FluidObstacleSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "FluidObstacleSystem"; }
    void PreRender(World& world, const FrameContext& frame) override;

private:
    std::vector<FluidObstacleInstance> obstacles_{};
};

} // namespace HIKARI
