#pragma once

#include <cstdint>
#include <string>

#include "Scene/HIKARI_SceneDocument.h"
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
    // 焼き込みの質の編集は文書の修订号を進めないので、状態の再計算はこの値の変化でも行う。
    SceneLightingBakeSettings cachedBakeSettings_{};
    // 数値欄を編集している間の値。編集が終わった時に一度だけ操作で文書へ書く。
    SceneLightingBakeSettings qualityDraft_{};
    bool qualityDraftActive_ = false;
    bool hasCachedStatus_ = false;
    bool statusRefreshRequested_ = true;
    bool wasBakeBusy_ = false;
    std::string lastOpenFolderMessage_{};
    std::string actionMessage_{};
    char objectFilter_[128]{};
    bool onlyDirty_ = false;
};

} // namespace HIKARI
