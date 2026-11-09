#pragma once

#include <cstdint>
#include <optional>

#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 地面に従う物体（GroundAnchorComponent）を、地面の形が変わった時に置き直す。どの工作区で地形を変えても効く。
// 筆を動かしている間は少し間を空けてまとめ、続く置き直しは一つの撤销にする。
class GroundFollowService {
public:
    // 置き直した時だけ撤销の記録を返す（文書はもう書き換えてある）。
    std::optional<SceneObjectAuthoringHistoryRequest> Update(DocumentSceneBase& scene, bool sceneDirty,
                                                             double nowSeconds);

private:
    uint64_t seenRevision_ = 0u;
    bool pending_ = false;
    double lastApplySeconds_ = -1.0e9;
};

} // namespace HIKARI::EDITOR
