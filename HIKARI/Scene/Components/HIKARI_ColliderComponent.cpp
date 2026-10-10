#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <algorithm>
#include <cmath>
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

void ColliderComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["fitMode"] = TEXT::EnumToName(kColliderFitModeNames, fitMode_, "Manual");
    out["shape"] = TEXT::EnumToName(kColliderShapeTypeNames, shapeType_, "Box");
    out["collisionAssetId"] = collisionGeometryAssetId_;
    out["center"] = JsonMath::ToJsonArray(center_);
    out["rotation"] = JsonMath::ToJsonArray(rotationEulerDegrees_);
    out["size"] = JsonMath::ToJsonArray(size_);
    out["radius"] = radius_;
    out["height"] = height_;
    out["trigger"] = trigger_;
    out["material"] = {{"friction", friction_}, {"restitution", restitution_}, {"density", density_}};
    out["filter"] = {{"layer", collisionLayer_}, {"mask", collisionMask_}};
}

void ColliderComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    TEXT::TryParseEnum(kColliderFitModeNames, in.value("fitMode", std::string{}), fitMode_);
    TEXT::TryParseEnum(kColliderShapeTypeNames, in.value("shape", std::string{}), shapeType_);
    collisionGeometryAssetId_ = in.value("collisionAssetId", collisionGeometryAssetId_);
    if (in.contains("center")) {
        center_ = JSONREAD::Vec3Or(in["center"], center_);
    }
    if (in.contains("rotation")) {
        rotationEulerDegrees_ = JSONREAD::Vec3Or(in["rotation"], rotationEulerDegrees_);
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
        const auto* provider = dynamic_cast<const IGeometryFitProvider*>(component.get());
        if (provider == nullptr) {
            continue;
        }
        GeometryFitDesc fit{};
        const int priority = provider->GetGeometryFitPriority();
        if (priority <= bestPriority || !provider->QueryGeometryFit(fit)) {
            continue;
        }
        bestFit = fit;
        bestPriority = priority;
        foundFit = true;
    }
    if (foundFit) {
        switch (bestFit.shape) {
        case GeometryFitShape::Sphere: resolved.type = PHYSICS::PhysicsShapeType::Sphere; break;
        case GeometryFitShape::Capsule: resolved.type = PHYSICS::PhysicsShapeType::Capsule; break;
        case GeometryFitShape::Box:
        default: resolved.type = PHYSICS::PhysicsShapeType::Box; break;
        }
        resolved.center = bestFit.center;
        resolved.rotationEulerDegrees = bestFit.rotationEulerDegrees;
        resolved.size = bestFit.size;
        resolved.radius = (std::max)(bestFit.radius, 0.0005f);
        resolved.height = (std::max)(bestFit.height, resolved.radius * 2.0f);
        resolved.fittedFromGeometry = true;
    }
    return resolved;
}
std::vector<ResolvedColliderShape> ColliderComponent::ResolveShapes() const {
    const ResolvedColliderShape single = ResolveShape();
    if (!single.fittedFromGeometry || GetOwner() == nullptr) {
        return {single};
    }
    // ResolveShape と同じ優先度で当てはめ元を選び、その形の組を使う。
    const IGeometryFitProvider* best = nullptr;
    int bestPriority = (std::numeric_limits<int>::min)();
    GeometryFitDesc ignored{};
    for (const auto& component : GetOwner()->GetComponents()) {
        const auto* provider = dynamic_cast<const IGeometryFitProvider*>(component.get());
        if (provider != nullptr && provider->GetGeometryFitPriority() > bestPriority &&
            provider->QueryGeometryFit(ignored)) {
            best = provider;
            bestPriority = provider->GetGeometryFitPriority();
        }
    }
    std::vector<GeometryFitDesc> parts;
    if (best != nullptr) {
        best->QueryGeometryFitParts(parts);
    }
    if (parts.empty()) {
        return {single};
    }
    std::vector<ResolvedColliderShape> shapes;
    shapes.reserve(parts.size());
    for (const GeometryFitDesc& part : parts) {
        ResolvedColliderShape shape = single;
        shape.type = PHYSICS::PhysicsShapeType::Box;
        shape.center = part.center;
        shape.rotationEulerDegrees = part.rotationEulerDegrees;
        shape.size = part.size;
        shape.radius = (std::max)(part.radius, 0.0005f);
        shape.height = (std::max)(part.height, shape.radius * 2.0f);
        shapes.push_back(shape);
    }
    return shapes;
}

const MATH::Vec3& ColliderComponent::GetCenter() const noexcept { return center_; }
const MATH::Vec3& ColliderComponent::GetSize() const noexcept { return size_; }
float ColliderComponent::GetRadius() const noexcept { return radius_; }
float ColliderComponent::GetHeight() const noexcept { return height_; }
bool ColliderComponent::IsTrigger() const noexcept { return trigger_; }
float ColliderComponent::GetFriction() const noexcept { return friction_; }
float ColliderComponent::GetRestitution() const noexcept { return restitution_; }
float ColliderComponent::GetDensity() const noexcept { return density_; }
uint32_t ColliderComponent::GetCollisionLayer() const noexcept { return collisionLayer_; }
uint32_t ColliderComponent::GetCollisionMask() const noexcept { return collisionMask_; }
const std::string& ColliderComponent::GetCollisionGeometryAssetId() const noexcept { return collisionGeometryAssetId_; }
bool ColliderComponent::UsesCollisionGeometryAsset() const noexcept {
    return fitMode_ == ColliderFitMode::CollisionAsset && !collisionGeometryAssetId_.empty();
}

void ColliderComponent::SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
void ColliderComponent::SetShapeType(PHYSICS::PhysicsShapeType type) noexcept { shapeType_ = type; }

} // namespace HIKARI
