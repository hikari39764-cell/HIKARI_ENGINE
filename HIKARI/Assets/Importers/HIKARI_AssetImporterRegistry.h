#pragma once

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "HIKARI_AssetImporter.h"

namespace HIKARI {

    class AssetImporterRegistry {
    public:
        void Register(std::unique_ptr<IAssetImporter> importer);

        const IAssetImporter* FindById(std::string_view importerId) const;
        IAssetImporter* FindById(std::string_view importerId);

        const IAssetImporter* FindForSource(const std::filesystem::path& sourcePath) const;
        IAssetImporter* FindForSource(const std::filesystem::path& sourcePath);

    private:
        std::vector<std::unique_ptr<IAssetImporter>> importers_{};
    };

} // namespace HIKARI
