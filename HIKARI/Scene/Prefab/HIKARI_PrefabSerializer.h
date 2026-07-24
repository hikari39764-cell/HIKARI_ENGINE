#pragma once

#include <string>

namespace HIKARI {

    struct PrefabDocument;

    class PrefabSerializer {
    public:
        bool LoadFromFile(const std::string& path, PrefabDocument& outDocument) const;
        bool SaveToFile(const std::string& path, const PrefabDocument& document) const;
    };

} // namespace HIKARI
