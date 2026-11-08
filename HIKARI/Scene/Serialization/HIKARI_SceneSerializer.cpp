#include "Scene/Serialization/HIKARI_SceneSerializer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneCellFiles.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI::SCENE::SERIALIZATION {

void SerializeSceneSystemsJson(const std::vector<SceneSystemData>& systems, nlohmann::json& out) {
    out = nlohmann::json::array();
    for (const SceneSystemData& system : systems) {
        out.push_back({{"systemId", system.systemId},
                       {"enabled", system.enabled},
                       {"executionOrder", system.executionOrder},
                       {"settings", system.settings.is_object() ? system.settings : nlohmann::json::object()}});
    }
}

void DeserializeSceneSystemsJson(const nlohmann::json& node, std::vector<SceneSystemData>& outSystems) {
    outSystems.clear();
    if (!node.is_array())
        return;
    for (const nlohmann::json& systemNode : node) {
        if (!systemNode.is_object())
            continue;
        SceneSystemData system{};
        system.systemId = systemNode.value("systemId", std::string{});
        if (system.systemId.empty())
            continue;
        system.enabled = systemNode.value("enabled", true);
        system.executionOrder = systemNode.value("executionOrder", 0);
        system.settings = systemNode.value("settings", nlohmann::json::object());
        if (!system.settings.is_object()) {
            system.settings = nlohmann::json::object();
        }
        outSystems.push_back(std::move(system));
    }
}

} // namespace HIKARI::SCENE::SERIALIZATION

namespace HIKARI {

namespace SceneJson = SCENE::SERIALIZATION;

namespace {

void DeserializeCamera(const nlohmann::json& node, SceneCameraSettings& outCamera) {
    if (!node.is_object() || !node.contains("defaultCameraObjectId") ||
        !node["defaultCameraObjectId"].is_number_unsigned()) {
        return;
    }

    const uint64_t objectId = node["defaultCameraObjectId"].get<uint64_t>();
    if (objectId != 0u) {
        outCamera.defaultCameraObjectId = SceneObjectId{objectId};
    }
}

void SerializeCamera(const SceneCameraSettings& camera, nlohmann::json& out) {
    out["defaultCameraObjectId"] = camera.defaultCameraObjectId.has_value()
                                       ? nlohmann::json(camera.defaultCameraObjectId->value)
                                       : nlohmann::json(nullptr);
}

nlohmann::json SerializeObjects(std::span<const SceneObjectData* const> objects) {
    nlohmann::json out = nlohmann::json::array();
    for (const SceneObjectData* object : objects) {
        nlohmann::json objectNode{};
        SceneJson::SerializeSceneObjectJson(*object, objectNode, SceneJson::SceneObjectJsonScope::SceneDocument);
        out.push_back(std::move(objectNode));
    }
    return out;
}

// 主ファイルの "partition"：格子の大きさと、物体のある格子の一覧。無ければ分区しない場景。
bool ReadPartition(const nlohmann::json& root, ScenePartitionSettings& outSettings,
                   std::vector<WorldCellCoord>& outCells, std::string& outError) {
    const auto node = root.find("partition");
    if (node == root.end()) {
        return true;
    }
    if (!node->is_object()) {
        outError = "partition is not an object";
        return false;
    }
    const uint32_t cellSize = node->value("cellSize", 0u);
    if (!IsWorldCellSize(cellSize)) {
        outError = "unsupported cell size " + std::to_string(cellSize);
        return false;
    }
    outSettings = {true, cellSize};
    for (const nlohmann::json& cell : node->value("cells", nlohmann::json::array())) {
        if (!cell.is_array() || cell.size() != 2u || !cell[0].is_number_integer() || !cell[1].is_number_integer()) {
            outError = "cell entry is not [x, z]";
            return false;
        }
        outCells.push_back({cell[0].get<int32_t>(), cell[1].get<int32_t>()});
    }
    return true;
}

// 一つの格子の物体を読む。id が欠けている・重なる、親が格子の外にある時は格子ごと読まない。
bool ReadCellObjects(const nlohmann::json& objects, std::unordered_set<uint64_t>& ids,
                     std::vector<SceneObjectData>& outObjects, std::string& outError) {
    std::vector<SceneObjectData> decoded{};
    std::unordered_set<uint64_t> cellIds{};
    for (const nlohmann::json& node : objects) {
        SceneObjectData object{};
        if (!SceneJson::DeserializeSceneObjectJson(node, object, SceneJson::SceneObjectJsonScope::SceneDocument) ||
            object.id.value == 0u || ids.contains(object.id.value) || !cellIds.insert(object.id.value).second) {
            outError = "object id is missing or duplicated";
            return false;
        }
        decoded.push_back(std::move(object));
    }
    for (const SceneObjectData& object : decoded) {
        if (object.parent.has_value() && !cellIds.contains(object.parent->value)) {
            outError = "object " + std::to_string(object.id.value) + " has a parent outside the cell";
            return false;
        }
    }
    ids.insert(cellIds.begin(), cellIds.end());
    outObjects.insert(outObjects.end(), std::make_move_iterator(decoded.begin()),
                      std::make_move_iterator(decoded.end()));
    return true;
}

// 一覧に無い格子ファイルを消し（物体が無くなった格子）、空になったフォルダも消す。消した数を返す。
size_t RemoveStaleCellFiles(const std::filesystem::path& scenePath, const std::set<WorldCellCoord>& keep) {
    const std::filesystem::path directory = ASSETS::SEMANTICS::MakeSceneCellDirectoryPath(scenePath);
    std::error_code ec{};
    if (!std::filesystem::is_directory(directory, ec)) {
        return 0u;
    }
    std::unordered_set<std::string> kept{};
    for (WorldCellCoord cell : keep) {
        kept.insert(WorldCellFileStem(cell) + std::string(kSceneCellObjectsSuffix));
    }
    std::vector<std::filesystem::path> stale{};
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        const std::string name = TEXT::PathToUtf8(entry.path().filename());
        if (entry.is_regular_file(ec) && name.ends_with(kSceneCellObjectsSuffix) && !kept.contains(name)) {
            stale.push_back(entry.path());
        }
    }
    size_t removed = 0u;
    for (const std::filesystem::path& file : stale) {
        if (std::filesystem::remove(file, ec)) {
            ++removed;
        } else {
            HIKARI_LOG_WARN("[SceneAsset] stale scene cell could not be removed: " + TEXT::PathToUtf8(file));
        }
    }
    if (std::filesystem::is_empty(directory, ec) && !ec) {
        std::filesystem::remove(directory, ec);
    }
    return removed;
}

} // namespace

