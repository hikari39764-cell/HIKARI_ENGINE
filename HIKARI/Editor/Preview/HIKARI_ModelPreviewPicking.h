#pragma once

#include <cstdint>
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

class ModelPreviewScene;

struct ModelPreviewSurfacePick {
    uint32_t materialIndex = 0u;
    int32_t nodeIndex = -1;
    int32_t meshIndex = -1;
    uint32_t primitiveIndex = 0u;
    float distance = 0.0f;
    bool valid = false;
};

ModelPreviewSurfacePick PickModelPreviewSurface(const EditorPointerRay& ray, const ModelPreviewScene& scene) noexcept;

} // namespace HIKARI::EDITOR
