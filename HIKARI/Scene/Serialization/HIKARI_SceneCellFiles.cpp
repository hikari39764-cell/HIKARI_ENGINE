#include "Scene/Serialization/HIKARI_SceneCellFiles.h"

#include <cstdio>
#include <unordered_map>

#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI {
namespace {
constexpr std::string_view kCellFormat = "HIKARI.SceneCell";
constexpr uint32_t kCellVersion = 1u;
constexpr std::string_view kObjectsContent = "objects";

std::string ChecksumText(uint64_t checksum) {
    char text[17]{};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(checksum));
    return text;
}
} // namespace

std::filesystem::path MakeSceneCellObjectsPath(const std::filesystem::path& scenePath, WorldCellCoord cell) {
    return ASSETS::SEMANTICS::MakeSceneCellDirectoryPath(scenePath) /
           TEXT::PathFromUtf8(WorldCellFileStem(cell) + std::string(kSceneCellObjectsSuffix));
}

SceneCellAssignment AssignSceneObjectsToCells(const SceneDocument& document) {
    std::unordered_map<uint64_t, size_t> indexById{};
    indexById.reserve(document.objects.size());
    for (size_t index = 0u; index < document.objects.size(); ++index) {
        indexById.emplace(document.objects[index].id.value, index);
    }
    // 親をたどって根を探す。親が見つからない物体は自分が根。循環しても物体数で打ち切る。
    const auto rootOf = [&](const SceneObjectData& object) -> const SceneObjectData& {
        const SceneObjectData* current = &object;
        for (size_t step = 0u; step < document.objects.size() && current->parent.has_value(); ++step) {
            const auto parent = indexById.find(current->parent->value);
            if (parent == indexById.end()) {
                break;
            }
            current = &document.objects[parent->second];
        }
        return *current;
    };

    SceneCellAssignment assignment{};
    const uint32_t cellSize = document.partition.cellSizeMeters;
    for (const SceneObjectData& object : document.objects) {
        const SceneObjectData& root = rootOf(object);
        if (!document.partition.enabled || root.alwaysLoaded) {
            assignment.mainObjects.push_back(&object);
            continue;
        }
        assignment.cells[WorldCellAt(root.transform.position.x, root.transform.position.z, cellSize)].push_back(
            &object);
    }
    return assignment;
}

uint64_t ComputeSceneCellChecksum(const nlohmann::json& objects) { return HASH::Fnv1aString(objects.dump()); }

nlohmann::json MakeSceneCellObjectsFile(WorldCellCoord cell, uint32_t cellSizeMeters, nlohmann::json objects,
                                        uint64_t checksum) {
    nlohmann::json file = nlohmann::json::object();
    file["format"] = kCellFormat;
    file["version"] = kCellVersion;
    file["content"] = kObjectsContent;
    file["cellSize"] = cellSizeMeters;
    file["cell"] = nlohmann::json::array({cell.x, cell.z});
    file["checksum"] = ChecksumText(checksum);
    file["objects"] = std::move(objects);
    return file;
}

bool ReadSceneCellObjectsFile(const std::filesystem::path& path, WorldCellCoord cell, uint32_t cellSizeMeters,
                              nlohmann::json& outObjects, uint64_t& outChecksum, std::string& outError) {
    nlohmann::json file{};
    std::string readError{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, file, &readError) || !file.is_object()) {
        outError = readError.empty() ? "not a JSON object" : readError;
        return false;
    }
    const auto text = [&](const char* key) {
        const auto found = file.find(key);
        return found != file.end() && found->is_string() ? found->get<std::string>() : std::string{};
    };
    if (text("format") != kCellFormat || text("content") != kObjectsContent) {
        outError = "not a scene cell objects file";
        return false;
    }
    if (file.value("version", 0u) != kCellVersion) {
        outError = "unsupported cell version " + std::to_string(file.value("version", 0u));
        return false;
    }
    if (file.value("cellSize", 0u) != cellSizeMeters) {
        outError = "cell size " + std::to_string(file.value("cellSize", 0u)) + " does not match the scene (" +
                   std::to_string(cellSizeMeters) + ")";
        return false;
    }
    const auto coord = file.find("cell");
    if (coord == file.end() || !coord->is_array() || coord->size() != 2u || !(*coord)[0].is_number_integer() ||
        !(*coord)[1].is_number_integer() || (*coord)[0].get<int64_t>() != cell.x ||
        (*coord)[1].get<int64_t>() != cell.z) {
        outError = "cell coordinate does not match the file name";
        return false;
    }
    const auto objects = file.find("objects");
    if (objects == file.end() || !objects->is_array()) {
        outError = "objects array is missing";
        return false;
    }
    const uint64_t checksum = ComputeSceneCellChecksum(*objects);
    if (text("checksum") != ChecksumText(checksum)) {
        outError = "checksum mismatch (file is damaged or was edited by hand)";
        return false;
    }
    outObjects = std::move(*objects);
    outChecksum = checksum;
    return true;
}

} // namespace HIKARI
