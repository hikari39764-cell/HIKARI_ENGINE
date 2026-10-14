#include "Editor/Authoring/HIKARI_StructuredAssetAuthoringService.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include <chrono>
#include <set>

namespace HIKARI::EDITOR {
bool UpdateSequenceAssets(AssetDatabase& database, const std::vector<std::pair<AssetGuid, CinematicSequence>>& updates,
                          std::string& message) {
    std::vector<IO::FileReplacementOperation> files;
    const auto cleanup = [&] {
        for (const auto& f : files) {
            std::error_code ec;
            std::filesystem::remove(f.stagedPath, ec);
        }
    };
    const auto suffix =
        ".sequence-stage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::set<std::string> seen;
    for (const auto& [guid, sequence] : updates) {
        const auto* record = database.FindByGuid(guid);
        if (!record || record->type != AssetType::Sequence || !seen.insert(guid.value).second) {
            cleanup();
            message = "Sequence group contains a missing or repeated asset.";
            return false;
        }
        const auto path = (database.GetProjectRoot() / record->sourcePath).lexically_normal();
        files.push_back({path, std::filesystem::path(path.wstring() + std::filesystem::path(suffix).wstring())});
        SequenceAsset asset;
        asset.guid = guid;
        asset.displayName = sequence.name;
        asset.sequence = sequence;
        if (!SaveSequenceAsset(files.back().stagedPath, asset, &message)) {
            cleanup();
            return false;
        }
    }
    // ステージング中の失敗も、置換中の失敗もソースを半分だけ更新しない。
    if (!IO::CommitFileReplacementTransaction(files, message)) {
        cleanup();
        return false;
    }
    cleanup();
    bool imported = true;
    for (const auto& [guid, sequence] : updates) {
        if (const auto* found = database.FindByGuid(guid)) {
            auto record = *found;
            record.displayName = sequence.name;
            record.meta.displayName = sequence.name;
            imported = database.WriteMeta(record) && imported;
        }
    }
    imported = database.ScanAssets(false) && imported;
    for (const auto& [guid, sequence] : updates) {
        (void)sequence;
        imported = database.ImportAsset(guid) && imported;
    }
    message = imported ? "关卡机位及关联演出已保存。" : "源文件已保存，但部分资产导入失败，请检查资产诊断。";
    return true;
}
} // namespace HIKARI::EDITOR
