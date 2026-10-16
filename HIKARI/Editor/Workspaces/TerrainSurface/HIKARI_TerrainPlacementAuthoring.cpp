#include "Editor/Workspaces/TerrainSurface/HIKARI_TerrainPlacementAuthoring.h"

#include <cmath>
#include <limits>
#include <unordered_set>

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Editor/Selection/HIKARI_EditorSelection.h"
#include "Editor/Selection/HIKARI_SceneSelectionTransform.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI::EDITOR {
namespace {
constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

bool ResolveWorldPosition(const GameObject& object, MATH::Vec3& outPosition) {
    const MATH::Vec4 world = object.GetTransform().GetWorldMatrix().TransformPoint({0.0f, 0.0f, 0.0f, 1.0f});
    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z) || !std::isfinite(world.w)) {
        return false;
    }
    if (std::fabs(world.w) > 1.0e-6f) {
        const float invW = 1.0f / world.w;
        outPosition = {world.x * invW, world.y * invW, world.z * invW};
    } else {
        outPosition = {world.x, world.y, world.z};
    }
    return true;
}

bool ResolveWorldGeometrySupport(const GameObject& object, MATH::Vec3& outSurfaceSamplePosition,
                                 float& outWorldBottom) {
    GeometryFitDesc bestFit{};
    int bestPriority = (std::numeric_limits<int>::lowest)();
    bool hasFit = false;
    for (const std::unique_ptr<IComponent>& component : object.GetComponents()) {
        const auto* provider = dynamic_cast<const IGeometryFitProvider*>(component.get());
        if (provider == nullptr) {
            continue;
        }
        GeometryFitDesc fit{};
        if (!provider->QueryGeometryFit(fit)) {
            continue;
        }
        const int priority = provider->GetGeometryFitPriority();
        if (!hasFit || priority > bestPriority) {
            bestFit = fit;
            bestPriority = priority;
            hasFit = true;
        }
    }
    if (!hasFit || !MATH::IsFinite(bestFit.center) || !MATH::IsFinite(bestFit.rotationEulerDegrees) ||
        !MATH::IsFinite(bestFit.size) || !std::isfinite(bestFit.radius) || !std::isfinite(bestFit.height)) {
        return false;
    }

    MATH::Vec3 halfExtent{};
    switch (bestFit.shape) {
    case GeometryFitShape::Sphere: halfExtent = MATH::Vec3{1.0f, 1.0f, 1.0f} * (std::max)(bestFit.radius, 0.01f); break;
    case GeometryFitShape::Capsule:
        halfExtent = {(std::max)(bestFit.radius, 0.01f), (std::max)(bestFit.height * 0.5f, bestFit.radius),
                      (std::max)(bestFit.radius, 0.01f)};
        break;
    case GeometryFitShape::Box:
    default:
        halfExtent = {(std::max)(std::fabs(bestFit.size.x) * 0.5f, 0.01f),
                      (std::max)(std::fabs(bestFit.size.y) * 0.5f, 0.01f),
                      (std::max)(std::fabs(bestFit.size.z) * 0.5f, 0.01f)};
        break;
    }

    const MATH::Quat fitRotation = MATH::Quat::FromEulerXYZ(bestFit.rotationEulerDegrees.x * kDegreesToRadians,
                                                            bestFit.rotationEulerDegrees.y * kDegreesToRadians,
                                                            bestFit.rotationEulerDegrees.z * kDegreesToRadians);
    const MATH::Mat4 fitLocal = MATH::Mat4::TRS(bestFit.center, fitRotation, {1.0f, 1.0f, 1.0f});
    const Bounds fitBounds{halfExtent * -1.0f, halfExtent};
    const Bounds worldBounds = BOUNDS::TransformBounds(fitBounds, object.GetTransform().GetWorldMatrix() * fitLocal);
    if (!BOUNDS::IsUsable(worldBounds)) {
        return false;
    }
    outSurfaceSamplePosition = {(worldBounds.min.x + worldBounds.max.x) * 0.5f, worldBounds.min.y,
                                (worldBounds.min.z + worldBounds.max.z) * 0.5f};
    outWorldBottom = worldBounds.min.y;
    return true;
}

