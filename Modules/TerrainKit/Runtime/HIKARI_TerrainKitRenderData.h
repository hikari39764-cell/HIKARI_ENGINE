#pragma once

#include <cstdint>

#include "HIKARI/Project/RenderSurface.h"

// 地形の描画資源の準備：瓦片の高さ・法線・制御図の転送、材質の配列、遠景の色、光線追跡の代理。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

// 瓦片の節の格子（辺あたりの区画数）。
inline constexpr uint32_t kGridCells = 32u;

// 描画の前に毎フレーム一度。ここだけで資源を作り、内容を送る。
void PrepareTerrainRenderData(IRenderSurfaceResources& resources, TerrainStore& store);

} // namespace HIKARI::TERRAINKIT
