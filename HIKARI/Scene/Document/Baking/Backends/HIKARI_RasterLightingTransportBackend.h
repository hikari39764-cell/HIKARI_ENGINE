#pragma once

#include <cstdint>

#include "Render3D/Lighting/Transport/HIKARI_LightingTransportContract.h"

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
            RENDER3D::LIGHTING::TRANSPORT::
                LightingTransportBackendCapabilities
                GetCapabilities() const noexcept;

            bool RenderProbeFace(
                DocumentSceneBase& scene,
                TOOLS::BAKING::ProbeCubemapCaptureTarget& captureTarget,
                const Camera3D& faceCamera,
                const SceneEnvironment& captureEnvironment,
                uint32_t faceIndex,
                RENDER3D::LIGHTING::TRANSPORT::
                    LightingTransportProductKind product) const;
        };

    } // namespace DOCUMENT_SCENE_BAKING

} // namespace HIKARI
