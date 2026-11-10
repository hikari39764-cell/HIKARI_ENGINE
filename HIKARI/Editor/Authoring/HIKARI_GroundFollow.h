#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 地面に従う物体（GroundAnchorComponent）を地面へ置き続ける。
// - 地面の形が変わったら（どの工作区で地形を変えても）置き直す。筆の間は少し間を空けてまとめ、続く置き直しは一つの撤销。
// - 人や智能体の編集で動かした・足した・約束を付けた物は、その編集の中で置く（撤销は編集と一つ）。
//   撤销や重做では置き直さないので、撤销の流れを壊さない。
class GroundFollowService {
public:
    // 置き直した時だけ撤销の記録を返す（文書はもう書き換えてある）。
    std::optional<SceneObjectAuthoringHistoryRequest> Update(DocumentSceneBase& scene, bool sceneDirty,
                                                             double nowSeconds);
    // 記録する前の編集に、地面に従う物体の置き直しを足す（history と文書の両方を書き換える）。
    // 模型がまだ描画の写しに無い物は次の Update で置く。
    void ApplyToEdit(DocumentSceneBase& scene, SceneObjectAuthoringHistoryRequest& history);

private:
    uint64_t seenRevision_ = 0u;
    bool pending_ = false;
    double lastApplySeconds_ = -1.0e9;
    // 編集で置くはずだったが、模型の底がまだ分からなかった物。
    std::vector<uint64_t> waiting_{};
};

} // namespace HIKARI::EDITOR
