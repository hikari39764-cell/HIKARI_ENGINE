#pragma once

#include <cstdint>
#include <string>

#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {

namespace EDITOR {
struct EditorToolContext;
}

class LightingBakePanel {
public:
    void Draw(EDITOR::EditorToolContext& tool, bool& open);

private:
    TOOLS::BAKING::LightingBakeStatus cachedStatus_{};
    std::string cachedSceneGuid_{};
    uint64_t cachedSceneRevision_ = 0u;
    uint64_t cachedAssetRevision_ = 0u;
    bool hasCachedStatus_ = false;
    bool statusRefreshRequested_ = true;
    bool wasBakeBusy_ = false;
    std::string lastOpenFolderMessage_{};
    std::string actionMessage_{};
    char objectFilter_[128]{};
    bool onlyDirty_ = false;
};

} // namespace HIKARI
