#include "Scene/Serialization/HIKARI_WorldInstanceFile.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#include <json.hpp>

#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {
namespace {

constexpr uint32_t kMagic = 0x53494B48u; // "HKIS"
constexpr uint32_t kFormatVersion = 1u;
constexpr uint32_t kSetsFileVersion = 1u;

struct CellHeader {
    uint32_t magic = kMagic;
    uint32_t version = kFormatVersion;
    int32_t cellX = 0;
    int32_t cellZ = 0;
    uint32_t setCount = 0u;
    uint32_t reserved = 0u;
    uint64_t checksum = 0u;
};
static_assert(sizeof(CellHeader) == 32u);

struct CellDirectoryEntry {
    uint64_t setId = 0u;
    uint64_t count = 0u;
};
static_assert(sizeof(CellDirectoryEntry) == 16u);

uint64_t Fnv1a(const void* data, size_t size, uint64_t hash = 14695981039346656037ull) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0u; index < size; ++index) {
        hash = (hash ^ bytes[index]) * 1099511628211ull;
    }
    return hash;
}

bool ReplaceFile(const std::filesystem::path& temporary, const std::filesystem::path& path, std::string& outError) {
    std::error_code ec{};
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        outError = "Could not replace " + path.generic_string() + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

// 終端の 0 まで含めて納まる固定長の文字列へ写す。
template <size_t Capacity>
bool CopyFixedText(const std::string& text, char (&out)[Capacity]) noexcept {
    if (text.size() >= Capacity) {
        return false;
    }
    std::memset(out, 0, Capacity);
    std::memcpy(out, text.data(), text.size());
    return true;
}

} // namespace

std::filesystem::path MakeWorldInstanceCellPath(const std::filesystem::path& cellDirectory, WorldCellCoord cell) {
    return cellDirectory / (WorldCellFileStem(cell) + std::string(kWorldInstanceCellSuffix));
}

bool WriteWorldInstanceCellFile(const std::filesystem::path& path, WorldCellCoord cell,
                                std::span<const WorldInstanceCellPart> parts, std::string& outError) {
    CellHeader header{};
    header.cellX = cell.x;
    header.cellZ = cell.z;
    header.setCount = static_cast<uint32_t>(parts.size());
    std::vector<CellDirectoryEntry> directory{};
    directory.reserve(parts.size());
    for (const WorldInstanceCellPart& part : parts) {
        directory.push_back({part.setId, part.instances.size()});
    }
    uint64_t checksum = Fnv1a(directory.data(), directory.size() * sizeof(CellDirectoryEntry));
    for (const WorldInstanceCellPart& part : parts) {
        checksum = Fnv1a(part.instances.data(), part.instances.size_bytes(), checksum);
    }
    header.checksum = checksum;

    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(directory.data()),
                   static_cast<std::streamsize>(directory.size() * sizeof(CellDirectoryEntry)));
        for (const WorldInstanceCellPart& part : parts) {
            file.write(reinterpret_cast<const char*>(part.instances.data()),
                       static_cast<std::streamsize>(part.instances.size_bytes()));
        }
        if (!file) {
            outError = "Could not write " + temporary.generic_string();
            return false;
        }
    }
    return ReplaceFile(temporary, path, outError);
}

bool ReadWorldInstanceCellFile(const std::filesystem::path& path, WorldCellCoord cell,
                               std::vector<WorldInstanceCellRead>& outParts, std::string& outError) {
    outParts.clear();
    std::ifstream file(path, std::ios::binary);
    CellHeader header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        outError = path.generic_string() + ": the file is too short";
        return false;
    }
    if (header.magic != kMagic || header.version != kFormatVersion || header.reserved != 0u) {
        outError = path.generic_string() + ": unsupported instance cell file";
        return false;
    }
    if (header.cellX != cell.x || header.cellZ != cell.z) {
        outError = path.generic_string() + ": the file belongs to another cell";
        return false;
    }
    std::error_code ec{};
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    const uint64_t directoryBytes = static_cast<uint64_t>(header.setCount) * sizeof(CellDirectoryEntry);
    if (ec || fileSize < sizeof(CellHeader) + directoryBytes) {
        outError = path.generic_string() + ": the directory is truncated";
        return false;
    }
    std::vector<CellDirectoryEntry> directory(header.setCount);
    if (!file.read(reinterpret_cast<char*>(directory.data()), static_cast<std::streamsize>(directoryBytes))) {
        outError = path.generic_string() + ": the directory could not be read";
        return false;
    }
    uint64_t instanceTotal = 0u;
    for (const CellDirectoryEntry& entry : directory) {
        instanceTotal += entry.count;
    }
    if (fileSize != sizeof(CellHeader) + directoryBytes + instanceTotal * sizeof(GameProjectWorldInstance)) {
        outError = path.generic_string() + ": the file size does not match its directory";
        return false;
    }
    uint64_t checksum = Fnv1a(directory.data(), directoryBytes);
    outParts.resize(directory.size());
    for (size_t index = 0u; index < directory.size(); ++index) {
        WorldInstanceCellRead& part = outParts[index];
        part.setId = directory[index].setId;
        part.instances.resize(directory[index].count);
        const size_t bytes = part.instances.size() * sizeof(GameProjectWorldInstance);
        if (!file.read(reinterpret_cast<char*>(part.instances.data()), static_cast<std::streamsize>(bytes))) {
            outError = path.generic_string() + ": the instances could not be read";
            outParts.clear();
            return false;
        }
        checksum = Fnv1a(part.instances.data(), bytes, checksum);
    }
    if (checksum != header.checksum) {
        outError = path.generic_string() + ": the checksum does not match";
        outParts.clear();
        return false;
    }
    return true;
}

