#pragma once

#include <string>
#include <vector>

#include "Project/HIKARI_ProjectModules.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 項目設定の画面と操作（project.modules）が共有する一覧の一行：選べるモジュールと、今のセッションでの読み込み状態。
struct ProjectModuleRow {
    ProjectModuleOption option{};
    // 今のセッションで読み込みを試みた時の状態（試みていなければ nullptr）。
    const InstalledModuleStatus* installed = nullptr;
    // Project.hikari の指定と今の読み込みが食い違う（項目を開き直すと反映される）。
    bool reopenPending = false;
};

struct ProjectModuleList {
    std::vector<ProjectModuleRow> rows{};
    // Project.hikari が読めない時の理由。
    std::string error{};
};

ProjectModuleList BuildProjectModuleList(const DocumentSceneBase& scene);

} // namespace HIKARI::EDITOR
