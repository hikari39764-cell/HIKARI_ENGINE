#include "Editor/Authoring/HIKARI_CollisionAuthoringService.h"

#include <algorithm>
#include <sstream>
#include <string_view>

#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/HIKARI_ComponentRegistry.h"

namespace HIKARI::EDITOR {
    namespace {
        SceneComponentData* FindFirstComponent(
            SceneObjectData& object,
            std::string_view typeName) {
            const auto found = std::find_if(
                object.components.begin(),
                object.components.end(),
                [typeName](const SceneComponentData& component) {
                    return component.type == typeName;
                });
            return found != object.components.end() ? &*found : nullptr;
        }

        const SceneComponentData* FindFirstComponent(
            const SceneObjectData& object,
            std::string_view typeName) {
            const auto found = std::find_if(
                object.components.begin(),
                object.components.end(),
                [typeName](const SceneComponentData& component) {
                    return component.type == typeName;
                });
            return found != object.components.end() ? &*found : nullptr;
        }

        bool HasAuthoringGeometry(const SceneObjectData& object) {
            if (FindFirstComponent(
                    object,
                    "ProceduralMeshComponent") != nullptr) {
                return true;
            }
            const SceneComponentData* model = FindFirstComponent(
                object,
                "ModelComponent");
            return model != nullptr &&
                model->properties.is_object() &&
                !model->properties.value(
                    "assetId",
                    std::string{}).empty();
        }

        std::string FindModelAssetId(const SceneObjectData& object) {
            const SceneComponentData* model = FindFirstComponent(
                object,
                "ModelComponent");
            return model != nullptr && model->properties.is_object()
                ? model->properties.value("assetId", std::string{})
                : std::string{};
        }

        const char* ToMotionType(CollisionBodyMode mode) noexcept {
            switch (mode) {
            case CollisionBodyMode::Kinematic:
                return "Kinematic";
            case CollisionBodyMode::Dynamic:
                return "Dynamic";
            case CollisionBodyMode::Static:
            default:
                return "Static";
            }
        }

        bool SetProperty(
            nlohmann::json& properties,
            const char* name,
            const nlohmann::json& value) {
            if (!properties.is_object()) {
                properties = nlohmann::json::object();
            }
            const auto found = properties.find(name);
            if (found != properties.end() && *found == value) {
                return false;
            }
            properties[name] = value;
            return true;
        }

        bool MatchesScope(
            const SceneObjectData& object,
            const CollisionAuthoringRequest& request) noexcept {
            return request.scope ==
                    CollisionAuthoringScope::SceneGeometry ||
                object.id == request.selectedObject;
        }
    }

    CollisionAuthoringResult CollisionAuthoringService::Apply(
        const ComponentRegistry& registry,
        SceneDocument& document,
        const CollisionAuthoringRequest& request) const {
        CollisionAuthoringResult result{};
        if (request.scope ==
                CollisionAuthoringScope::SelectedObject &&
            request.selectedObject.value == 0u) {
            result.message = "Select a scene object first.";
            return result;
        }
        DocumentComponentAuthoringService componentService{};
        for (SceneObjectData& object : document.objects) {
            if (!MatchesScope(object, request) ||
                !HasAuthoringGeometry(object)) {
                continue;
            }
            const std::string modelAssetId = FindModelAssetId(object);
            if (request.shapeSource ==
                    CollisionAuthoringShapeSource::ModelDefaultCollision &&
                modelAssetId.empty()) {
                continue;
            }
            ++result.matchedObjectCount;
            bool objectChanged = false;
            SceneComponentData* collider = FindFirstComponent(
                object,
                "ColliderComponent");
            const bool createdCollider = collider == nullptr;
            if (createdCollider) {
                const ComponentAddResult addCollider =
                    componentService.AddComponent(
                        registry,
                        object,
                        "ColliderComponent");
                if (!addCollider.success) {
                    continue;
                }
                collider = FindFirstComponent(
                    object,
                    "ColliderComponent");
                if (collider == nullptr) {
                    continue;
                }
                ++result.createdColliderCount;
                objectChanged = true;
            }

            if (createdCollider ||
                request.existingColliderPolicy ==
                    ExistingColliderPolicy::ConfigureFirst) {
                const bool fitChanged = SetProperty(
                    collider->properties,
                    "fitMode",
                    request.shapeSource ==
                            CollisionAuthoringShapeSource::ModelDefaultCollision
                        ? "CollisionAsset"
                        : "Geometry");
                const bool assetChanged = SetProperty(
                    collider->properties,
                    "collisionAssetId",
                    request.shapeSource ==
                            CollisionAuthoringShapeSource::ModelDefaultCollision
                        ? nlohmann::json(modelAssetId)
                        : nlohmann::json(std::string{}));
                const bool triggerChanged = SetProperty(
                    collider->properties,
                    "trigger",
                    request.makeTrigger);
                if (!createdCollider && fitChanged) {
                    ++result.fittedColliderCount;
                }
                objectChanged |= fitChanged || assetChanged || triggerChanged;
            }

            SceneComponentData* body = FindFirstComponent(
                object,
                "PhysicsBodyComponent");
            if (request.bodyMode != CollisionBodyMode::Static &&
                body == nullptr) {
                const ComponentAddResult addBody =
                    componentService.AddComponent(
                        registry,
                        object,
                        "PhysicsBodyComponent");
                if (addBody.success) {
                    body = FindFirstComponent(
                        object,
                        "PhysicsBodyComponent");
                    ++result.createdBodyCount;
                    objectChanged = true;
                }
            }
            if (body != nullptr) {
                objectChanged |= SetProperty(
                    body->properties,
                    "motionType",
                    ToMotionType(request.bodyMode));
            }

            if (objectChanged) {
                ++result.changedObjectCount;
                result.documentChanged = true;
            }
        }

        result.success = result.matchedObjectCount > 0u;
        if (!result.success) {
            result.message = request.scope ==
                    CollisionAuthoringScope::SelectedObject
                ? "Selected object has no model or procedural geometry."
                : "No scene geometry objects were found.";
            return result;
        }

        std::ostringstream message{};
        message << "Collision setup: "
            << result.changedObjectCount << " changed, "
            << result.createdColliderCount << " colliders, "
            << result.createdBodyCount << " bodies.";
        if (!result.documentChanged) {
            message.str(std::string{});
            message.clear();
            message << "Collision setup already matches "
                << result.matchedObjectCount << " object(s).";
        }
        result.message = message.str();
        return result;
    }

} // namespace HIKARI::EDITOR
