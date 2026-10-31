#include "Scene/Terrain/HIKARI_TerrainSurfaceFollowerComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

namespace {
// 範囲へ丸める。
TerrainSurfaceFollowerSettings Normalize(TerrainSurfaceFollowerSettings settings) {
    return JSONFIELDS::Sanitize(std::move(settings), kTerrainSurfaceFollowerFields);
}
} // namespace

float TerrainSurfaceFollowerComponent::ResolveRadius() const noexcept {
    if (settings_.radius > 0.0f) {
        return settings_.radius;
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
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kTerrainSurfaceFollowerFields);
}

void TerrainSurfaceFollowerComponent::Deserialize(const nlohmann::json& in) {
    TerrainSurfaceFollowerSettings decoded{};
    JSONFIELDS::Read(in, decoded, kTerrainSurfaceFollowerFields);
    settings_ = Normalize(std::move(decoded));
}

#if defined(HIKARI_WITH_EDITOR)
void TerrainSurfaceFollowerComponent::BuildInspector(IInspectorBuilder& builder) {
    TerrainSurfaceFollowerSettings next = settings_;
    if (InspectFields(builder, next, kTerrainSurfaceFollowerFields)) {
        settings_ = Normalize(std::move(next));
    }
}
#endif

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
            targetY = surfaceHeight + settings_.heightOffset + ResolveRadius();
        }
    }

    if (!smoothedHeightValid_) {
        smoothedHeight_ = targetY;
        smoothedHeightValid_ = true;
    } else {
        const float rate = 1.0f - std::exp(-dt / settings_.smoothingTime);
        smoothedHeight_ += (targetY - smoothedHeight_) * rate;
    }

    const MATH::Mat4 worldMatrix = MATH::Mat4::TRS({physicsPosition.x, smoothedHeight_, physicsPosition.z},
                                                   owner->GetTransform().rotation, owner->GetTransform().scale);
    presentation->SetWorldMatrix(owner->GetRuntimeHandle(), worldMatrix);
}

} // namespace HIKARI
