#include "HIKARI_LightingBakeManifest.h"

#include <algorithm>
#include <utility>

#include <json.hpp>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI::ASSETS::LIGHTING {

    namespace JsonMath = ::HIKARI::SERIALIZATION::JSON::MATH;

    namespace {

        std::string NormalizeInfluenceShape(std::string value) {
            return value == "Box" ? "Box" : "Sphere";
        }

        std::string NormalizeProjectionShape(std::string value) {
            return value == "Box" ? "Box" : "Infinite";
        }

        MATH::Vec3 RadiusBoxSize(float radius) {
            const float diameter = std::max(0.01f, radius * 2.0f);
            return { diameter, diameter, diameter };
        }

        ReflectionProbeBakeRecord ReadReflectionProbe(const nlohmann::json& node) {
            ReflectionProbeBakeRecord record{};
            if (!node.is_object()) {
                return record;
            }

            record.id = node.value("id", std::string{});
            record.name = node.value("name", std::string{});
            record.position = JSONREAD::Vec3Or(node.value("position", nlohmann::json::array()), {});
            record.radius = node.value("radius", 0.0f);
            record.intensity = node.value("intensity", 1.0f);
            record.influenceShape = NormalizeInfluenceShape(node.value("influenceShape", std::string{ "Sphere" }));
            record.influenceBoxCenter = node.contains("influenceBoxCenter")
                ? JSONREAD::Vec3Or(node["influenceBoxCenter"], {})
                : record.position;
            record.influenceBoxSize = node.contains("influenceBoxSize")
                ? JSONREAD::Vec3Or(node["influenceBoxSize"], {})
                : RadiusBoxSize(record.radius);
            record.projectionShape = NormalizeProjectionShape(node.value("projectionShape", std::string{ "Infinite" }));
            record.projectionBoxCenter = node.contains("projectionBoxCenter")
                ? JSONREAD::Vec3Or(node["projectionBoxCenter"], {})
                : record.position;
            record.projectionBoxSize = node.contains("projectionBoxSize")
                ? JSONREAD::Vec3Or(node["projectionBoxSize"], {})
                : RadiusBoxSize(record.radius);
            record.blendDistance = node.value("blendDistance", 1.0f);
            record.priority = node.value("priority", 0);
            record.captureCubemapPath = node.value("captureCubemapPath", std::string{});
            record.prefilteredCubemapPath = node.value("prefilteredCubemapPath", std::string{});
            record.brdfLutPath = node.value("brdfLutPath", std::string{});
            record.prefilteredMipCount = node.value("prefilteredMipCount", 1u);
            return record;
        }

        LightProbeBakeRecord ReadLightProbe(const nlohmann::json& node) {
            LightProbeBakeRecord record{};
            if (!node.is_object()) {
                return record;
            }

            record.id = node.value("id", std::string{});
            record.name = node.value("name", std::string{});
            record.type = node.value("type", std::string{ "VolumeGrid" });
            record.position = JSONREAD::Vec3Or(node.value("position", nlohmann::json::array()), {});
            record.origin = JSONREAD::Vec3Or(node.value("origin", nlohmann::json::array()), {});
            record.size = JSONREAD::Vec3Or(node.value("size", nlohmann::json::array()), {});
            record.countX = node.value("countX", 0u);
            record.countY = node.value("countY", 0u);
            record.countZ = node.value("countZ", 0u);
            record.shOrder = node.value("shOrder", 3u);
            record.probeCount = node.value("probeCount", 0u);
            record.shDataPath = node.value("shDataPath", std::string{});
            return record;
        }

        LightmapBakeRecord ReadLightmap(const nlohmann::json& node) {
            LightmapBakeRecord record{};
            if (!node.is_object()) {
                return record;
            }

            record.id = node.value("id", std::string{});
            record.texturePath = node.value("texturePath", std::string{});
            record.width = node.value("width", 0u);
            record.height = node.value("height", 0u);
            return record;
        }

        nlohmann::json ToJson(const ReflectionProbeBakeRecord& record) {
            return nlohmann::json{
                { "id", record.id },
                { "name", record.name },
                { "position", JsonMath::ToJsonArray(record.position) },
                { "radius", record.radius },
                { "intensity", record.intensity },
                { "influenceShape", NormalizeInfluenceShape(record.influenceShape) },
                { "influenceBoxCenter",
                    JsonMath::ToJsonArray(
                        record.influenceBoxCenter) },
                { "influenceBoxSize",
                    JsonMath::ToJsonArray(
                        record.influenceBoxSize) },
                { "projectionShape", NormalizeProjectionShape(record.projectionShape) },
                { "projectionBoxCenter",
                    JsonMath::ToJsonArray(
                        record.projectionBoxCenter) },
                { "projectionBoxSize",
                    JsonMath::ToJsonArray(
                        record.projectionBoxSize) },
                { "blendDistance", record.blendDistance },
                { "priority", record.priority },
                { "captureCubemapPath", record.captureCubemapPath },
                { "prefilteredCubemapPath", record.prefilteredCubemapPath },
                { "brdfLutPath", record.brdfLutPath },
                { "prefilteredMipCount", record.prefilteredMipCount },
            };
        }

        nlohmann::json ToJson(const LightProbeBakeRecord& record) {
            return nlohmann::json{
                { "id", record.id },
                { "name", record.name },
                { "type", record.type },
                { "position", JsonMath::ToJsonArray(record.position) },
                { "origin", JsonMath::ToJsonArray(record.origin) },
                { "size", JsonMath::ToJsonArray(record.size) },
                { "countX", record.countX },
                { "countY", record.countY },
                { "countZ", record.countZ },
                { "shOrder", record.shOrder },
                { "probeCount", record.probeCount },
                { "shDataPath", record.shDataPath },
            };
        }

        nlohmann::json ToJson(const LightmapBakeRecord& record) {
            return nlohmann::json{
                { "id", record.id },
                { "texturePath", record.texturePath },
                { "width", record.width },
                { "height", record.height },
            };
        }

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage) {
                *outMessage = std::move(message);
            }
        }

    } // namespace

    bool LoadLightingBakeManifest(
        const std::filesystem::path& path,
        LightingBakeManifest& outManifest,
        std::string* outMessage) {

        nlohmann::json root{};
        std::string ioMessage{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root,
                &ioMessage)) {
            SetMessage(
                outMessage,
                "manifest could not be read: " + ioMessage);
            return false;
        }
        if (!root.is_object()) {
            SetMessage(outMessage, "manifest parse failed: " + path.generic_string());
            return false;
        }

        LightingBakeManifest manifest{};
        manifest.version = root.value("version", kLightingBakeManifestVersion);
        manifest.sceneGuid = root.value("sceneGuid", std::string{});
        manifest.bakeGuid = root.value("bakeGuid", std::string{});
        manifest.bakeVersion = root.value("bakeVersion", 1u);
        manifest.generatedRoot = root.value("generatedRoot", std::string{});

        for (const nlohmann::json& node : root.value("reflectionProbes", nlohmann::json::array())) {
            manifest.reflectionProbes.push_back(ReadReflectionProbe(node));
        }
        for (const nlohmann::json& node : root.value("lightProbes", nlohmann::json::array())) {
            manifest.lightProbes.push_back(ReadLightProbe(node));
        }
        for (const nlohmann::json& node : root.value("lightmaps", nlohmann::json::array())) {
            manifest.lightmaps.push_back(ReadLightmap(node));
        }

        outManifest = std::move(manifest);
        SetMessage(outMessage, "manifest loaded");
        return true;
    }

    bool SaveLightingBakeManifest(
        const std::filesystem::path& path,
        const LightingBakeManifest& manifest,
        std::string* outMessage) {

        nlohmann::json reflectionProbes = nlohmann::json::array();
        for (const ReflectionProbeBakeRecord& record : manifest.reflectionProbes) {
            reflectionProbes.push_back(ToJson(record));
        }

        nlohmann::json lightProbes = nlohmann::json::array();
        for (const LightProbeBakeRecord& record : manifest.lightProbes) {
            lightProbes.push_back(ToJson(record));
        }

        nlohmann::json lightmaps = nlohmann::json::array();
        for (const LightmapBakeRecord& record : manifest.lightmaps) {
            lightmaps.push_back(ToJson(record));
        }

        // Bake manifest は editor baker が生成し、runtime loader が消費する。
        const nlohmann::json root{
            { "version", manifest.version },
            { "sceneGuid", manifest.sceneGuid },
            { "bakeGuid", manifest.bakeGuid },
            { "bakeVersion", manifest.bakeVersion },
            { "generatedRoot", manifest.generatedRoot },
            { "reflectionProbes", reflectionProbes },
            { "lightProbes", lightProbes },
            { "lightmaps", lightmaps },
        };

        std::string ioMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
                path,
                root,
                &ioMessage)) {
            SetMessage(
                outMessage,
                "failed to write manifest: " + ioMessage);
            return false;
        }
        SetMessage(outMessage, "manifest saved");
        return true;
    }

    std::filesystem::path BuildLightingBakeRoot(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) {

        return (projectRoot / "Library" / "Generated" / "Lighting" / sceneGuid).lexically_normal();
    }

    std::filesystem::path BuildLightingBakeManifestPath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) {

        return BuildLightingBakeRoot(projectRoot, sceneGuid) / "bake_manifest.json";
    }

} // namespace HIKARI::ASSETS::LIGHTING
