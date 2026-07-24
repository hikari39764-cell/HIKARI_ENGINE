#pragma once

#include <cstddef>
#include <string>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
    class ComponentRegistry;
}

namespace HIKARI::EDITOR {

    enum class CollisionAuthoringScope {
        SelectedObject,
        SceneGeometry,
    };

    enum class CollisionBodyMode {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class CollisionAuthoringShapeSource {
        GeometryFit,
        ModelDefaultCollision,
    };

    enum class ExistingColliderPolicy {
        Keep,
        ConfigureFirst,
    };

    struct CollisionAuthoringRequest {
        CollisionAuthoringScope scope =
            CollisionAuthoringScope::SelectedObject;
        SceneObjectId selectedObject{};
        CollisionBodyMode bodyMode =
            CollisionBodyMode::Static;
        CollisionAuthoringShapeSource shapeSource =
            CollisionAuthoringShapeSource::GeometryFit;
        ExistingColliderPolicy existingColliderPolicy =
            ExistingColliderPolicy::Keep;
        bool makeTrigger = false;
    };

    struct CollisionAuthoringResult {
        bool success = false;
        bool documentChanged = false;
        size_t matchedObjectCount = 0;
        size_t changedObjectCount = 0;
        size_t createdColliderCount = 0;
        size_t fittedColliderCount = 0;
        size_t createdBodyCount = 0;
        std::string message{};
    };

    class CollisionAuthoringService {
    public:
        CollisionAuthoringResult Apply(
            const ComponentRegistry& registry,
            SceneDocument& document,
            const CollisionAuthoringRequest& request) const;
    };

} // namespace HIKARI::EDITOR
