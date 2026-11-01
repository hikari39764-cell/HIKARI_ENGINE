#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#include <algorithm>
#include <unordered_map>

#include <json.hpp>
#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"

namespace HIKARI {

namespace {
std::unordered_map<std::string, MaterialFxProfile> gProfileCache;
MaterialFxProfileCacheStats gProfileCacheStats{};

bool ParseProfileNode(const nlohmann::json& node, const std::string& targetId, MaterialFxProfile& outProfile) {
    if (!node.is_object()) {
        return false;
    }
    if (node.value("id", std::string{}) != targetId) {
        return false;
    }

    outProfile = MaterialFxProfile{};
    outProfile.id = node.value("id", targetId);
    outProfile.displayName = node.value("displayName", outProfile.displayName);
    outProfile.shaderProfileId = node.value("shaderProfileId", outProfile.shaderProfileId);
    outProfile.vertexShaderId = node.value("vertexShaderId", outProfile.vertexShaderId);
    outProfile.pixelShaderId = node.value("pixelShaderId", outProfile.pixelShaderId);
    outProfile.featureBits = node.value("featureBits", outProfile.featureBits);
    outProfile.depthTest = node.value("depthTest", outProfile.depthTest);
    outProfile.depthWrite = node.value("depthWrite", outProfile.depthWrite);
    outProfile.doubleSided = node.value("doubleSided", outProfile.doubleSided);
    outProfile.composite =
        JSONREAD::EnumOr(node.value("composite", nlohmann::json{}), VFX::kCompositeModeNames, outProfile.composite);
    outProfile.renderPhase = JSONREAD::EnumOr(node.value("renderPhase", nlohmann::json{}), kMaterialFxRenderPhaseNames,
                                              MaterialFxRenderPhase::Opaque);

    std::string parameterError;
    if (!VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(node.value("params", nlohmann::json::array()),
                                                              VFX::kMaterialFxUserCount, outProfile.params,
                                                              parameterError)) {
        return false;
    }
    VFX::ResetParameterValuesFromDefaults(outProfile.params, outProfile.values.data(), outProfile.values.size());

    return true;
}
} // namespace

bool MaterialFxProfile::LoadFromJson(const std::string& path) {
    if (id.empty()) {
        return false;
    }

    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(std::filesystem::path(path), root) || !root.is_object()) {
        return false;
    }

    if (root.contains("profiles") && root["profiles"].is_array()) {
        for (const auto& profileNode : root["profiles"]) {
            if (ParseProfileNode(profileNode, id, *this)) {
                return true;
            }
        }
    }

    return ParseProfileNode(root, id, *this);
}

bool MaterialFxProfile::LoadById(const std::string& profileId, MaterialFxProfile& out) {
    if (profileId.empty()) {
        return false;
    }

    const auto found = gProfileCache.find(profileId);
    if (found != gProfileCache.end()) {
        ++gProfileCacheStats.hitCount;
        out = found->second;
        return true;
    }

    ++gProfileCacheStats.missCount;

    MaterialFxProfile loaded{};
    loaded.id = profileId;
    if (profileId == "HIKARI_Dissolve") {
        // 標準シェーダーの溶解機能を全プロジェクトで利用できるようにする。
        loaded.displayName = "ディゾルブ";
        loaded.shaderProfileId = "StaticFx";
        loaded.composite = VFX::CompositeMode::Replace;
        const auto parameter = [&](const char* key, const char* label, uint8_t slot, uint8_t channel, float value,
                                   float maximum) {
            VFX::ParamDesc p{};
            p.key = key;
            p.label = label;
            p.ref = {slot, channel};
            p.defaultValues[0] = value;
            p.maxValues[0] = maximum;
            loaded.params.push_back(std::move(p));
        };
        parameter("amount", "消失量", 0, 2, 0.0f, 1.0f);
        parameter("edgeWidth", "境界の幅", 0, 3, 0.06f, 0.5f);
        parameter("rimStrength", "輪郭の発光", 0, 0, 0.0f, 8.0f);
        parameter("rimPower", "輪郭の鋭さ", 0, 1, 3.0f, 16.0f);
        parameter("pulseSpeed", "点滅速度", 1, 0, 8.0f, 32.0f);
        parameter("edgeBoost", "境界の発光", 1, 1, 2.8f, 16.0f);
        parameter("noiseScale", "粒の細かさ", 1, 2, 1.5f, 32.0f);
        VFX::ResetParameterValuesFromDefaults(loaded.params, loaded.values.data(), loaded.values.size());
    } else if (!loaded.LoadFromJson("Data/material_fx_profiles.json")) {
        ++gProfileCacheStats.failCount;
        return false;
    }

    const auto inserted = gProfileCache.emplace(profileId, std::move(loaded));
    out = inserted.first->second;
    return true;
}

void MaterialFxProfile::ClearCache() {
    gProfileCache.clear();
    gProfileCacheStats = {};
}

MaterialFxProfileCacheStats MaterialFxProfile::GetCacheStats() { return gProfileCacheStats; }

void MaterialFxProfile::CopyValuesTo(DirectX::XMFLOAT4 (&dst)[VFX::kMaterialFxUserCount]) const {
    const size_t count = std::min(values.size(), std::size(dst));
    for (size_t i = 0; i < count; ++i) {
        dst[i] = values[i];
    }
}

} // namespace HIKARI
