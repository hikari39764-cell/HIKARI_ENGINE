#include "HIKARI_AssetImporterRegistry.h"

#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    void AssetImporterRegistry::Register(std::unique_ptr<IAssetImporter> importer) {
        if (!importer || importer->GetSemantics().importerId.empty()) {
            HIKARI_LOG_WARN("[AssetImporter] Skip invalid importer registration.");
            return;
        }

        const std::string_view importerId =
            importer->GetSemantics().importerId;
        if (FindById(importerId) != nullptr) {
            HIKARI_LOG_WARN(
                std::string("[AssetImporter] Duplicate importer ignored: ") +
                std::string(importerId));
            return;
        }

        importers_.push_back(std::move(importer));
    }

    const IAssetImporter* AssetImporterRegistry::FindById(std::string_view importerId) const {
        for (const auto& importer : importers_) {
            if (importer &&
                importerId == importer->GetSemantics().importerId) {
                return importer.get();
            }
        }
        return nullptr;
    }

    IAssetImporter* AssetImporterRegistry::FindById(std::string_view importerId) {
        for (auto& importer : importers_) {
            if (importer &&
                importerId == importer->GetSemantics().importerId) {
                return importer.get();
            }
        }
        return nullptr;
    }

    const IAssetImporter* AssetImporterRegistry::FindForSource(const std::filesystem::path& sourcePath) const {
        const ASSETS::SEMANTICS::AssetImporterSemantics* semantics =
            ASSETS::SEMANTICS::ResolveAssetSourceSemantics(sourcePath);
        return semantics != nullptr
            ? FindById(semantics->importerId)
            : nullptr;
    }

    IAssetImporter* AssetImporterRegistry::FindForSource(const std::filesystem::path& sourcePath) {
        const ASSETS::SEMANTICS::AssetImporterSemantics* semantics =
            ASSETS::SEMANTICS::ResolveAssetSourceSemantics(sourcePath);
        return semantics != nullptr
            ? FindById(semantics->importerId)
            : nullptr;
    }

} // namespace HIKARI
