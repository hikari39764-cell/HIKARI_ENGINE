#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::ASSETS::LIGHTING {

    constexpr uint32_t kLightingBakeManifestVersion = 1;

    struct ReflectionProbeBakeRecord {
        std::string id{};
        std::string name{};
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        float radius = 0.0f;
        float intensity = 1.0f;
        std::string influenceShape{ "Sphere" };
        MATH::Vec3 influenceBoxCenter{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 influenceBoxSize{ 0.0f, 0.0f, 0.0f };
        std::string projectionShape{ "Infinite" };
        MATH::Vec3 projectionBoxCenter{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 projectionBoxSize{ 0.0f, 0.0f, 0.0f };
        float blendDistance = 1.0f;
        int priority = 0;
        std::string captureCubemapPath{};
        std::string prefilteredCubemapPath{};
        std::string brdfLutPath{};
        uint32_t prefilteredMipCount = 1;
    };

    struct LightProbeBakeRecord {
        std::string id{};
        std::string name{};
        std::string type{ "VolumeGrid" };
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 origin{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 size{ 0.0f, 0.0f, 0.0f };
        uint32_t countX = 0;
        uint32_t countY = 0;
        uint32_t countZ = 0;
        uint32_t shOrder = 3;
        uint32_t probeCount = 0;
        std::string shDataPath{};
    };

    struct LightmapBakeRecord {
        std::string id{};
        std::string texturePath{};
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct LightingBakeManifest {
        uint32_t version = kLightingBakeManifestVersion;
        std::string sceneGuid{};
        std::string bakeGuid{};
        uint32_t bakeVersion = 1;
        std::string generatedRoot{};
        std::vector<ReflectionProbeBakeRecord> reflectionProbes{};
        std::vector<LightProbeBakeRecord> lightProbes{};
        std::vector<LightmapBakeRecord> lightmaps{};
    };

    bool LoadLightingBakeManifest(
        const std::filesystem::path& path,
        LightingBakeManifest& outManifest,
        std::string* outMessage = nullptr);

    bool SaveLightingBakeManifest(
        const std::filesystem::path& path,
        const LightingBakeManifest& manifest,
        std::string* outMessage = nullptr);

    std::filesystem::path BuildLightingBakeRoot(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid);

    std::filesystem::path BuildLightingBakeManifestPath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid);

} // namespace HIKARI::ASSETS::LIGHTING
