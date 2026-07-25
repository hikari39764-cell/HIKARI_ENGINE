#pragma once

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    enum class ColliderFitMode : uint8_t {
        Manual,
        Geometry,
        CollisionAsset,
    };

    struct ResolvedColliderShape {
        PHYSICS::PhysicsShapeType type =
            PHYSICS::PhysicsShapeType::Box;
        MATH::Vec3 center{};
        MATH::Vec3 rotationEulerDegrees{};
        MATH::Vec3 size{ 1.0f, 1.0f, 1.0f };
        float radius = 0.5f;
        float height = 1.0f;
        bool fittedFromGeometry = false;
    };

    class ColliderComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "ColliderComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept;
        ColliderFitMode GetFitMode() const noexcept;
        ResolvedColliderShape ResolveShape() const noexcept;
        PHYSICS::PhysicsShapeType GetShapeType() const noexcept;
        const MATH::Vec3& GetCenter() const noexcept;
        const MATH::Vec3& GetRotationEulerDegrees() const noexcept;
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
        void SetFitMode(ColliderFitMode mode) noexcept;
        void SetShapeType(PHYSICS::PhysicsShapeType type) noexcept;

    private:
        void ClampSettings() noexcept;

        bool enabled_ = true;
        ColliderFitMode fitMode_ = ColliderFitMode::Manual;
        PHYSICS::PhysicsShapeType shapeType_ =
            PHYSICS::PhysicsShapeType::Box;
        MATH::Vec3 center_{};
        MATH::Vec3 rotationEulerDegrees_{};
        MATH::Vec3 size_{ 1.0f, 1.0f, 1.0f };
        float radius_ = 0.5f;
        float height_ = 1.0f;
        bool trigger_ = false;
        float friction_ = 0.5f;
        float restitution_ = 0.0f;
        float density_ = 1.0f;
        uint32_t collisionLayer_ = 1u;
        uint32_t collisionMask_ = 0xFFFFFFFFu;
        std::string collisionGeometryAssetId_{};
    };

} // namespace HIKARI
