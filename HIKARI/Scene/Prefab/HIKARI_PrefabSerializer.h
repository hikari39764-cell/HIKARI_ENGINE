#pragma once

#include <filesystem>
#include <string>

namespace HIKARI {

struct PrefabDocument;

class PrefabSerializer {
public:
    bool LoadFromFile(const std::filesystem::path& path, PrefabDocument& outDocument, std::string& outError) const;
    bool SaveToFile(const std::filesystem::path& path, const PrefabDocument& document, std::string& outError) const;
};

} // namespace HIKARI
