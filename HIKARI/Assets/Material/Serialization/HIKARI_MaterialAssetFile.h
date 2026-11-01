#pragma once

#include <filesystem>
#include <string>

#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"

// 標準 PBR の値だけで材質原稿ファイルを読み書きする。原稿 JSON そのものの読み書きは HIKARI_MaterialSourceJson.h、
// 描画側が読むデータの型は RenderContracts の HIKARI_MaterialAssetData.h。
namespace HIKARI {

// v1-v3 の固定 PBR API は既存 consumer の移行窓口として保持する。
bool LoadPbrMaterialAssetData(const std::filesystem::path& path, PbrMaterialAssetData& outData, std::string& outError);

bool SavePbrMaterialAssetData(const std::filesystem::path& path, const PbrMaterialAssetData& data,
                              std::string& outError);

} // namespace HIKARI
