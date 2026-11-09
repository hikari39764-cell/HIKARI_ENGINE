#pragma once

#include "HIKARI/Project/RenderSurface.h"

// 地形の草。葉は mesh shader が世界の格子と地形の図から作る。ここは視点の周りの小区画を選ぶだけ。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

void RecordTerrainGrass(const RenderSurfaceView& view, IRenderSurfaceCommands& commands, TerrainStore& store);

} // namespace HIKARI::TERRAINKIT
