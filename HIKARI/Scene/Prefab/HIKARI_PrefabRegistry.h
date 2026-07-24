#pragma once

#include <string>
#include <vector>

namespace HIKARI {

    class PrefabSerializer;
    struct PrefabDocument;

    class PrefabRegistry {
    public:
        explicit PrefabRegistry(std::string rootPath = "Data/prefabs");

        bool Save(const std::string& prefabId, const PrefabDocument& document, const PrefabSerializer& serializer) const;
        bool Load(const std::string& prefabId, PrefabDocument& outDocument, const PrefabSerializer& serializer) const;
        std::vector<std::string> ListPrefabIds() const;

    private:
        std::string BuildPath(const std::string& prefabId) const;

    private:
        std::string rootPath_{};
    };

} // namespace HIKARI
