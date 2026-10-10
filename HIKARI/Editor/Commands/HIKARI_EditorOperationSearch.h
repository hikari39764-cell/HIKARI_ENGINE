#pragma once

#include <cstddef>
#include <string>

#include <json.hpp>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR {

struct EditorOperationSearchQuery {
    std::string workspace{};
    // 目的・操作名・検索語。空白区切りの語ごとにも当てる。
    std::string keyword{};
    // false なら引数は名前だけ（required / optional）。
    bool includeParameters = false;
    size_t offset = 0u;
    size_t limit = 8u;
};

// 智能体が目的から操作を探す。exposure を持つ操作だけが対象。workspace も keyword も無ければ
// 工作区ごとの件数だけを返す（全件を一度に返さない）。
nlohmann::json SearchEditorOperations(const EditorOperationRegistry& registry, EditorOperationExposureMask exposure,
                                      const EditorOperationSearchQuery& query);

// 智能体に見せる操作の説明。既定値と同じ項目（false の旗、未指定の権限、空の説明）は省く。
// includeParameters が false なら引数は名前の一覧（required / optional）だけ。
nlohmann::json DescribeEditorOperationCompact(const EditorOperationDefinition& definition, bool includeParameters);

} // namespace HIKARI::EDITOR
