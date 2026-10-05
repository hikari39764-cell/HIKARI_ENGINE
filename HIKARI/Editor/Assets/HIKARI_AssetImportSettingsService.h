#pragma once

#include <functional>
#include "Assets/HIKARI_AssetMeta.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {
class AssetDatabase;
struct AssetRecord;
namespace EDITOR {
struct AssetImportSettingsDraft {
    AssetMeta baseline;
    AssetMeta edited;
    bool IsDirty() const;
    void Reset(const AssetRecord& record);
};

EditorOperationResult ApplyAssetImportSettings(AssetDatabase& database, const AssetGuid& guid,
                                               AssetImportSettingsDraft& draft);
void RegisterAssetImportSettingsOperations(EditorOperationRegistry& registry,
                                           std::function<AssetDatabase*()> databaseProvider);
} // namespace EDITOR
} // namespace HIKARI
