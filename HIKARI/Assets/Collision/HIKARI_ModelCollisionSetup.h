#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"
#include "Assets/HIKARI_AssetRecord.h"

namespace HIKARI::ASSETS::COLLISION {

    constexpr uint32_t kModelCollisionSetupVersion = 2u;

    struct ModelCollisionShape {
        uint64_t id = 0u;
        std::string name{};
        CollisionGeometryShapeType type =
            CollisionGeometryShapeType::Box;
        MATH::Vec3 center{};
        MATH::Vec3 rotationEulerDegrees{};
        MATH::Vec3 size{ 1.0f, 1.0f, 1.0f };
        float radius = 0.5f;
        float height = 1.0f;
        bool enabled = true;
        bool generated = false;
        std::vector<int32_t> sourceNodeIndices{};
        std::vector<MATH::Vec3> vertices{};
        std::vector<uint32_t> indices{};
        std::string generationMethod{};
        float generationError = 0.0f;
    };

    struct ModelCollisionSetup {
        uint32_t version = kModelCollisionSetupVersion;
        std::string modelAssetGuid{};
        uint64_t nextShapeId = 1u;
        std::vector<ModelCollisionShape> shapes{};

        uint64_t AllocateShapeId() noexcept;
        ModelCollisionShape* FindShape(uint64_t id) noexcept;
        const ModelCollisionShape* FindShape(uint64_t id) const noexcept;
    };

    struct ModelCollisionSetupLoadResult {
        bool success = false;
        bool exists = false;
        std::string message{};
    };

    std::filesystem::path GetModelCollisionSetupPath(
        const AssetRecord& record);

    ModelCollisionSetupLoadResult LoadModelCollisionSetup(
        const std::filesystem::path& path,
        std::string_view expectedModelGuid,
        ModelCollisionSetup& outSetup);

    bool SaveModelCollisionSetup(
        const std::filesystem::path& path,
        const ModelCollisionSetup& setup,
        std::string& outMessage);

    bool ValidateModelCollisionSetup(
        const ModelCollisionSetup& setup,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::COLLISION
