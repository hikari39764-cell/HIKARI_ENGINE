#pragma once

#include <string>
#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

struct SceneObjectAuthoringHistoryRequest {
    std::string label{};
    std::vector<SceneObjectData> beforeObjects{};
    std::vector<SceneObjectData> afterObjects{};
    SceneCameraSettings beforeCamera{};
    SceneCameraSettings afterCamera{};
    bool dirtyBefore = false;
    bool runtimeWorldAffected = true;
    bool runtimeWorldRefreshRequired = false;
    // 0 以外なら、同じ値の続く編集を一つの取り消しにまとめる（ドラッグ中の数値など）。
    uint64_t mergeGroup = 0;
};

} // namespace HIKARI
