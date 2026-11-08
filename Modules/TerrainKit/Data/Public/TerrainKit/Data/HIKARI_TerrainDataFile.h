#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainData.h"

// 地形の保存形式（今の版だけを読む。旧版の移行はしない）。
//
// 層スタックの記述：<場景>.terrain.json（WriteHeightLayerStackJson）。
// セルファイル：<場景>.cells/x+003_z-012.terrain.bin。1 タイル = 1 ファイルで、焼いた高さと制御図（実行時が読む）、
// 手彫り層の値とマスク（編集時だけ読む）をチャンクに分けて持つ。数値は全て little endian。
//
//   ヘッダー 40 B : magic "HKTC", u16 版, u16 予約 0, i32 tileX, i32 tileZ, u32 tileCells, u32 チャンク数,
//                   f32 heightMin, f32 heightRange, u32 目録の CRC32, u32 ヘッダー（先頭 36 B）の CRC32
//   目録 32 B × n : u32 種類, u32 層 ID, u8 圧縮（0 生 / 1 deflate）, u8 要素の大きさ, u16 予約 0,
//                   u64 位置, u32 保存した大きさ, u32 元の大きさ, u32 保存したバイトの CRC32
//   本体         : 目録の順に詰める
//
// 種類："HGHT" 焼いた高さ u16（生）、"CTRL" 制御図 u32、"LVAL" 手彫り層の値 f32、"LMSK" 層のマスク f32（どれも deflate）。
namespace HIKARI::TERRAIN::DATA {

inline constexpr uint16_t kTerrainCellFileVersion = 1u;
inline constexpr uint32_t kTerrainCellHeaderBytes = 40u;
inline constexpr uint32_t kTerrainCellDirectoryEntryBytes = 32u;

// "x+003_z-012.terrain.bin"
std::string TerrainCellFileName(TileCoord tile);

struct TerrainCellWriteOptions {
    // 焼いた高さを入れるか（編集中の途中保存では省ける）。
    bool includeBakedHeights = true;
};

// タイルの全チャンクを書く。evaluator は焼いた高さに使う（includeBakedHeights の時は必須）。
bool WriteTerrainCell(const TerrainData& data, TileCoord tile, const HeightStackEvaluator* evaluator,
                      const TerrainCellWriteOptions& options, std::vector<uint8_t>& out, std::string& error);
// ファイルを検証してから、そのタイルの手彫り・マスク・制御図を置き換える（失敗時は data を変えない）。
bool ReadTerrainCell(TerrainData& data, TileCoord expected, std::span<const uint8_t> bytes, std::string& error);

// 実行時が読む部分だけ（焼いた高さと制御図）。
struct TerrainCellRuntime {
    TileRaster<uint16_t> heights{};
    TileRaster<uint32_t> control{};
};
bool ReadTerrainCellRuntime(const TerrainGrid& grid, TileCoord expected, std::span<const uint8_t> bytes,
                            TerrainCellRuntime& out, std::string& error);

bool SaveTerrainCellFile(const std::filesystem::path& path, std::span<const uint8_t> bytes, std::string& error);
bool LoadTerrainCellFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error);

} // namespace HIKARI::TERRAIN::DATA
