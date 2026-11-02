#pragma once

#include <cstdint>


namespace HIKARI {

class Camera3D;
class DocumentSceneBase;
struct SceneEnvironment;

namespace TOOLS::BAKING {
class ProbeCubemapCaptureTarget;
}

namespace DOCUMENT_SCENE_BAKING {

class RasterLightingTransportBackend final {
public:
    // 反射探針の cubemap の一面（拡散の探針は DXR の経路で焼く）。
    bool RenderProbeFace(DocumentSceneBase& scene, TOOLS::BAKING::ProbeCubemapCaptureTarget& captureTarget,
                         const Camera3D& faceCamera, const SceneEnvironment& captureEnvironment,
                         uint32_t faceIndex) const;
};

} // namespace DOCUMENT_SCENE_BAKING

} // namespace HIKARI
