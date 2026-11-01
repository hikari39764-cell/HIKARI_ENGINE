#pragma once

#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI::ASSETS::MATERIAL {

// Render3D の材質プログラム registry に渡す、標準 PBR の出どころ（回退プログラムと、標準 PBR の形の資産データの
// コンパイル）。材質コンパイラは Assets にあるので、registry を持つ側（Runtime の起動、エディターの材質プレビュー）が
// 構築や Reset の時に渡す。
RENDER3D::MATERIAL::StandardPbrProgramSource MakeStandardPbrProgramSource();

} // namespace HIKARI::ASSETS::MATERIAL