bool SceneSerializer::LoadFromFile(const std::string& path, SceneDocument& outDocument,
                                   SceneCellFileState* cells) const {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(std::filesystem::path(path), root) || !root.is_object()) {
        return false;
    }

    try {
        SceneDocument decoded{};
        // 現行版だけを読む。旧版の scene は旧版エンジンで開いて保存し直す。
        decoded.version = root.value("version", 0u);
        if (decoded.version != kCurrentSceneDocumentVersion) {
            HIKARI_LOG_ERROR("[SceneAsset] unsupported scene version: " + std::to_string(decoded.version) +
                             " (supported: " + std::to_string(kCurrentSceneDocumentVersion) + ")");
            return false;
        }
        decoded.sceneName = root.value("sceneName", std::string("Untitled"));
        if (const auto folders = root.find("editorFolders"); folders != root.end() && folders->is_array())
            for (const auto& folder : *folders)
                if (folder.is_string())
                    decoded.editorFolders.push_back(folder.get<std::string>());

        ENVIRONMENT::ReadSceneEnvironmentBinding(root.value("environment", nlohmann::json::object()),
                                                 decoded.environment);
        DeserializeCamera(root.value("camera", nlohmann::json::object()), decoded.camera);
        DeserializeSceneCinematicsJson(root.value("cinematics", nlohmann::json::object()), decoded.cinematics);
        JSONFIELDS::Read(root.value("lightingBake", nlohmann::json::object()), decoded.lightingBake,
                         kSceneLightingBakeFields);
        decoded.lightingBake = JSONFIELDS::Sanitize(decoded.lightingBake, kSceneLightingBakeFields);
        SceneJson::DeserializeSceneSystemsJson(root.value("systems", nlohmann::json{}), decoded.systems);
        std::vector<WorldCellCoord> cellIndex{};
        std::string partitionError{};
        if (!ReadPartition(root, decoded.partition, cellIndex, partitionError)) {
            HIKARI_LOG_ERROR("[SceneAsset] unreadable scene partition: " + partitionError + " (" + path + ")");
            return false;
        }

        if (root.contains("objects") && root["objects"].is_array()) {
            for (const nlohmann::json& node : root["objects"]) {
                SceneObjectData object{};
                if (SceneJson::DeserializeSceneObjectJson(node, object,
                                                          SceneJson::SceneObjectJsonScope::SceneDocument)) {
                    decoded.objects.push_back(std::move(object));
                }
            }
        }

        SceneCellFileState loadedCells{};
        loadedCells.scenePath = std::filesystem::path(path).lexically_normal();
        if (decoded.partition.enabled) {
            loadedCells.cellSizeMeters = decoded.partition.cellSizeMeters;
            loadedCells.mainChecksum = HASH::Fnv1aString(root.dump(2));
            std::unordered_set<uint64_t> ids{};
            for (const SceneObjectData& object : decoded.objects) {
                ids.insert(object.id.value);
            }
            for (WorldCellCoord cell : cellIndex) {
                const std::filesystem::path cellPath = MakeSceneCellObjectsPath(loadedCells.scenePath, cell);
                nlohmann::json objects{};
                uint64_t checksum = 0u;
                std::string error{};
                bool read = false;
                try {
                    read = ReadSceneCellObjectsFile(cellPath, cell, decoded.partition.cellSizeMeters, objects, checksum,
                                                    error) &&
                           ReadCellObjects(objects, ids, decoded.objects, error);
                } catch (const std::exception& exception) {
                    error = exception.what();
                }
                if (!read) {
                    HIKARI_LOG_ERROR("[SceneAsset] scene cell skipped: " + TEXT::PathToUtf8(cellPath) + ": " + error);
                    loadedCells.unreadableCells.insert(cell);
                    continue;
                }
                loadedCells.cellChecksums[cell] = checksum;
            }
        }

        for (SceneObjectData& object : decoded.objects) {
            NormalizeSceneComponentIds(object);
        }
        outDocument = std::move(decoded);
        if (cells != nullptr) {
            *cells = std::move(loadedCells);
        }
        return true;
    } catch (const std::exception& exception) {
        // JSON の型違いも、現行形式でない内容（例：Sequence v5 以前の全体時間カメラトラック）も読込失敗として返す。
        HIKARI_LOG_ERROR(std::string("[SceneAsset] unreadable scene: ") + exception.what());
        return false;
    }
}

