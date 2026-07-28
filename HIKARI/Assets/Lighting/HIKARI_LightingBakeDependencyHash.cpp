#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"

namespace HIKARI::ASSETS::LIGHTING {

    namespace JsonMath = SERIALIZATION::JSON::MATH;
    namespace SceneJson = SCENE::SERIALIZATION;

    namespace {

        class StableHashBuilder {
        public:
            void Add(std::string_view text) noexcept {
                for (const unsigned char value : text) {
                    hash_ ^= static_cast<uint64_t>(value);
                    hash_ *= 1099511628211ull;
                }
            }

            std::string Finish() const {
                std::ostringstream out{};
                out << std::hex << std::setfill('0')
                    << std::setw(16) << hash_;
                return out.str();
            }

        private:
            uint64_t hash_ = 14695981039346656037ull;
        };

        nlohmann::json BuildObjectNode(const SceneObjectData& object) {
            nlohmann::json node{
                { "id", object.id.value },
                { "enabled", object.enabled },
                { "position", JsonMath::ToJsonArray(object.transform.position) },
                { "rotation", JsonMath::ToJsonArray(object.transform.rotationEulerDeg) },
                { "scale", JsonMath::ToJsonArray(object.transform.scale) },
                { "components", nlohmann::json::array() }
            };
            for (const SceneComponentData& component : object.components) {
                node["components"].push_back({
                    { "type", component.type },
                    { "properties", component.properties }
                });
            }
            return node;
        }

        void AddFileStamp(
            StableHashBuilder& hash,
            const std::filesystem::path& path) {

            std::error_code ec{};
            hash.Add(path.lexically_normal().generic_string());
            if (!std::filesystem::exists(path, ec) || ec) {
                hash.Add("|missing");
                return;
            }
            if (std::filesystem::is_regular_file(path, ec) && !ec) {
                hash.Add("|");
                hash.Add(std::to_string(
                    std::filesystem::file_size(path, ec)));
                if (ec) {
                    ec.clear();
                }
            }
            const auto writeTime =
                std::filesystem::last_write_time(path, ec);
            if (!ec) {
                hash.Add("|");
                hash.Add(std::to_string(
                    writeTime.time_since_epoch().count()));
            }
        }

    } // namespace

    LightingBakeHashSnapshot BuildLightingBakeHashSnapshot(
        const SceneDocument& document,
        const AssetDatabase& assetDatabase) {

        LightingBakeHashSnapshot snapshot{};
        StableHashBuilder sceneHash{};
        nlohmann::json environment{};
        SceneJson::SerializeSceneEnvironmentJson(
            document.environment,
            environment);
        environment.erase("reflectionProbe");
        sceneHash.Add(environment.dump());

        for (const SceneObjectData& object : document.objects) {
            const std::string objectJson =
                BuildObjectNode(object).dump();
            sceneHash.Add(objectJson);
            StableHashBuilder objectHash{};
            objectHash.Add(objectJson);
            snapshot.objectAuthoringHashes.emplace(
                object.id.value,
                objectHash.Finish());
        }
        snapshot.sceneHash = sceneHash.Finish();

        StableHashBuilder dependencyHash{};
        dependencyHash.Add(snapshot.sceneHash);
        const AssetUsageSummary usage = AnalyzeAssetUsage(
            document,
            assetDatabase);
        std::vector<std::string> usedGuids(
            usage.usedGuids.begin(),
            usage.usedGuids.end());
        std::sort(usedGuids.begin(), usedGuids.end());
        for (const std::string& guid : usedGuids) {
            dependencyHash.Add(guid);
            const AssetRecord* record = assetDatabase.FindByGuid(
                AssetGuid{ guid });
            if (record == nullptr) {
                continue;
            }
            AddFileStamp(
                dependencyHash,
                assetDatabase.GetProjectRoot() / record->sourcePath);
            AddFileStamp(
                dependencyHash,
                assetDatabase.GetProjectRoot() / record->metaPath);
            AddFileStamp(
                dependencyHash,
                assetDatabase.GetProjectRoot() /
                    record->artifactManifestPath);
        }
        for (const AssetMissingReference& missing :
            usage.missingReferences) {
            snapshot.missingAssetIds.push_back(missing.assetId);
            dependencyHash.Add("|missing:");
            dependencyHash.Add(missing.assetId);
        }
        snapshot.dependencyHash = dependencyHash.Finish();
        return snapshot;
    }

} // namespace HIKARI::ASSETS::LIGHTING
