#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <algorithm>
#include <limits>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {
        const char* ToString(ColliderFitMode mode) noexcept {
            switch (mode) {
            case ColliderFitMode::Geometry:
                return "Geometry";
            case ColliderFitMode::CollisionAsset:
                return "CollisionAsset";
            case ColliderFitMode::Manual:
            default:
                return "Manual";
            }
        }

        ColliderFitMode ParseFitMode(
            const nlohmann::json& value,
            ColliderFitMode fallback) {
            if (value.is_number_integer()) {
                const int index = value.get<int>();
                if (index == 1) return ColliderFitMode::Geometry;
                if (index == 2) return ColliderFitMode::CollisionAsset;
                return index == 0 ? ColliderFitMode::Manual : fallback;
            }
            if (!value.is_string()) {
                return fallback;
            }
            const std::string text = value.get<std::string>();
            if (text == "Geometry" || text == "FitGeometry") {
                return ColliderFitMode::Geometry;
            }
            if (text == "CollisionAsset") {
                return ColliderFitMode::CollisionAsset;
            }
            if (text == "Manual") {
                return ColliderFitMode::Manual;
            }
            return fallback;
        }

        const char* ToString(PHYSICS::PhysicsShapeType type) noexcept {
            switch (type) {
            case PHYSICS::PhysicsShapeType::Sphere:
                return "Sphere";
            case PHYSICS::PhysicsShapeType::Capsule:
                return "Capsule";
            case PHYSICS::PhysicsShapeType::Box:
            default:
                return "Box";
            }
        }

        PHYSICS::PhysicsShapeType ParseShapeType(
            const nlohmann::json& value,
            PHYSICS::PhysicsShapeType fallback) {
            if (value.is_number_integer()) {
                const int index = value.get<int>();
                return index >= 0 && index <= 2
                    ? static_cast<PHYSICS::PhysicsShapeType>(index)
                    : fallback;
            }
            if (!value.is_string()) {
                return fallback;
            }
            const std::string text = value.get<std::string>();
            if (text == "Box") return PHYSICS::PhysicsShapeType::Box;
            if (text == "Sphere") return PHYSICS::PhysicsShapeType::Sphere;
            if (text == "Capsule") return PHYSICS::PhysicsShapeType::Capsule;
            return fallback;
        }

    }

    void ColliderComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["fitMode"] = ToString(fitMode_);
        out["shape"] = ToString(shapeType_);
        out["collisionAssetId"] = collisionGeometryAssetId_;
        out["center"] = JsonMath::ToJsonArray(center_);
        out["rotation"] =
            JsonMath::ToJsonArray(rotationEulerDegrees_);
        out["size"] = JsonMath::ToJsonArray(size_);
        out["radius"] = radius_;
        out["height"] = height_;
        out["trigger"] = trigger_;
        out["material"] = {
            { "friction", friction_ },
            { "restitution", restitution_ },
            { "density", density_ }
        };
        out["filter"] = {
            { "layer", collisionLayer_ },
            { "mask", collisionMask_ }
        };
    }

    void ColliderComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        if (const auto found = in.find("fitMode"); found != in.end()) {
            fitMode_ = ParseFitMode(*found, fitMode_);
        }
        if (const auto found = in.find("shape"); found != in.end()) {
            shapeType_ = ParseShapeType(*found, shapeType_);
        }
        collisionGeometryAssetId_ = in.value(
            "collisionAssetId",
            collisionGeometryAssetId_);
        if (in.contains("center")) {
            center_ = JSONREAD::Vec3Or(in["center"], center_);
        }
        if (in.contains("rotation")) {
            rotationEulerDegrees_ = JSONREAD::Vec3Or(
                in["rotation"], rotationEulerDegrees_);
        }
        if (in.contains("size")) {
            size_ = JSONREAD::Vec3Or(in["size"], size_);
        }
        radius_ = in.value("radius", radius_);
        height_ = in.value("height", height_);
        trigger_ = in.value("trigger", trigger_);
        if (in.contains("material") && in["material"].is_object()) {
            const nlohmann::json& material = in["material"];
            friction_ = material.value("friction", friction_);
            restitution_ = material.value("restitution", restitution_);
            density_ = material.value("density", density_);
        }
        if (in.contains("filter") && in["filter"].is_object()) {
            const nlohmann::json& filter = in["filter"];
            collisionLayer_ = filter.value("layer", collisionLayer_);
            collisionMask_ = filter.value("mask", collisionMask_);
        }
        ClampSettings();
    }

    void ColliderComponent::ClampSettings() noexcept {
        size_.x = (std::max)(size_.x, 0.001f);
        size_.y = (std::max)(size_.y, 0.001f);
        size_.z = (std::max)(size_.z, 0.001f);
        radius_ = (std::max)(radius_, 0.0005f);
        height_ = (std::max)(height_, radius_ * 2.0f);
        friction_ = (std::max)(friction_, 0.0f);
        restitution_ = (std::clamp)(restitution_, 0.0f, 1.0f);
        density_ = (std::max)(density_, 0.0001f);
        if (collisionLayer_ == 0u) {
            collisionLayer_ = 1u;
        }
    }

    bool ColliderComponent::IsEnabled() const noexcept { return enabled_; }
    ColliderFitMode ColliderComponent::GetFitMode() const noexcept { return fitMode_; }
    ResolvedColliderShape ColliderComponent::ResolveShape() const noexcept {
        ResolvedColliderShape resolved{};
        resolved.type = shapeType_;
        resolved.center = center_;
        resolved.rotationEulerDegrees = rotationEulerDegrees_;
        resolved.size = size_;
        resolved.radius = radius_;
        resolved.height = height_;
        if (fitMode_ != ColliderFitMode::Geometry) {
            return resolved;
        }

        const GameObject* owner = GetOwner();
        if (owner == nullptr) {
            return resolved;
        }
        GeometryFitDesc bestFit{};
        int bestPriority = (std::numeric_limits<int>::min)();
        bool foundFit = false;
        for (const auto& component : owner->GetComponents()) {
            const auto* provider =
                dynamic_cast<const IGeometryFitProvider*>(component.get());
            if (provider == nullptr) {
                continue;
            }
            GeometryFitDesc fit{};
            const int priority = provider->GetGeometryFitPriority();
            if (priority <= bestPriority ||
                !provider->QueryGeometryFit(fit)) {
                continue;
            }
            bestFit = fit;
            bestPriority = priority;
            foundFit = true;
        }
        if (foundFit) {
            switch (bestFit.shape) {
            case GeometryFitShape::Sphere:
                resolved.type = PHYSICS::PhysicsShapeType::Sphere;
                break;
            case GeometryFitShape::Capsule:
                resolved.type = PHYSICS::PhysicsShapeType::Capsule;
                break;
            case GeometryFitShape::Box:
            default:
                resolved.type = PHYSICS::PhysicsShapeType::Box;
                break;
            }
            resolved.center = bestFit.center;
            resolved.rotationEulerDegrees =
                bestFit.rotationEulerDegrees;
            resolved.size = bestFit.size;
            resolved.radius = (std::max)(
                bestFit.radius,
                0.0005f);
            resolved.height = (std::max)(
                bestFit.height,
                resolved.radius * 2.0f);
            resolved.fittedFromGeometry = true;
        }
        return resolved;
    }
    PHYSICS::PhysicsShapeType ColliderComponent::GetShapeType() const noexcept { return shapeType_; }
    const MATH::Vec3& ColliderComponent::GetCenter() const noexcept { return center_; }
    const MATH::Vec3& ColliderComponent::GetRotationEulerDegrees() const noexcept { return rotationEulerDegrees_; }
    const MATH::Vec3& ColliderComponent::GetSize() const noexcept { return size_; }
    float ColliderComponent::GetRadius() const noexcept { return radius_; }
    float ColliderComponent::GetHeight() const noexcept { return height_; }
    bool ColliderComponent::IsTrigger() const noexcept { return trigger_; }
    float ColliderComponent::GetFriction() const noexcept { return friction_; }
    float ColliderComponent::GetRestitution() const noexcept { return restitution_; }
    float ColliderComponent::GetDensity() const noexcept { return density_; }
    uint32_t ColliderComponent::GetCollisionLayer() const noexcept { return collisionLayer_; }
    uint32_t ColliderComponent::GetCollisionMask() const noexcept { return collisionMask_; }
    const std::string& ColliderComponent::GetCollisionGeometryAssetId() const noexcept {
        return collisionGeometryAssetId_;
    }
    bool ColliderComponent::UsesCollisionGeometryAsset() const noexcept {
        return fitMode_ == ColliderFitMode::CollisionAsset &&
            !collisionGeometryAssetId_.empty();
    }
    void ColliderComponent::SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
    void ColliderComponent::SetFitMode(ColliderFitMode mode) noexcept { fitMode_ = mode; }
    void ColliderComponent::SetShapeType(PHYSICS::PhysicsShapeType type) noexcept { shapeType_ = type; }

} // namespace HIKARI
