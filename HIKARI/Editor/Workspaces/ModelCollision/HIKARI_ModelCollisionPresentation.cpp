#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPresentation.h"

#include <string>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::EDITOR::MODEL_COLLISION_PRESENTATION {

const char* ShapeTypeLabel(ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {
    switch (type) {
    case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
        return TEXT::Localize("ui.sceneCreation.sphere").c_str();
    case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
        return TEXT::Localize("ui.sceneCreation.capsule").c_str();
    case ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull:
        return TEXT::Localize("editor.ui.convex.hull.483fd1").c_str();
    case ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh:
        return TEXT::Localize("editor.ui.static.mesh.cb0a46").c_str();
    case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
    default: return TEXT::Localize("ui.sceneCreation.box").c_str();
    }
}

bool MatchesSearch(std::string_view text, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    const std::string lowerText = TEXT::ToLowerAsciiCopy(text);
    const std::string lowerQuery = TEXT::ToLowerAsciiCopy(query);
    return lowerText.find(lowerQuery) != std::string::npos;
}

} // namespace HIKARI::EDITOR::MODEL_COLLISION_PRESENTATION
