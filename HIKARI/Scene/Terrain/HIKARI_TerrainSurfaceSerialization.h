#pragma once

#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI::TERRAIN {
void SerializeTerrainSurfaceSettings(const TerrainSurface& surface, nlohmann::json& out);
void SerializeTerrainSurface(const TerrainSurface& surface, nlohmann::json& out);
// 呼出元のコピーを読込み、成功時だけ component が revision を更新する。
bool DeserializeTerrainSurface(const nlohmann::json& in, TerrainSurface& surface);
} // namespace HIKARI::TERRAIN