bool ParseWorldInstanceCellFileName(std::string_view fileName, WorldCellCoord& outCell) noexcept {
    if (!fileName.ends_with(kWorldInstanceCellSuffix)) {
        return false;
    }
    const std::string stem(fileName.substr(0u, fileName.size() - kWorldInstanceCellSuffix.size()));
    int x = 0;
    int z = 0;
    char tail = 0;
    if (std::sscanf(stem.c_str(), "x%d_z%d%c", &x, &z, &tail) != 2) {
        return false;
    }
    outCell = {x, z};
    return true;
}

bool WriteWorldInstanceSetsFile(const std::filesystem::path& path,
                                std::span<const GameProjectWorldInstanceSetDesc> sets, std::string& outError) {
    nlohmann::json list = nlohmann::json::array();
    for (const GameProjectWorldInstanceSetDesc& set : sets) {
        list.push_back({{"name", set.name},
                        {"geometry", static_cast<uint32_t>(set.geometry)},
                        {"flags", set.flags},
                        {"modelAssetGuid", set.modelAssetGuid},
                        {"meshIndex", set.meshIndex},
                        {"primitiveIndex", set.primitiveIndex},
                        {"materialIndex", set.materialIndex},
                        {"tintVariation", set.tintVariation},
                        {"windStrength", set.windStrength},
                        {"minScreenHeight", set.minScreenHeight},
                        {"farModelAssetGuid", set.farModelAssetGuid},
                        {"farDistance", set.farDistance}});
    }
    const nlohmann::json document = {{"version", kSetsFileVersion}, {"sets", std::move(list)}};
    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << document.dump(2);
        if (!file) {
            outError = "Could not write " + temporary.generic_string();
            return false;
        }
    }
    return ReplaceFile(temporary, path, outError);
}

bool ReadWorldInstanceSetsFile(const std::filesystem::path& path, std::vector<GameProjectWorldInstanceSetDesc>& outSets,
                               std::string& outError) {
    outSets.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        outError = "Could not open " + path.generic_string();
        return false;
    }
    const nlohmann::json document = nlohmann::json::parse(file, nullptr, false);
    if (document.is_discarded() || !document.is_object() || document.value("version", 0u) != kSetsFileVersion ||
        !document.contains("sets") || !document.at("sets").is_array()) {
        outError = path.generic_string() + ": unsupported instance set file";
        return false;
    }
    try {
        for (const nlohmann::json& entry : document.at("sets")) {
            GameProjectWorldInstanceSetDesc set{};
            if (!CopyFixedText(entry.at("name").get<std::string>(), set.name) ||
                !CopyFixedText(entry.at("modelAssetGuid").get<std::string>(), set.modelAssetGuid) ||
                !CopyFixedText(entry.value("farModelAssetGuid", std::string{}), set.farModelAssetGuid)) {
                outError = path.generic_string() + ": a name is too long";
                outSets.clear();
                return false;
            }
            set.geometry = static_cast<GameProjectWorldMassRenderGeometry>(entry.at("geometry").get<uint32_t>());
            set.flags = entry.at("flags").get<uint32_t>();
            set.meshIndex = entry.at("meshIndex").get<uint32_t>();
            set.primitiveIndex = entry.at("primitiveIndex").get<uint32_t>();
            set.materialIndex = entry.at("materialIndex").get<uint32_t>();
            set.tintVariation = entry.at("tintVariation").get<float>();
            set.windStrength = entry.at("windStrength").get<float>();
            set.minScreenHeight = entry.at("minScreenHeight").get<float>();
            set.farDistance = entry.value("farDistance", 0.0f);
            outSets.push_back(set);
        }
    } catch (const nlohmann::json::exception& exception) {
        outError = path.generic_string() + ": " + exception.what();
        outSets.clear();
        return false;
    }
    return true;
}

} // namespace HIKARI
