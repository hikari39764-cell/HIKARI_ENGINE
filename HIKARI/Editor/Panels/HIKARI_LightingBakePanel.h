#pragma once

#include <cstdint>
#include <string>

#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {

    class DocumentSceneBase;

    class LightingBakePanel {
    public:
        void Draw(DocumentSceneBase& scene, bool& open);

    private:
        TOOLS::BAKING::LightingBakeStatus cachedStatus_{};
        TOOLS::BAKING::LightingBakeReport lastReport_{};
        std::string cachedSceneGuid_{};
        uint64_t cachedSceneRevision_ = 0u;
        uint64_t cachedAssetRevision_ = 0u;
        bool hasCachedStatus_ = false;
        bool statusRefreshRequested_ = true;
        bool wasBakeBusy_ = false;
        bool hasReport_ = false;
        std::string lastOpenFolderMessage_{};
    };

} // namespace HIKARI
