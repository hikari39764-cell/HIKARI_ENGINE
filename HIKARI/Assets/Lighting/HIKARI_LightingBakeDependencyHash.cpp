#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
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

        const SceneComponentData* FindComponent(
            const SceneObjectData& object,
            std::string_view type) {

            const auto found = std::find_if(
                object.components.begin(),
                object.components.end(),
                [type](const SceneComponentData& component) {
                    return component.type == type;
                });
            return found == object.components.end()
                ? nullptr
                : &*found;
        }

        bool IsLightingBakeModel(
            const SceneObjectData& object,
            const SceneComponentData*& outModel) {

            outModel = FindComponent(object, "ModelComponent");
            if (!object.enabled ||
                outModel == nullptr ||
                !outModel->properties.is_object()) {
                return false;
            }
            const nlohmann::json& properties =
                outModel->properties;
            return properties.value("visible", true) &&
                properties.value("includeInLightingBake", true);
        }

        nlohmann::json BuildTransformNode(
            const TransformData& transform) {

            return {
                { "position",
                    JsonMath::ToJsonArray(transform.position) },
                { "rotation",
                    JsonMath::ToJsonArray(
                        transform.rotationEulerDeg) },
                { "scale",
                    JsonMath::ToJsonArray(transform.scale) }
            };
        }

        const SceneObjectData* FindObject(
            const SceneDocument& document,
            SceneObjectId id) {

            const auto found = std::find_if(
                document.objects.begin(),
                document.objects.end(),
                [id](const SceneObjectData& object) {
                    return object.id == id;
                });
            return found == document.objects.end()
                ? nullptr
                : &*found;
        }

        nlohmann::json BuildTransformChain(
            const SceneDocument& document,
            const SceneObjectData& object) {

            nlohmann::json chain = nlohmann::json::array();
            const SceneObjectData* current = &object;
            std::unordered_set<uint64_t> visited{};
            while (current != nullptr &&
                visited.insert(current->id.value).second) {
                chain.push_back(
                    BuildTransformNode(current->transform));
                current = current->parent
                    ? FindObject(document, *current->parent)
                    : nullptr;
            }
            return chain;
        }

        nlohmann::json BuildCaptureModelProperties(
            const nlohmann::json& source) {

            nlohmann::json properties = source;
            properties.erase("showSkeletonDebug");
            properties.erase("skeletonDebugXRay");
            properties.erase("debugRenderMode");
            properties.erase("wireColor");
            properties.erase("maxWireLines");
            properties.erase("wirePerPrimitiveColor");
            properties.erase("postGroupMask");
            properties.erase("renderStatic");
            properties.erase("includeInLightingBake");
            return properties;
        }

        nlohmann::json BuildCaptureObjectNode(
            const SceneDocument& document,
            const SceneObjectData& object,
            const SceneComponentData& model) {

            nlohmann::json components =
                nlohmann::json::array();
            components.push_back({
                { "type", "ModelComponent" },
                { "properties",
                    BuildCaptureModelProperties(model.properties) }
            });
            for (std::string_view type : {
                    std::string_view("MaterialFxComponent"),
                    std::string_view("ProceduralMeshComponent") }) {
                if (const SceneComponentData* component =
                        FindComponent(object, type)) {
                    components.push_back({
                        { "type", component->type },
                        { "properties", component->properties }
                    });
                }
            }
            return {
                { "transformChain",
                    BuildTransformChain(document, object) },
                { "components", std::move(components) }
            };
        }

        nlohmann::json BuildCaptureEnvironmentNode(
            const SceneEnvironment& source) {

            nlohmann::json environment{};
            SceneJson::SerializeSceneEnvironmentJson(
                source,
                environment);
            environment.erase("reflectionProbe");
            environment.erase("directional");
            environment.erase("pointLights");
            environment.erase("spotLights");
            environment.erase("ambientOcclusion");
            environment.erase("surfaceLightmaps");
            environment.erase("screenSpaceReflections");
            environment.erase("bloom");
            environment.erase("toneMapping");
            environment.erase("post");
            environment.erase("showLightDebug");
            environment.erase("showPointLightMarkers");
            environment.erase("showSkyDebugInfo");
            if (environment.contains("directionalShadow")) {
                environment["directionalShadow"].erase(
                    "showDebugTexture");
                environment["directionalShadow"].erase(
                    "showDebugFrustum");
            }
            if (environment.contains("sky")) {
                environment["sky"].erase("showDebugTexture");
            }
            return environment;
        }

        SceneDocument BuildLightingCaptureUsageDocument(
            const SceneDocument& document) {

            SceneDocument capture{};
            capture.environment = document.environment;
            capture.environment.reflectionProbe = {};
            capture.objects.reserve(document.objects.size());
            for (const SceneObjectData& object : document.objects) {
                const SceneComponentData* model = nullptr;
                if (!IsLightingBakeModel(object, model)) {
                    continue;
                }
                SceneObjectData captureObject{};
                captureObject.id = object.id;
                captureObject.name = object.name;
                captureObject.components.push_back(*model);
                for (std::string_view type : {
                        std::string_view("MaterialFxComponent"),
                        std::string_view("ProceduralMeshComponent") }) {
                    if (const SceneComponentData* component =
                            FindComponent(object, type)) {
                        captureObject.components.push_back(
                            *component);
                    }
                }
                capture.objects.push_back(
                    std::move(captureObject));
            }
            return capture;
        }

        nlohmann::json BuildReflectionProbeCaptureNode(
            const LIGHTING_AUTHORING::
                ReflectionProbeAuthoring& probe) {

            return {
                { "kind", "ReflectionProbe" },
                { "bakePolicyVersion",
                    kReflectionProbeBakePolicyVersion },
                { "position",
                    JsonMath::ToJsonArray(probe.position) },
                { "captureRadius", probe.settings.radius },
                { "captureResolution",
                    probe.captureResolution },
                { "sourceCubemapAsset",
                    probe.settings.sourceCubemapAsset },
                { "bakeSet", probe.bakeSet },
                { "scenario", probe.scenario }
            };
        }

        nlohmann::json BuildIrradianceVolumeCaptureNode(
            const LIGHTING_AUTHORING::
                IrradianceVolumeAuthoring& volume) {

            return {
                { "kind", "IrradianceVolume" },
                { "origin",
                    JsonMath::ToJsonArray(
                        volume.settings.origin) },
                { "size",
                    JsonMath::ToJsonArray(
                        volume.settings.size) },
                { "countX", volume.settings.countX },
                { "countY", volume.settings.countY },
                { "countZ", volume.settings.countZ },
                { "captureResolution",
                    volume.settings.captureResolution },
                { "shOrder", volume.settings.shOrder },
                { "bakeSet", volume.bakeSet },
                { "scenario", volume.scenario }
            };
        }

        nlohmann::json BuildSurfaceLightmapCaptureNode(
            const SceneObjectData& object,
            const SceneComponentData& model) {

            return {
                { "kind", "SurfaceLightmap" },
                { "bakePolicyVersion",
                    kSurfaceLightmapBakePolicyVersion },
                { "objectId", object.id.value },
                { "uvSet", kSurfaceLightmapUvSet },
                { "outputResolution",
                    kSurfaceLightmapDefaultResolution },
                { "sampleCount",
                    kSurfaceLightmapDefaultSampleCount },
                { "bounceCount",
                    kSurfaceLightmapDefaultBounceCount },
                { "renderStatic",
                    model.properties.value(
                        "renderStatic",
                        false) },
                { "includeInLightingBake",
                    model.properties.value(
                        "includeInLightingBake",
                        true) }
            };
        }

        std::string HashJson(const nlohmann::json& node) {
            StableHashBuilder hash{};
            hash.Add(node.dump());
            return hash.Finish();
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

        void AddAssetRecordRevision(
            StableHashBuilder& hash,
            const AssetDatabase& assetDatabase,
            const AssetRecord& record) {

            hash.Add(record.guid.value);
            hash.Add("|meta:");
            hash.Add(std::to_string(record.meta.metaVersion));
            hash.Add("|");
            hash.Add(record.meta.importerId);
            hash.Add("|");
            hash.Add(std::to_string(
                record.meta.importerVersion));
            hash.Add("|");
            hash.Add(record.meta.sourcePath);
            hash.Add("|");
            hash.Add(record.meta.importSettingsJson);

            const AssetArtifactManifest& manifest =
                record.artifactManifest;
            hash.Add("|artifact:");
            hash.Add(std::to_string(
                manifest.manifestVersion));
            hash.Add("|");
            hash.Add(manifest.importerId);
            hash.Add("|");
            hash.Add(std::to_string(
                manifest.importerVersion));

            std::vector<std::string> dependencyNodes{};
            dependencyNodes.reserve(
                manifest.dependencies.size());
            for (const AssetDependencyDesc& dependency :
                manifest.dependencies) {
                dependencyNodes.push_back(
                    dependency.guid.value + "|" +
                    dependency.path + "|" +
                    dependency.role);
            }
            std::sort(
                dependencyNodes.begin(),
                dependencyNodes.end());
            for (const std::string& node : dependencyNodes) {
                hash.Add("|dep:");
                hash.Add(node);
            }

            std::vector<std::string> artifactNodes{};
            artifactNodes.reserve(manifest.artifacts.size());
            for (const AssetArtifactDesc& artifact :
                manifest.artifacts) {
                artifactNodes.push_back(
                    artifact.role + "|" +
                    artifact.path + "|" +
                    artifact.format);
            }
            std::sort(
                artifactNodes.begin(),
                artifactNodes.end());
            for (const std::string& node : artifactNodes) {
                hash.Add("|output:");
                hash.Add(node);
            }

            AddFileStamp(
                hash,
                assetDatabase.GetProjectRoot() /
                    record.sourcePath);
            for (const AssetArtifactDesc& artifact :
                manifest.artifacts) {
                AddFileStamp(
                    hash,
                    assetDatabase.GetProjectRoot() /
                        artifact.path);
            }
        }

        std::vector<std::string> BuildAssetDependencyClosure(
            const AssetUsageSummary& usage,
            const AssetDatabase& assetDatabase,
            std::vector<std::string>& missingAssetIds) {

            std::vector<std::string> pending(
                usage.usedGuids.begin(),
                usage.usedGuids.end());
            std::unordered_set<std::string> visited{};
            while (!pending.empty()) {
                std::string guid = std::move(pending.back());
                pending.pop_back();
                if (guid.empty() ||
                    !visited.insert(guid).second) {
                    continue;
                }
                const AssetRecord* record =
                    assetDatabase.FindByGuid(
                        AssetGuid{ guid });
                if (record == nullptr) {
                    missingAssetIds.push_back(guid);
                    continue;
                }
                for (const AssetDependencyDesc& dependency :
                    record->artifactManifest.dependencies) {
                    if (dependency.guid.IsValid()) {
                        pending.push_back(
                            dependency.guid.value);
                    }
                }
            }
            std::vector<std::string> result(
                visited.begin(),
                visited.end());
            std::sort(result.begin(), result.end());
            return result;
        }

        std::string BuildSceneHashInternal(
            const SceneDocument& document) {

            StableHashBuilder sceneHash{};
            sceneHash.Add(
                BuildCaptureEnvironmentNode(
                    document.environment).dump());

            std::vector<std::string> captureObjects{};
            for (const SceneObjectData& object :
                document.objects) {
                const SceneComponentData* model = nullptr;
                if (!IsLightingBakeModel(object, model)) {
                    continue;
                }
                captureObjects.push_back(
                    BuildCaptureObjectNode(
                        document,
                        object,
                        *model).dump());
            }
            std::sort(
                captureObjects.begin(),
                captureObjects.end());
            for (const std::string& object :
                captureObjects) {
                sceneHash.Add(object);
            }
            std::vector<std::string> captureLights{};
            for (const SceneObjectData& object :
                 document.objects) {
                const SceneComponentData* light =
                    FindComponent(
                        object,
                        LIGHTING_AUTHORING::
                            kLightComponentType);
                if (!object.enabled ||
                    light == nullptr ||
                    !light->properties.is_object()) {
                    continue;
                }
                captureLights.push_back(
                    nlohmann::json{
                        { "kind", "Light" },
                        {
                            "transformChain",
                            BuildTransformChain(
                                document,
                                object)
                        },
                        { "properties", light->properties }
                    }.dump());
            }
            std::sort(
                captureLights.begin(),
                captureLights.end());
            for (const std::string& light :
                 captureLights) {
                sceneHash.Add(light);
            }
            return sceneHash.Finish();
        }

    } // namespace

    std::string BuildLightingSceneHash(
        const SceneDocument& document) {

        return BuildSceneHashInternal(document);
    }

    std::unordered_map<std::string, std::string>
        BuildLightingBakeRecordAuthoringHashes(
            const SceneDocument& document) {

        std::unordered_map<std::string, std::string> hashes{};
        for (const LIGHTING_AUTHORING::
             ReflectionProbeAuthoring& probe :
             LIGHTING_AUTHORING::CollectReflectionProbes(
                 document)) {
            hashes.emplace(
                "reflection_probe_" +
                    std::to_string(probe.objectId.value),
                HashJson(
                    BuildReflectionProbeCaptureNode(probe)));
        }
        for (const LIGHTING_AUTHORING::
             IrradianceVolumeAuthoring& volume :
             LIGHTING_AUTHORING::CollectIrradianceVolumes(
                 document)) {
            hashes.emplace(
                "irradiance_volume_" +
                    std::to_string(volume.objectId.value),
                HashJson(
                    BuildIrradianceVolumeCaptureNode(volume)));
        }
        for (const SceneObjectData& object : document.objects) {
            const SceneComponentData* model =
                FindComponent(object, "ModelComponent");
            if (model == nullptr ||
                !model->properties.is_object() ||
                !model->properties.value(
                    "renderStatic",
                    false) ||
                !model->properties.value(
                    "includeInLightingBake",
                    true)) {
                continue;
            }
            hashes.emplace(
                "lightmap_object_" +
                    std::to_string(object.id.value),
                HashJson(
                    BuildSurfaceLightmapCaptureNode(
                        object,
                        *model)));
        }
        return hashes;
    }

    LightingBakeHashSnapshot BuildLightingBakeHashSnapshot(
        const SceneDocument& document,
        const AssetDatabase& assetDatabase) {

        LightingBakeHashSnapshot snapshot{};
        snapshot.sceneHash =
            BuildSceneHashInternal(document);
        snapshot.recordAuthoringHashes =
            BuildLightingBakeRecordAuthoringHashes(document);

        StableHashBuilder dependencyHash{};
        dependencyHash.Add(snapshot.sceneHash);
        const SceneDocument captureUsage =
            BuildLightingCaptureUsageDocument(document);
        const AssetUsageSummary usage = AnalyzeAssetUsage(
            captureUsage,
            assetDatabase);
        for (const AssetMissingReference& missing :
            usage.missingReferences) {
            snapshot.missingAssetIds.push_back(
                missing.assetId);
        }
        const std::vector<std::string> usedGuids =
            BuildAssetDependencyClosure(
                usage,
                assetDatabase,
                snapshot.missingAssetIds);
        for (const std::string& guid : usedGuids) {
            const AssetRecord* record = assetDatabase.FindByGuid(
                AssetGuid{ guid });
            if (record == nullptr) {
                continue;
            }
            AddAssetRecordRevision(
                dependencyHash,
                assetDatabase,
                *record);
        }
        std::sort(
            snapshot.missingAssetIds.begin(),
            snapshot.missingAssetIds.end());
        snapshot.missingAssetIds.erase(
            std::unique(
                snapshot.missingAssetIds.begin(),
                snapshot.missingAssetIds.end()),
            snapshot.missingAssetIds.end());
        for (const std::string& missing :
            snapshot.missingAssetIds) {
            dependencyHash.Add("|missing:");
            dependencyHash.Add(missing);
        }
        snapshot.dependencyHash = dependencyHash.Finish();
        return snapshot;
    }

} // namespace HIKARI::ASSETS::LIGHTING