bool SetWorldPosition(GameObject& object, const MATH::Vec3& worldPosition) {
    MATH::Mat4 world = object.GetTransform().GetWorldMatrix();
    world.m[3][0] = worldPosition.x;
    world.m[3][1] = worldPosition.y;
    world.m[3][2] = worldPosition.z;
    const MATH::Mat4 local = object.GetParent() != nullptr
                                 ? MATH::Inverse(object.GetParent()->GetTransform().GetWorldMatrix()) * world
                                 : world;
    MATH::Vec3 position{};
    MATH::Quat rotation{};
    MATH::Vec3 scale{};
    if (!MATH::DecomposeTRS(local, position, rotation, scale)) {
        return false;
    }
    Transform3D transform = object.GetTransform();
    transform.position = position;
    transform.rotation = rotation;
    transform.scale = scale;
    transform.useExplicitMatrix = false;
    return object.SetLocalTransform(transform);
}

void SyncDocumentTransform(DocumentSceneBase& scene, const GameObject& object) {
    SceneObjectData* documentObject = FindSceneObjectData(scene.GetSceneDocument(), object.GetDocumentId());
    if (documentObject == nullptr) {
        return;
    }
    const Transform3D& runtime = object.GetTransform();
    documentObject->transform.position = runtime.position;
    documentObject->transform.rotationEulerDeg =
        MATH::EulerXYZDegreesFromQuatNearest(runtime.rotation, documentObject->transform.rotationEulerDeg);
    documentObject->transform.scale = runtime.scale;
}
} // namespace

TerrainPlacementResult PlaceSelectedObjectsOnTerrain(DocumentSceneBase& scene, const EditorSelection& selection,
                                                     SceneObjectId terrainObjectId, float surfaceOffset) {
    TerrainPlacementResult result{};
    GameObject* terrainObject = scene.GetWorld().FindObject(terrainObjectId);
    TerrainSurfaceComponent* terrain =
        terrainObject != nullptr ? terrainObject->GetComponent<TerrainSurfaceComponent>() : nullptr;
    if (terrain == nullptr || !std::isfinite(surfaceOffset)) {
        return result;
    }

    std::unordered_set<uint64_t> selectedIds{};
    for (const SceneObjectId objectId : selection.GetSelectedObjectIds()) {
        if (objectId.value != 0u && objectId != terrainObjectId) {
            selectedIds.insert(objectId.value);
        }
    }

    for (const SceneObjectId objectId : selection.GetSelectedObjectIds()) {
        if (objectId.value == 0u || objectId == terrainObjectId) {
            continue;
        }
        GameObject* object = scene.GetWorld().FindObject(objectId);
        if (object == nullptr) {
            continue;
        }
        if (IsObjectEditorLocked(scene.GetSceneDocument(), objectId)) {
            ++result.skippedLocked;
            continue;
        }
        if (HasSelectedAncestor(*object, selectedIds)) {
            ++result.skippedHierarchyChildren;
            continue;
        }

        MATH::Vec3 worldPosition{};
        if (!ResolveWorldPosition(*object, worldPosition)) {
            ++result.skippedOutsideTerrain;
            continue;
        }
        MATH::Vec3 surfaceSamplePosition = worldPosition;
        float worldBottom = worldPosition.y;
        (void)ResolveWorldGeometrySupport(*object, surfaceSamplePosition, worldBottom);
        float surfaceHeight = 0.0f;
        MATH::Vec3 surfaceNormal{};
        if (!terrain->SampleWorldHeight(surfaceSamplePosition, surfaceHeight, surfaceNormal)) {
            ++result.skippedOutsideTerrain;
            continue;
        }
        worldPosition.y += surfaceHeight + surfaceOffset - worldBottom;
        if (!SetWorldPosition(*object, worldPosition)) {
            ++result.skippedOutsideTerrain;
            continue;
        }
        SyncDocumentTransform(scene, *object);
        result.changedObjectIds.push_back(objectId);
    }
    return result;
}

} // namespace HIKARI::EDITOR
