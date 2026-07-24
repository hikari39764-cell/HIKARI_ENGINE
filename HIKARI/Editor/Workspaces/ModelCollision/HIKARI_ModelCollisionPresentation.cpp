#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPresentation.h"

#include <string>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::EDITOR::MODEL_COLLISION_PRESENTATION {

const char *
ShapeTypeLabel(ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {
  switch (type) {
  case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
    return "Sphere";
  case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
    return "Capsule";
  case ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull:
    return "Convex Hull";
  case ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh:
    return "Static Mesh";
  case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
  default:
    return "Box";
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
