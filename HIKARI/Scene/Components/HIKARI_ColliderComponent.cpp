#include "Scene/Components/HIKARI_ColliderComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "Physics/HIKARI_PhysicsBodyValidator.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

ResolvedColliderShape ScaleColliderShape(const ResolvedColliderShape& shape, const MATH::Vec3& worldScale) noexcept {
    const MATH::Vec3 scale{std::abs(worldScale.x), std::abs(worldScale.y), std::abs(worldScale.z)};
    ResolvedColliderShape scaled = shape;
    scaled.center = {shape.center.x * scale.x, shape.center.y * scale.y, shape.center.z * scale.z};
    scaled.size = {shape.size.x * scale.x, shape.size.y * scale.y, shape.size.z * scale.z};
    const float radialScale = shape.type == PHYSICS::PhysicsShapeType::Sphere ? (std::max)({scale.x, scale.y, scale.z})
                                                                              : (std::max)(scale.x, scale.z);
    scaled.radius = shape.radius * radialScale;
    scaled.height = shape.height * scale.y;
    return scaled;
}

ColliderSettings NormalizeColliderSettings(ColliderSettings settings) {
    settings = JSONFIELDS::Sanitize(std::move(settings), kColliderFields);
    settings.height = (std::max)(settings.height, settings.radius * 2.0f);
    if (settings.filter.layer == 0u) {
        settings.filter.layer = 1u;
    }
    return settings;
}

void ColliderComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kColliderFields);
}

void ColliderComponent::Deserialize(const nlohmann::json& in) {
    ColliderSettings decoded{};
    JSONFIELDS::Read(in, decoded, kColliderFields);
    settings_ = NormalizeColliderSettings(std::move(decoded));
}

bool ColliderComponent::IsEnabled() const noexcept { return settings_.enabled; }
ColliderFitMode ColliderComponent::GetFitMode() const noexcept { return settings_.fitMode; }
ResolvedColliderShape ColliderComponent::ResolveShape() const noexcept {
    ResolvedColliderShape resolved{};
    resolved.type = settings_.shape;
    resolved.center = settings_.center;
    resolved.rotationEulerDegrees = settings_.rotation;
    resolved.size = settings_.size;
    resolved.radius = settings_.radius;
    resolved.height = settings_.height;
    if (settings_.fitMode != ColliderFitMode::Geometry) {
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

const MATH::Vec3& ColliderComponent::GetCenter() const noexcept { return settings_.center; }
const MATH::Vec3& ColliderComponent::GetSize() const noexcept { return settings_.size; }
float ColliderComponent::GetRadius() const noexcept { return settings_.radius; }
float ColliderComponent::GetHeight() const noexcept { return settings_.height; }
bool ColliderComponent::IsTrigger() const noexcept { return settings_.trigger; }
float ColliderComponent::GetFriction() const noexcept { return settings_.material.friction; }
float ColliderComponent::GetRestitution() const noexcept { return settings_.material.restitution; }
float ColliderComponent::GetDensity() const noexcept { return settings_.material.density; }
uint32_t ColliderComponent::GetCollisionLayer() const noexcept { return settings_.filter.layer; }
uint32_t ColliderComponent::GetCollisionMask() const noexcept { return settings_.filter.mask; }
const std::string& ColliderComponent::GetCollisionGeometryAssetId() const noexcept {
    return settings_.collisionAssetId;
}
bool ColliderComponent::UsesCollisionGeometryAsset() const noexcept {
    return settings_.fitMode == ColliderFitMode::CollisionAsset && !settings_.collisionAssetId.empty();
}

void ColliderComponent::SetEnabled(bool enabled) noexcept { settings_.enabled = enabled; }
void ColliderComponent::SetShapeType(PHYSICS::PhysicsShapeType type) noexcept {
    // 名前の表に無い形は保存できないので受け付けない。
    if (TEXT::IsNamedEnum(kColliderShapeTypeNames, type)) {
        settings_.shape = type;
    }
}

} // namespace HIKARI
