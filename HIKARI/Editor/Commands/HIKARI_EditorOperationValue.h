#pragma once
#include <string>
#include <string_view>
#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR {
inline float ReadEditorOperationFloat(const nlohmann::json& json, const char* key, float fallback) {
    const auto found = json.find(key);
    if (found == json.end()) {
        return fallback;
    }
    if (found->is_number()) {
        return found->get<float>();
    }
    if (found->is_string()) {
        try {
            return std::stof(found->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

// 資産の引数（GUID か、プロジェクト内の相対・絶対パス）を type の資産記録にする。空・見つからない・種類違いは nullptr。
inline const AssetRecord* FindOperationAsset(const AssetDatabase& database, std::string_view text, AssetType type) {
    if (text.empty()) {
        return nullptr;
    }
    const AssetRecord* record = database.FindByGuid(AssetGuid{std::string(text)});
    if (record == nullptr) {
        record = database.FindByPath(TEXT::PathFromUtf8(text));
    }
    return record != nullptr && record->type == type ? record : nullptr;
}
} // namespace HIKARI::EDITOR
