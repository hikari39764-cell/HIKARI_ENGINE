#pragma once

#include <string>
#include <vector>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 流体（VFX の流体層）を押しのける形。物体か模型の节点（骨骼）に付いて動き、動く速さで烟を押し出し引きずる。
enum class FluidObstacleShapeType : uint8_t { Sphere, Capsule, Box };

inline constexpr TEXT::EnumName<FluidObstacleShapeType> kFluidObstacleShapeTypeNames[] = {
    {FluidObstacleShapeType::Sphere, "Sphere"},
    {FluidObstacleShapeType::Capsule, "Capsule"},
    {FluidObstacleShapeType::Box, "Box"},
};

// 形 1 つ。中心・回転は socket（空なら物体）の局所、大きさは socket の拡縮を掛ける前の値。カプセルは局所 Y 軸に沿い、
// 高さは両端の半球を含む全長。
struct FluidObstacleShape {
    std::string socket{};
    FluidObstacleShapeType shape = FluidObstacleShapeType::Capsule;
    MATH::Vec3 center{};
    // オイラー角（度）。
    MATH::Vec3 rotation{};
    float radius = 0.1f;
    float height = 0.5f;
    MATH::Vec3 size{0.3f, 0.3f, 0.3f};
};

struct FluidObstacleSettings {
    bool enabled = true;
    // 物体の碰撞器（手動の形と、模型の形への当てはめ）も障碍物にする。触发器と碰撞资产の形は使わない。
    bool useColliders = false;
    std::vector<FluidObstacleShape> shapes{};
};

inline constexpr auto kFluidObstacleShapeFields = [](auto&& field) {
    using S = FluidObstacleShape;
    using namespace JSONFIELDS;
    constexpr auto rounded = [](const S& s) { return s.shape != FluidObstacleShapeType::Box; };
    constexpr auto capsule = [](const S& s) { return s.shape == FluidObstacleShapeType::Capsule; };
    constexpr auto box = [](const S& s) { return s.shape == FluidObstacleShapeType::Box; };
    field("socket", &S::socket, Label{"Socket"},
          Help{"Model node (bone) the shape follows, by exact name. Empty follows the object."});
    field("shape", &S::shape, kFluidObstacleShapeTypeNames, Label{"Shape"});
    field("center", &S::center, Range{-1000.0, 1000.0}, Unit{"m"}, Label{"Center"});
    field("rotation", &S::rotation, Range{-360.0, 360.0}, Label{"Rotation"});
    field("radius", &S::radius, Range{0.001, 100.0}, Unit{"m"}, Label{"Radius"}, Show{rounded});
    field("height", &S::height, Range{0.002, 200.0}, Unit{"m"}, Label{"Height"}, Show{capsule});
    field("size", &S::size, Range{0.002, 200.0}, Unit{"m"}, Label{"Size"}, Show{box});
};

inline constexpr auto kFluidObstacleFields = [](auto&& field) {
    using S = FluidObstacleSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("useColliders", &S::useColliders, Label{"Use Colliders"},
          Help{"Also push fluid smoke with this object's Collider shapes (manual or fitted to the model)."}, Common{});
    field("shapes", &S::shapes, Label{"Shape"}, Elements{kFluidObstacleShapeFields}, Common{});
};

class FluidObstacleComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "FluidObstacleComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const FluidObstacleSettings& Settings() const noexcept { return settings_; }

private:
    FluidObstacleSettings settings_{};
};

} // namespace HIKARI
