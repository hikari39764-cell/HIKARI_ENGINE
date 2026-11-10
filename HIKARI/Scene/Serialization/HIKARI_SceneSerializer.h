#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Scene/Partition/HIKARI_ScenePartition.h"

namespace HIKARI {

struct SceneDocument;

// 分区場景の格子ファイルの、最後に読み書きした内容の検査和。保存は内容が変わった格子と主ファイルだけを書く。
// 読めなかった格子は上書きも削除もしない（中身を失わないため）。scenePath と格子の大きさが違えば記録は使わない。
struct SceneCellFileState {
    std::filesystem::path scenePath{};
    uint32_t cellSizeMeters = 0;
    uint64_t mainChecksum = 0;
    std::map<WorldCellCoord, uint64_t> cellChecksums{};
    std::set<WorldCellCoord> unreadableCells{};
};

struct SceneSaveReport {
    bool mainWritten = false;
    std::vector<WorldCellCoord> writtenCells{};
    // 物体が無くなって消した格子ファイルの数。
    size_t removedCellFiles = 0u;
};

// 格子の普通物体のファイル：場景の隣の格子フォルダの "x+003_z-012.objects.json"。
inline constexpr std::string_view kSceneCellObjectsSuffix = ".objects.json";
std::filesystem::path MakeSceneCellObjectsPath(const std::filesystem::path& scenePath, WorldCellCoord cell);

class SceneSerializer {
public:
    // 分区場景は主ファイルの格子一覧にある格子ファイルも読む。読めない格子はエラーを記録して飛ばし、ほかは読む。
    // cells を渡すと、読んだ内容の検査和と読めなかった格子をそこへ記録する（次の保存で変わった格子だけ書くため）。
    bool LoadFromFile(const std::string& path, SceneDocument& outDocument, SceneCellFileState* cells = nullptr) const;
    // cells が同じ場景を最後に読み書きした記録なら、分区場景は内容の変わった格子と主ファイルだけを書き、記録を更新する。
    // 読めなかった格子へ物体を入れる保存や、読めない格子が残ったままの分区解除・格子の大きさの変更は断る。
    bool SaveToFile(const std::string& path, const SceneDocument& document, SceneCellFileState* cells = nullptr,
                    SceneSaveReport* report = nullptr) const;
};

} // namespace HIKARI
