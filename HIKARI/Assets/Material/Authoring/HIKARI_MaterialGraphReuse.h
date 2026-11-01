#pragma once
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include <span>
#include <string_view>
namespace HIKARI::ASSETS::MATERIAL {
struct MaterialGraphModuleInfo {
    std::string_view id, labelKey, searchTerms;
};
std::span<const MaterialGraphModuleInfo> GetMaterialGraphModules() noexcept;
MaterialGraphData CreateMaterialGraphModule(std::string_view id);
// 単一の出力ノードを持つ選択を、既存の関数インターフェースへ変換する。
bool ExtractMaterialGraphFunction(const MaterialAssetData& source, const std::vector<std::string>& selection,
                                  const AssetGuid& functionGuid, std::string_view name, std::string_view callId,
                                  MaterialAssetData& function, MaterialAssetData& replaced, std::string& error);
} // namespace HIKARI::ASSETS::MATERIAL
