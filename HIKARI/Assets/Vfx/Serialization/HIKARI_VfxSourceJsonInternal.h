#pragma once

#include <string>

#include <json.hpp>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::SERIALIZATION::INTERNAL {

bool ReadCurve(const nlohmann::json& node, VfxCurveData& outCurve);
bool ReadVec4(const nlohmann::json& node, MATH::Vec4& outValue);
bool ReadVec3(const nlohmann::json& node, MATH::Vec3& outValue);
bool ReadValue(const nlohmann::json& node, VfxValueType type, VfxValue& outValue);
bool ReadDistribution(const nlohmann::json& node, VfxFloatDistributionData& outValue);
bool ReadDistribution(const nlohmann::json& node, VfxVec3DistributionData& outValue);
bool ReadDistribution(const nlohmann::json& node, VfxColorDistributionData& outValue);
bool ReadAnimatedVec3(const nlohmann::json& node, VfxAnimatedVec3Data& outValue);
bool ReadAnimatedColor(const nlohmann::json& node, VfxAnimatedColorData& outValue);
bool ReadTransform(const nlohmann::json& node, VfxTransformData& outValue);
bool ReadVfxSourceEmitter(const nlohmann::json& node, uint32_t sourceVersion, VfxEmitterData& outEmitter);
bool ReadVfxSourceDocument(const nlohmann::json& root, VfxAssetData& outAsset, std::string& outError);
nlohmann::json WriteVfxSourceDocument(const VfxAssetData& asset);
bool ValidateVfxSourceAsset(const VfxAssetData& asset, std::string& outError);

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL
