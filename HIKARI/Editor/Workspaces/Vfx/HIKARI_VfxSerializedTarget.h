#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <json.hpp>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {

using VfxStableIdAllocator = std::function<std::string(std::string_view)>;

bool BuildVfxSerializedDocument(const VFX::VfxAssetData& asset, nlohmann::json& outDocument, std::string& outMessage);
bool ParseVfxSerializedDocument(const nlohmann::json& document, VFX::VfxAssetData& outAsset, std::string& outMessage);

nlohmann::json* FindVfxSerializedTarget(nlohmann::json& document, std::string_view targetId) noexcept;
const nlohmann::json* FindVfxSerializedTarget(const nlohmann::json& document, std::string_view targetId) noexcept;
const nlohmann::json* FindVfxSerializedProperty(const nlohmann::json& target, std::string_view propertyPath);

bool SetVfxSerializedProperty(nlohmann::json& document, std::string_view targetId, std::string_view propertyPath,
                              const nlohmann::json& value, const VfxStableIdAllocator& allocateId,
                              std::string& outMessage);

nlohmann::json DescribeVfxSerializedProperties(const nlohmann::json& target);

} // namespace HIKARI::EDITOR
