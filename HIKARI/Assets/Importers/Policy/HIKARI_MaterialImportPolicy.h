#pragma once

#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

    struct MaterialImportPolicy {
        bool cookMaterial = true;
        CookedAssetFormat outputFormat = CookedAssetFormat::HMAT;

        bool ShouldCookHmat() const noexcept {
            return cookMaterial &&
                outputFormat == CookedAssetFormat::HMAT;
        }
    };

    MaterialImportPolicy ResolveMaterialImportPolicy(
        std::string_view importSettingsJson);
    std::string MakeDefaultMaterialImportSettingsJson();

} // namespace HIKARI::ASSETS::IMPORT_POLICY
