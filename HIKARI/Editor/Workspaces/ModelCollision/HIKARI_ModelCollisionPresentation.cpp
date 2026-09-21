#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPresentation.h"

#include <string>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::EDITOR::MODEL_COLLISION_PRESENTATION {

const char *
ShapeTypeLabel(ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {
  switch (type) {
  case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
    return TEXT::LocalizeCStr("ui.sceneCreation.sphere");
  case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
    return TEXT::LocalizeCStr("ui.sceneCreation.capsule");
  case ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull:
    return TEXT::LocalizeCStr("editor.ui.convex.hull.483fd1");
  case ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh:
    return TEXT::LocalizeCStr("editor.ui.static.mesh.cb0a46");
  case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
  default:
    return TEXT::LocalizeCStr("ui.sceneCreation.box");
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
