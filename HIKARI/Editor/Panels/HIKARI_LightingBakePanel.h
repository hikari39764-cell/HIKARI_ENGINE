#pragma once

#include <string>

#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI {

    class DocumentSceneBase;

    class LightingBakePanel {
    public:
        void Draw(DocumentSceneBase& scene, bool& open);

    private:
        TOOLS::BAKING::LightingBakeReport lastReport_{};
        bool hasReport_ = false;
        std::string lastOpenFolderMessage_{};
    };

} // namespace HIKARI
