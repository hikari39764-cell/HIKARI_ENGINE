#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#include <algorithm>
#include <unordered_map>

#include <json.hpp>
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"
#include "Vfx/Serialization/HIKARI_VfxProfileJson.h"

namespace HIKARI {

namespace {
    std::unordered_map<std::string, MaterialFxProfile> gProfileCache;
    MaterialFxProfileCacheStats gProfileCacheStats{};

    MaterialFxRenderPhase ParseRenderPhase(const std::string& value) {
        if (value == "DepthAware" || value == "SceneDepth") {
            return MaterialFxRenderPhase::DepthAware;
        }
        return MaterialFxRenderPhase::Opaque;
    }

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
        outProfile.composite = VFX::SERIALIZATION::ParseCompositeMode(
            node.value("composite", nlohmann::json{}),
            outProfile.composite);
        outProfile.renderPhase = ParseRenderPhase(node.value("renderPhase", std::string{ "Opaque" }));

        const nlohmann::json* paramsNode = nullptr;
        if (node.contains("params") && node["params"].is_array()) {
            paramsNode = &node["params"];
        } else if (node.contains("parameters") && node["parameters"].is_array()) {
            paramsNode = &node["parameters"];
        }
        VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(
            paramsNode != nullptr ? *paramsNode : nlohmann::json{},
            outProfile.params);
        VFX::ResetParameterValuesFromDefaults(
            outProfile.params,
            outProfile.values.data(),
            outProfile.values.size());

        return true;
    }
}

bool MaterialFxProfile::LoadFromJson(const std::string& path) {
    if (id.empty()) {
        return false;
    }

    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(
            std::filesystem::path(path),
            root) ||
        !root.is_object()) {
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
    if (!loaded.LoadFromJson("Data/material_fx_profiles.json")) {
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

MaterialFxProfileCacheStats MaterialFxProfile::GetCacheStats() {
    return gProfileCacheStats;
}

void MaterialFxProfile::CopyValuesTo(DirectX::XMFLOAT4(&dst)[VFX::kMaterialFxUserCount]) const {
    const size_t count = std::min(values.size(), std::size(dst));
    for (size_t i = 0; i < count; ++i) {
        dst[i] = values[i];
    }
}

} // namespace HIKARI
