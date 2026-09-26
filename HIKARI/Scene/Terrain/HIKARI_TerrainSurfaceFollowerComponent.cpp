#include "Scene/Terrain/HIKARI_TerrainSurfaceFollowerComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_JsonRead.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"

namespace HIKARI {

float TerrainSurfaceFollowerComponent::GetRadius() const noexcept { return radius_; }

void TerrainSurfaceFollowerComponent::SetRadius(float radius) {
    radius_ = (std::max)(std::isfinite(radius) ? radius : 0.0f, 0.0f);
}

float TerrainSurfaceFollowerComponent::GetHeightOffset() const noexcept { return heightOffset_; }

void TerrainSurfaceFollowerComponent::SetHeightOffset(float offset) {
    heightOffset_ = std::isfinite(offset) ? offset : 0.0f;
}

float TerrainSurfaceFollowerComponent::GetSmoothingTime() const noexcept { return smoothingTime_; }

void TerrainSurfaceFollowerComponent::SetSmoothingTime(float seconds) {
    smoothingTime_ = (std::max)(std::isfinite(seconds) ? seconds : 0.08f, 0.001f);
}

float TerrainSurfaceFollowerComponent::ResolveRadius() const noexcept {
    if (radius_ > 0.0f) {
        return radius_;
    }
    const GameObject* owner = GetOwner();
    float localRadius = 0.0f;
    if (owner != nullptr) {
        if (const ColliderComponent* collider = owner->GetComponent<ColliderComponent>()) {
            const ResolvedColliderShape shape = collider->ResolveShape();
            if (shape.type == PHYSICS::PhysicsShapeType::Sphere) {
                localRadius = shape.radius;
            }
        }
        if (localRadius <= 0.0f) {
            if (const ProceduralMeshComponent* procedural = owner->GetComponent<ProceduralMeshComponent>()) {
                GeometryFitDesc fit{};
                if (procedural->QueryGeometryFit(fit) && fit.shape == GeometryFitShape::Sphere) {
                    localRadius = fit.radius;
                }
            }
        }
    }
    if (localRadius <= 0.0f) {
        return 0.5f;
    }
    // stamp 側と同じく、所有者のユニフォームスケールを適用して
    // 視覚半径と凹み半径を一致させる。
    const float worldScale =
        owner != nullptr
            ? (std::max)({owner->GetTransform().scale.x, owner->GetTransform().scale.y, owner->GetTransform().scale.z})
            : 1.0f;
    return localRadius * worldScale;
}

void TerrainSurfaceFollowerComponent::Serialize(nlohmann::json& out) const {
    out = {
        {"_schemaVersion", 1u},
        {"radius", radius_},
        {"heightOffset", heightOffset_},
        {"smoothingTime", smoothingTime_},
    };
}

void TerrainSurfaceFollowerComponent::Deserialize(const nlohmann::json& in) {
    if (!in.is_object()) {
        return;
    }
    SetRadius(in.value("radius", radius_));
    SetHeightOffset(in.value("heightOffset", heightOffset_));
    SetSmoothingTime(in.value("smoothingTime", smoothingTime_));
}

void TerrainSurfaceFollowerComponent::OnDetach() {
    GameObject* owner = GetOwner();
    if (owner == nullptr || owner->GetWorld() == nullptr) {
        return;
    }
    if (auto* presentation = owner->GetWorld()->Services().Find<PresentationTransformService>()) {
        presentation->Remove(owner->GetRuntimeHandle());
    }
    smoothedHeightValid_ = false;
}

void TerrainSurfaceFollowerComponent::Update(float dt) {
    GameObject* owner = GetOwner();
    if (owner == nullptr || owner->GetWorld() == nullptr) {
        return;
    }
    World& world = *owner->GetWorld();
    TERRAIN::TerrainSurfaceQueryService* query = world.Services().Find<TERRAIN::TerrainSurfaceQueryService>();
    PresentationTransformService* presentation = world.Services().Find<PresentationTransformService>();
    if (presentation == nullptr) {
        return;
    }

    const MATH::Vec3 physicsPosition = owner->GetTransform().position;
    float targetY = physicsPosition.y;
    if (query != nullptr) {
        float surfaceHeight = 0.0f;
        MATH::Vec3 normal{};
        if (query->SampleWorldHeight(physicsPosition, surfaceHeight, normal)) {
            targetY = surfaceHeight + heightOffset_ + ResolveRadius();
        }
    }

    if (!smoothedHeightValid_) {
        smoothedHeight_ = targetY;
        smoothedHeightValid_ = true;
    } else {
        const float rate = 1.0f - std::exp(-dt / smoothingTime_);
        smoothedHeight_ += (targetY - smoothedHeight_) * rate;
    }

    const MATH::Mat4 worldMatrix = MATH::Mat4::TRS({physicsPosition.x, smoothedHeight_, physicsPosition.z},
                                                   owner->GetTransform().rotation, owner->GetTransform().scale);
    presentation->SetWorldMatrix(owner->GetRuntimeHandle(), worldMatrix);
}

} // namespace HIKARI
