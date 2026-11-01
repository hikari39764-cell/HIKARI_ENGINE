#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <json.hpp>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::VFX::SERIALIZATION {

// 効果のパラメーター定義配列を読む。ref は slotCount 個の float4 のどこかを指す。
bool ReadParameterDescriptorsFromJson(const nlohmann::json& parameterArray, size_t slotCount,
                                      std::vector<ParamDesc>& outParameters, std::string& outError);

} // namespace HIKARI::VFX::SERIALIZATION
