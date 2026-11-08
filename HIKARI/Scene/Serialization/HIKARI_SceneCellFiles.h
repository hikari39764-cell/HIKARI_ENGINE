#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <json.hpp>

#include "Scene/Partition/HIKARI_ScenePartition.h"

namespace HIKARI {

struct SceneDocument;
struct SceneObjectData;

// 根の物体の位置で物体を格子へ分ける。子は根に従い、常駐の根とその子は主ファイルに残る（分区しない場景は全部
// 主ファイル）。どちらも document の並び順。
struct SceneCellAssignment {
    std::vector<const SceneObjectData*> mainObjects{};
    std::map<WorldCellCoord, std::vector<const SceneObjectData*>> cells{};
};
SceneCellAssignment AssignSceneObjectsToCells(const SceneDocument& document);

// 格子の物体配列の検査和（詰めた JSON の FNV-1a 64）。
uint64_t ComputeSceneCellChecksum(const nlohmann::json& objects);
// 格式名・版・中身の種類・格子の大きさ・格子座標・検査和を付けた格子ファイル。
nlohmann::json MakeSceneCellObjectsFile(WorldCellCoord cell, uint32_t cellSizeMeters, nlohmann::json objects,
                                        uint64_t checksum);
// 現行版だけを読む。見出しが合わない・検査和が違う時は false と理由。
bool ReadSceneCellObjectsFile(const std::filesystem::path& path, WorldCellCoord cell, uint32_t cellSizeMeters,
                              nlohmann::json& outObjects, uint64_t& outChecksum, std::string& outError);

} // namespace HIKARI
