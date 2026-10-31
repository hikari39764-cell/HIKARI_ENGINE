#pragma once

#include <string>
#include <vector>

#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

struct AuthoringFieldDescriptor;

// 碰撞器が持てる形（保存名と診断で共通）。
inline constexpr TEXT::EnumName<PHYSICS::PhysicsShapeType> kColliderShapeTypeNames[] = {
    {PHYSICS::PhysicsShapeType::Box, "Box"},
    {PHYSICS::PhysicsShapeType::Sphere, "Sphere"},
    {PHYSICS::PhysicsShapeType::Capsule, "Capsule"},
};

enum class ColliderFitMode : uint8_t {
    Manual,
    Geometry,
    CollisionAsset,
};

inline constexpr TEXT::EnumName<ColliderFitMode> kColliderFitModeNames[] = {
    {ColliderFitMode::Manual, "Manual"},
    {ColliderFitMode::Geometry, "Geometry"},
    {ColliderFitMode::CollisionAsset, "CollisionAsset"},
};

struct ResolvedColliderShape {
    PHYSICS::PhysicsShapeType type = PHYSICS::PhysicsShapeType::Box;
    MATH::Vec3 center{};
    MATH::Vec3 rotationEulerDegrees{};
    MATH::Vec3 size{1.0f, 1.0f, 1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    bool fittedFromGeometry = false;
};

// 作者値（シーン文書の properties と同じ形）。項目表（kColliderFields）が保存・読み取り・検査器・説明を決める。
// material と filter は物理側の値（PhysicsMaterialDesc / PhysicsCollisionFilter）がそのまま入れ子の項目になる。
struct ColliderSettings {
    bool enabled = true;
    ColliderFitMode fitMode = ColliderFitMode::Manual;
    PHYSICS::PhysicsShapeType shape = PHYSICS::PhysicsShapeType::Box;
    std::string collisionAssetId{};
    MATH::Vec3 center{};
    // オイラー角（度）。
    MATH::Vec3 rotation{};
    MATH::Vec3 size{1.0f, 1.0f, 1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    bool trigger = false;
    PHYSICS::PhysicsMaterialDesc material{};
    PHYSICS::PhysicsCollisionFilter filter{};
};

// 衝突フィルタ（層と、当たる層の集合）の編集欄（kColliderFields の Draw）。プロジェクトの層名があればその名前で選ぶ。
bool DrawColliderFilter(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field,
                        PHYSICS::PhysicsCollisionFilter& filter, const ColliderSettings& owner);

inline constexpr auto kPhysicsMaterialFields = [](auto&& field) {
    using S = PHYSICS::PhysicsMaterialDesc;
    using namespace JSONFIELDS;
    field("friction", &S::friction, Range{0.0, 100.0}, Label{"Friction"});
    field("restitution", &S::restitution, Range{0.0, 1.0}, Label{"Restitution"});
    field("density", &S::density, Range{0.0001, 100000.0}, Label{"Density"});
};

inline constexpr auto kPhysicsCollisionFilterFields = [](auto&& field) {
    using S = PHYSICS::PhysicsCollisionFilter;
    using namespace JSONFIELDS;
    field("layer", &S::layer, Label{"Collision Layer"});
    field("mask", &S::mask, Label{"Collision Mask"});
};

inline constexpr auto kColliderFields = [](auto&& field) {
    using S = ColliderSettings;
    using namespace JSONFIELDS;
    constexpr auto fromAsset = [](const S& s) { return s.fitMode == ColliderFitMode::CollisionAsset; };
    constexpr auto manual = [](const S& s) { return s.fitMode == ColliderFitMode::Manual; };
    constexpr auto manualBox = [](const S& s) {
        return s.fitMode == ColliderFitMode::Manual && s.shape == PHYSICS::PhysicsShapeType::Box;
    };
    constexpr auto manualRounded = [](const S& s) {
        return s.fitMode == ColliderFitMode::Manual && s.shape != PHYSICS::PhysicsShapeType::Box;
    };
    constexpr auto manualCapsule = [](const S& s) {
        return s.fitMode == ColliderFitMode::Manual && s.shape == PHYSICS::PhysicsShapeType::Capsule;
    };
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});
    field("fitMode", &S::fitMode, kColliderFitModeNames, Label{"Shape Source"},
          Help{"Manual uses the shape below. Geometry fits the object's own geometry. Collision Asset uses the "
               "default collision authored on a model."},
          Common{});
    field("collisionAssetId", &S::collisionAssetId, Label{"Collision Source Model"}, AssetRef{AssetType::Model},
          Show{fromAsset}, Common{});
    field("shape", &S::shape, kColliderShapeTypeNames, Label{"Shape"}, Show{manual});
    field("center", &S::center, Label{"Center"}, Show{manual});
    field("rotation", &S::rotation, Label{"Rotation"}, Show{manual});
    field("size", &S::size, Range{0.001, 100000.0}, Label{"Size"}, Show{manualBox});
    field("radius", &S::radius, Range{0.0005, 100000.0}, Label{"Radius"}, Show{manualRounded});
    field("height", &S::height, Range{0.001, 200000.0}, Label{"Height"}, Show{manualCapsule});
    field("trigger", &S::trigger, Label{"Is Trigger"});
    field("material", &S::material, Nested{kPhysicsMaterialFields}, Group{"Material"});
    field("filter", &S::filter, Nested{kPhysicsCollisionFilterFields}, Draw{&DrawColliderFilter},
          Group{"Collision Filter"});
};

// 範囲へ丸める。高さは半径の 2 倍より小さくできず、層は 0（どの層でもない）にしない。
ColliderSettings NormalizeColliderSettings(ColliderSettings settings);

class ColliderComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "ColliderComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept;
    ColliderFitMode GetFitMode() const noexcept;
    ResolvedColliderShape ResolveShape() const noexcept;
    // 物理の形の一覧。当てはめ元が形の組（切り抜いた箱）を出していれば組の全部、無ければ ResolveShape の一つ。
    std::vector<ResolvedColliderShape> ResolveShapes() const;
    const MATH::Vec3& GetCenter() const noexcept;
    const MATH::Vec3& GetSize() const noexcept;
    float GetRadius() const noexcept;
    float GetHeight() const noexcept;
    bool IsTrigger() const noexcept;
    float GetFriction() const noexcept;
    float GetRestitution() const noexcept;
    float GetDensity() const noexcept;
    uint32_t GetCollisionLayer() const noexcept;
    uint32_t GetCollisionMask() const noexcept;
    const std::string& GetCollisionGeometryAssetId() const noexcept;
    bool UsesCollisionGeometryAsset() const noexcept;
    void SetEnabled(bool enabled) noexcept;
    void SetShapeType(PHYSICS::PhysicsShapeType type) noexcept;

private:
    ColliderSettings settings_{};
};

} // namespace HIKARI
