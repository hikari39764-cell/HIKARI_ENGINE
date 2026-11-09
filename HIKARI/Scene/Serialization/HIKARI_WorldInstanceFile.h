#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "HIKARI/Project/WorldInstanceTypes.h"
#include "Scene/Partition/HIKARI_ScenePartition.h"

// 実例集のファイル。場景の格子フォルダ（Main.cells）に置く。
//   instances.json            実例集の定義の一覧
//   x+003_z-012.instances.bin 一格の全実例集の個体。数値は全て little endian
//     見出し 32 B：magic "HKIS"、u32 版、i32 格子 x、i32 格子 z、u32 実例集の数、u32 予約 0、u64 検査和（見出しより後の FNV-1a）
//     目録：実例集ごとに u64 ID、u64 個体数
//     本体：目録の順に 32 B の個体が並ぶ
namespace HIKARI {

inline constexpr std::string_view kWorldInstanceSetsFileName = "instances.json";
inline constexpr std::string_view kWorldInstanceCellSuffix = ".instances.bin";

struct WorldInstanceCellPart {
    uint64_t setId = 0u;
    std::span<const GameProjectWorldInstance> instances{};
};

struct WorldInstanceCellRead {
    uint64_t setId = 0u;
    std::vector<GameProjectWorldInstance> instances{};
};

std::filesystem::path MakeWorldInstanceCellPath(const std::filesystem::path& cellDirectory, WorldCellCoord cell);

// 一時ファイルへ書いて置き換えるので、失敗しても前のファイルは残る。
bool WriteWorldInstanceCellFile(const std::filesystem::path& path, WorldCellCoord cell,
                                std::span<const WorldInstanceCellPart> parts, std::string& outError);
// 現行版だけを読む。見出しや検査和が合わない時は false と理由。
bool ReadWorldInstanceCellFile(const std::filesystem::path& path, WorldCellCoord cell,
                               std::vector<WorldInstanceCellRead>& outParts, std::string& outError);
// ファイル名（x+003_z-012.instances.bin）から格子を読む。
bool ParseWorldInstanceCellFileName(std::string_view fileName, WorldCellCoord& outCell) noexcept;

bool WriteWorldInstanceSetsFile(const std::filesystem::path& path,
                                std::span<const GameProjectWorldInstanceSetDesc> sets, std::string& outError);
bool ReadWorldInstanceSetsFile(const std::filesystem::path& path, std::vector<GameProjectWorldInstanceSetDesc>& outSets,
                               std::string& outError);

} // namespace HIKARI