bool SceneSerializer::SaveToFile(const std::string& path, const SceneDocument& document, SceneCellFileState* cells,
                                 SceneSaveReport* report) const {
    SceneDocument persisted = document;
    persisted.version = kCurrentSceneDocumentVersion;
    for (SceneObjectData& object : persisted.objects) {
        NormalizeSceneComponentIds(object);
    }

    nlohmann::json root = nlohmann::json::object();
    root["version"] = persisted.version;
    root["sceneName"] = persisted.sceneName;
    if (!persisted.editorFolders.empty())
        root["editorFolders"] = persisted.editorFolders;

    root["environment"] = ENVIRONMENT::WriteSceneEnvironmentBinding(persisted.environment);
    SerializeCamera(persisted.camera, root["camera"]);
    SerializeSceneCinematicsJson(persisted.cinematics, root["cinematics"]);
    JSONFIELDS::Write(root["lightingBake"], persisted.lightingBake, kSceneLightingBakeFields);
    SceneJson::SerializeSceneSystemsJson(persisted.systems, root["systems"]);

    const std::filesystem::path scenePath = std::filesystem::path(path).lexically_normal();
    // 別の路径へ保存する時は検査和を使わず全部書く。読めなかった格子だけは一覧と保護を引き継ぎ、ファイルを生のまま写す。
    SceneCellFileState inherited{};
    if (cells != nullptr && cells->scenePath != scenePath) {
        inherited.cellSizeMeters = cells->cellSizeMeters;
        inherited.unreadableCells = cells->unreadableCells;
    }
    const SceneCellFileState& previous = cells != nullptr && cells->scenePath == scenePath ? *cells : inherited;
    SceneSaveReport written{};
    if (!persisted.partition.enabled) {
        if (!previous.unreadableCells.empty()) {
            HIKARI_LOG_ERROR("[SceneAsset] partitioning cannot be turned off while unreadable cells remain: " + path);
            return false;
        }
        root["objects"] = SerializeObjects(AssignSceneObjectsToCells(persisted).mainObjects);
        if (!SERIALIZATION::JSON::WriteJsonFile(scenePath, root)) {
            return false;
        }
        written.mainWritten = true;
        written.removedCellFiles = RemoveStaleCellFiles(scenePath, {});
        if (cells != nullptr) {
            *cells = SceneCellFileState{scenePath};
        }
        if (report != nullptr) {
            *report = std::move(written);
        }
        return true;
    }

    const uint32_t cellSize = persisted.partition.cellSizeMeters;
    if (!IsWorldCellSize(cellSize)) {
        HIKARI_LOG_ERROR("[SceneAsset] unsupported partition cell size " + std::to_string(cellSize) + ": " + path);
        return false;
    }
    const bool sameLayout = previous.cellSizeMeters == cellSize;
    if (!sameLayout && !previous.unreadableCells.empty()) {
        HIKARI_LOG_ERROR("[SceneAsset] the cell size cannot change while unreadable cells remain: " + path);
        return false;
    }
    const SceneCellAssignment assignment = AssignSceneObjectsToCells(persisted);
    std::set<WorldCellCoord> index{};
    for (const auto& [cell, objects] : assignment.cells) {
        if (sameLayout && previous.unreadableCells.contains(cell)) {
            HIKARI_LOG_ERROR("[SceneAsset] objects were moved into the unreadable cell " + WorldCellFileStem(cell) +
                             "; repair or remove that cell file first: " + path);
            return false;
        }
        index.insert(cell);
    }
    if (cells != nullptr && cells->scenePath != scenePath) {
        for (WorldCellCoord cell : inherited.unreadableCells) {
            const std::filesystem::path source = MakeSceneCellObjectsPath(cells->scenePath, cell);
            const std::filesystem::path destination = MakeSceneCellObjectsPath(scenePath, cell);
            std::error_code ec{};
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (std::filesystem::exists(source, ec) &&
                !std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing,
                                            ec)) {
                HIKARI_LOG_ERROR("[SceneAsset] could not copy the unreadable cell " + WorldCellFileStem(cell) +
                                 " to the new location: " + path);
                return false;
            }
        }
    }
    SceneCellFileState next{};
    next.scenePath = scenePath;
    next.cellSizeMeters = cellSize;
    if (sameLayout) {
        // 読めなかった格子は一覧に残し、次に開いた時にまた読みにいく。
        next.unreadableCells = previous.unreadableCells;
        index.insert(previous.unreadableCells.begin(), previous.unreadableCells.end());
    }
    nlohmann::json cellList = nlohmann::json::array();
    for (WorldCellCoord cell : index) {
        cellList.push_back(nlohmann::json::array({cell.x, cell.z}));
    }
    root["partition"] = {{"cellSize", cellSize}, {"cells", std::move(cellList)}};
    root["objects"] = SerializeObjects(assignment.mainObjects);

    // 格子を先に書き、主ファイルの一覧が指すファイルが必ずある順にする。
    for (const auto& [cell, objects] : assignment.cells) {
        nlohmann::json objectsJson = SerializeObjects(objects);
        const uint64_t checksum = ComputeSceneCellChecksum(objectsJson);
        next.cellChecksums[cell] = checksum;
        const std::filesystem::path cellPath = MakeSceneCellObjectsPath(scenePath, cell);
        const auto known = previous.cellChecksums.find(cell);
        std::error_code ec{};
        if (sameLayout && known != previous.cellChecksums.end() && known->second == checksum &&
            std::filesystem::is_regular_file(cellPath, ec)) {
            continue;
        }
        if (!SERIALIZATION::JSON::WriteJsonFile(
                cellPath, MakeSceneCellObjectsFile(cell, cellSize, std::move(objectsJson), checksum))) {
            HIKARI_LOG_ERROR("[SceneAsset] scene cell could not be written: " + TEXT::PathToUtf8(cellPath));
            return false;
        }
        written.writtenCells.push_back(cell);
    }
    next.mainChecksum = HASH::Fnv1aString(root.dump(2));
    std::error_code ec{};
    if (!sameLayout || previous.mainChecksum != next.mainChecksum || !std::filesystem::is_regular_file(scenePath, ec)) {
        if (!SERIALIZATION::JSON::WriteJsonFile(scenePath, root)) {
            return false;
        }
        written.mainWritten = true;
    }
    written.removedCellFiles = RemoveStaleCellFiles(scenePath, index);
    if (cells != nullptr) {
        *cells = std::move(next);
    }
    if (report != nullptr) {
        *report = std::move(written);
    }
    return true;
}

} // namespace HIKARI
