#pragma once
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include <string_view>
namespace HIKARI::ASSETS::MATERIAL {
// 作業用コピーを生成。保存文書と実行中シーンには適用しない。
bool BuildMaterialNodePreview(const MaterialAssetData& source, std::string_view nodeId, std::string_view pin,
                              int channel, MaterialAssetData& result, std::string& error);
} // namespace HIKARI::ASSETS::MATERIAL
