#pragma once

#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Shader/HIKARI_ShaderAssetResolver.h"

namespace HIKARI {
class AssetRegistry;
} // namespace HIKARI

namespace HIKARI::ASSETS::SHADER {

// 登録済みの Shader 資産を GUID で引き、ShaderAssetResolver で契約と照合する。
// レジストリが安定している間に、その所有スレッドで呼ぶ。返したリースはレジストリより長く生きてよい。
ShaderAssetResolveResult ResolveShaderAssetByGuid(const AssetRegistry& registry, const AssetGuid& assetGuid,
                                                  const ShaderAssetEntryExpectation& entry,
                                                  const ShaderAssetContractExpectation& contract);

} // namespace HIKARI::ASSETS::SHADER
