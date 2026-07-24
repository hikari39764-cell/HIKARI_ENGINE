#include "Assets/Importers/Policy/HIKARI_MaterialImportPolicy.h"

#include <json.hpp>

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

    MaterialImportPolicy ResolveMaterialImportPolicy(
        std::string_view importSettingsJson) {

        nlohmann::json settings = nlohmann::json::parse(
            importSettingsJson,
            nullptr,
            false);
        if (!settings.is_object()) {
            settings = nlohmann::json::object();
        }

        MaterialImportPolicy policy{};
        policy.cookMaterial = settings.value("cookMaterial", true);
        policy.outputFormat = SEMANTICS::ParseCookedAssetFormat(
            settings.value(
                "outputFormat",
                std::string(SEMANTICS::ToString(
                    CookedAssetFormat::HMAT))));
        return policy;
    }

    std::string MakeDefaultMaterialImportSettingsJson() {
        return nlohmann::json{
            { "shaderModel", "PBR" },
            { "sourceFormat", ".material.json" },
            { "outputFormat", std::string(SEMANTICS::ToString(
                CookedAssetFormat::HMAT)) },
            { "cookMaterial", true },
        }.dump(2);
    }

} // namespace HIKARI::ASSETS::IMPORT_POLICY
