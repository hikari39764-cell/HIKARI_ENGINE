#include "HIKARI_PrefabRegistry.h"

#include <filesystem>
#include <utility>

#include "HIKARI_PrefabDocument.h"
#include "HIKARI_PrefabSerializer.h"

namespace HIKARI {

    PrefabRegistry::PrefabRegistry(std::string rootPath)
        : rootPath_(std::move(rootPath)) {
    }

    bool PrefabRegistry::Save(const std::string& prefabId, const PrefabDocument& document, const PrefabSerializer& serializer) const {
        if (prefabId.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(rootPath_, ec);
        if (ec) {
            return false;
        }

        return serializer.SaveToFile(BuildPath(prefabId), document);
    }

    bool PrefabRegistry::Load(const std::string& prefabId, PrefabDocument& outDocument, const PrefabSerializer& serializer) const {
        if (prefabId.empty()) {
            return false;
        }
        return serializer.LoadFromFile(BuildPath(prefabId), outDocument);
    }

    std::vector<std::string> PrefabRegistry::ListPrefabIds() const {
        std::vector<std::string> ids{};

        std::error_code ec{};
        if (!std::filesystem::exists(rootPath_, ec) || ec) {
            return ids;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(rootPath_, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path path = entry.path();
            if (path.extension() != ".json") {
                continue;
            }
            ids.push_back(path.stem().string());
        }

        return ids;
    }

    std::string PrefabRegistry::BuildPath(const std::string& prefabId) const {
        return rootPath_ + "/" + prefabId + ".json";
    }

} // namespace HIKARI
