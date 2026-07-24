#pragma once

#include <string>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace RUNTIME_TOOLS {

        class PortableObjectToolsPanel {
        public:
            void Draw(DocumentSceneBase& scene);

        private:
            SceneObjectId selectedObjectId_{};
            std::string lastSceneMessage_{};
        };

    } // namespace RUNTIME_TOOLS
} // namespace HIKARI
