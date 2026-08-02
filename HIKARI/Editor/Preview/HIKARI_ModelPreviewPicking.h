#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

class ModelPreviewScene;

struct ModelPreviewPointerRay {
  MATH::Vec3 origin{};
  MATH::Vec3 direction{};
};

struct ModelPreviewSurfacePick {
  uint32_t materialIndex = 0u;
  int32_t nodeIndex = -1;
  int32_t meshIndex = -1;
  uint32_t primitiveIndex = 0u;
  float distance = 0.0f;
  bool valid = false;
};

bool BuildModelPreviewPointerRay(const Camera3D &camera,
                                 float normalizedViewportX,
                                 float normalizedViewportY,
                                 ModelPreviewPointerRay &outRay) noexcept;

ModelPreviewSurfacePick PickModelPreviewSurface(
    const ModelPreviewPointerRay &ray,
    const ModelPreviewScene &scene) noexcept;

} // namespace HIKARI::EDITOR
