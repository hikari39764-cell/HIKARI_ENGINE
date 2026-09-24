#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::EDITOR {

struct AssetTrashResult {
    bool success = false;
    std::filesystem::path trashDirectory{};
    std::string message{};
};

struct AssetTrashReference {
    AssetGuid ownerAssetGuid{};
    std::filesystem::path ownerSourcePath{};
    std::string role{};
};

struct AssetTrashReferenceAnalysis {
    std::vector<AssetTrashReference> references{};

    bool IsReferenced() const noexcept { return !references.empty(); }
};

AssetTrashReferenceAnalysis AnalyzeAssetTrashReferences(const AssetDatabase& database, const AssetGuid& guid);

// Moves authoring source and meta files into the project Library trash.
// Cooked artifacts are left to the database/import cleanup path.
AssetTrashResult MoveAssetToProjectTrash(AssetDatabase& database, const AssetGuid& guid);

} // namespace HIKARI::EDITOR
