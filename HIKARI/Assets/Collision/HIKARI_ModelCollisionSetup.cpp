#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>

#include <json.hpp>

#include "Assets/Collision/HIKARI_ModelCollisionSetupGeometry.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::COLLISION {

    namespace JsonMath = ::HIKARI::SERIALIZATION::JSON::MATH;

    namespace {
        const char* ToString(CollisionGeometryShapeType type) noexcept {
            switch (type) {
            case CollisionGeometryShapeType::Sphere:
                return "Sphere";
            case CollisionGeometryShapeType::Capsule:
                return "Capsule";
            case CollisionGeometryShapeType::ConvexHull:
                return "ConvexHull";
            case CollisionGeometryShapeType::TriangleMesh:
                return "TriangleMesh";
            case CollisionGeometryShapeType::Box:
            default:
                return "Box";
            }
        }

        CollisionGeometryShapeType ParseShapeType(
            std::string_view value) noexcept {

            if (value == "Sphere") {
                return CollisionGeometryShapeType::Sphere;
            }
            if (value == "Capsule") {
                return CollisionGeometryShapeType::Capsule;
            }
            if (value == "ConvexHull") {
                return CollisionGeometryShapeType::ConvexHull;
            }
            if (value == "TriangleMesh") {
                return CollisionGeometryShapeType::TriangleMesh;
            }
            return CollisionGeometryShapeType::Box;
        }

        MATH::Vec3 ReadVec3(
            const nlohmann::json& value,
            const MATH::Vec3& fallback) {

            if (!value.is_array() || value.size() != 3u) {
                return fallback;
            }
            return {
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>()
            };
        }

        bool IsShapeValid(const ModelCollisionShape& shape) noexcept {
            if (shape.id == 0u ||
                shape.name.empty() ||
                !MATH::IsFinite(shape.center) ||
                !MATH::IsFinite(shape.rotationEulerDegrees) ||
                !MATH::IsFinite(shape.size) ||
                !std::isfinite(shape.radius) ||
                !std::isfinite(shape.height) ||
                !std::isfinite(shape.generationError) ||
                shape.generationError < 0.0f ||
                !std::all_of(
                    shape.vertices.begin(),
                    shape.vertices.end(),
                    [](const MATH::Vec3& vertex) {
                        return MATH::IsFinite(vertex);
                    }) ||
                !std::all_of(
                    shape.sourceNodeIndices.begin(),
                    shape.sourceNodeIndices.end(),
                    [](int32_t index) { return index >= 0; })) {
                return false;
            }
            switch (shape.type) {
            case CollisionGeometryShapeType::Sphere:
                return shape.radius > 0.0f;
            case CollisionGeometryShapeType::Capsule:
                return shape.radius > 0.0f &&
                    shape.height >= shape.radius * 2.0f;
            case CollisionGeometryShapeType::ConvexHull:
                return shape.vertices.size() >= 4u &&
                    (shape.indices.empty() || shape.indices.size() % 3u == 0u) &&
                    std::all_of(
                        shape.indices.begin(),
                        shape.indices.end(),
                        [&shape](uint32_t index) {
                            return index < shape.vertices.size();
                        });
            case CollisionGeometryShapeType::TriangleMesh:
                return shape.vertices.size() >= 3u &&
                    shape.indices.size() >= 3u &&
                    shape.indices.size() % 3u == 0u &&
                    std::all_of(
                        shape.indices.begin(),
                        shape.indices.end(),
                        [&shape](uint32_t index) {
                            return index < shape.vertices.size();
                        });
            case CollisionGeometryShapeType::Box:
            default:
                return shape.size.x > 0.0f &&
                    shape.size.y > 0.0f &&
                    shape.size.z > 0.0f;
            }
        }

        nlohmann::json SerializeSetup(const ModelCollisionSetup& setup) {
            nlohmann::json shapes = nlohmann::json::array();
            for (const ModelCollisionShape& shape : setup.shapes) {
                shapes.push_back({
                    { "id", shape.id },
                    { "name", shape.name },
                    { "type", ToString(shape.type) },
                    { "center", JsonMath::ToJsonArray(shape.center) },
                    { "rotationEulerDegrees",
                        JsonMath::ToJsonArray(
                            shape.rotationEulerDegrees) },
                    { "size", JsonMath::ToJsonArray(shape.size) },
                    { "radius", shape.radius },
                    { "height", shape.height },
                    { "enabled", shape.enabled },
                    { "generated", shape.generated },
                    { "sourceNodeIndices", shape.sourceNodeIndices },
                    { "generationMethod", shape.generationMethod },
                    { "generationError", shape.generationError },
                    { "geometryVertexCount", shape.vertices.size() },
                    { "geometryIndexCount", shape.indices.size() },
                });
            }
            return {
                { "version", setup.version },
                { "modelAssetGuid", setup.modelAssetGuid },
                { "nextShapeId", setup.nextShapeId },
                { "shapes", std::move(shapes) },
            };
        }
    }

    uint64_t ModelCollisionSetup::AllocateShapeId() noexcept {
        nextShapeId = (std::max)(nextShapeId, 1ull);
        const uint64_t result = nextShapeId++;
        if (nextShapeId == 0u) {
            nextShapeId = 1u;
        }
        return result;
    }

    ModelCollisionShape* ModelCollisionSetup::FindShape(
        uint64_t id) noexcept {

        const auto found = std::find_if(
            shapes.begin(),
            shapes.end(),
            [id](const ModelCollisionShape& shape) {
                return shape.id == id;
            });
        return found != shapes.end() ? &*found : nullptr;
    }

    const ModelCollisionShape* ModelCollisionSetup::FindShape(
        uint64_t id) const noexcept {

        const auto found = std::find_if(
            shapes.begin(),
            shapes.end(),
            [id](const ModelCollisionShape& shape) {
                return shape.id == id;
            });
        return found != shapes.end() ? &*found : nullptr;
    }

    std::filesystem::path GetModelCollisionSetupPath(
        const AssetRecord& record) {

        if (record.metaPath.empty()) {
            return {};
        }
        const std::string sourceFile = record.sourcePath.filename().string();
        return record.metaPath.parent_path() /
            (sourceFile + ".hikari.collision.json");
    }

    ModelCollisionSetupLoadResult LoadModelCollisionSetup(
        const std::filesystem::path& path,
        std::string_view expectedModelGuid,
        ModelCollisionSetup& outSetup) {

        outSetup = {};
        outSetup.modelAssetGuid = expectedModelGuid;
        ModelCollisionSetupLoadResult result{};
        if (path.empty()) {
            result.message = "collision setup path is empty";
            return result;
        }

        std::error_code existsEc{};
        result.exists = std::filesystem::exists(path, existsEc);
        if (existsEc) {
            result.message = "failed to inspect collision setup: " +
                existsEc.message();
            return result;
        }
        if (!result.exists) {
            result.success = true;
            result.message = "new collision setup";
            return result;
        }

        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root,
                &result.message)) {
            return result;
        }
        if (!root.is_object()) {
            result.message = "collision setup JSON is invalid";
            return result;
        }

        outSetup.version = root.value("version", 0u);
        outSetup.modelAssetGuid = root.value(
            "modelAssetGuid",
            std::string{});
        outSetup.nextShapeId = root.value("nextShapeId", 1ull);
        if (root.contains("shapes") && root["shapes"].is_array()) {
            for (const nlohmann::json& source : root["shapes"]) {
                if (!source.is_object()) {
                    continue;
                }
                ModelCollisionShape shape{};
                shape.id = source.value("id", 0ull);
                shape.name = source.value("name", std::string{});
                shape.type = ParseShapeType(source.value(
                    "type",
                    std::string("Box")));
                shape.center = ReadVec3(
                    source.value("center", nlohmann::json{}),
                    {});
                shape.rotationEulerDegrees = ReadVec3(
                    source.value(
                        "rotationEulerDegrees",
                        nlohmann::json{}),
                    {});
                shape.size = ReadVec3(
                    source.value("size", nlohmann::json{}),
                    { 1.0f, 1.0f, 1.0f });
                shape.radius = source.value("radius", 0.5f);
                shape.height = source.value("height", 1.0f);
                shape.enabled = source.value("enabled", true);
                shape.generated = source.value("generated", false);
                shape.sourceNodeIndices = source.value(
                    "sourceNodeIndices",
                    std::vector<int32_t>{});
                shape.generationMethod = source.value(
                    "generationMethod",
                    std::string{});
                shape.generationError = source.value(
                    "generationError",
                    0.0f);
                outSetup.shapes.push_back(std::move(shape));
            }
        }

        if (outSetup.modelAssetGuid.empty()) {
            outSetup.modelAssetGuid = expectedModelGuid;
        }
        if (outSetup.modelAssetGuid != expectedModelGuid) {
            result.message =
                "collision setup belongs to another model GUID";
            outSetup = {};
            outSetup.modelAssetGuid = expectedModelGuid;
            return result;
        }
        if (!LoadModelCollisionSetupGeometry(
                path,
                outSetup,
                result.message) ||
            !ValidateModelCollisionSetup(outSetup, result.message)) {
            return result;
        }
        result.success = true;
        result.message = "collision setup loaded";
        return result;
    }

    bool SaveModelCollisionSetup(
        const std::filesystem::path& path,
        const ModelCollisionSetup& setup,
        std::string& outMessage) {

        if (!ValidateModelCollisionSetup(setup, outMessage)) {
            return false;
        }
        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "failed to create collision setup directory: " +
                ec.message();
            return false;
        }
        const std::filesystem::path stagedSetup(
            path.string() + ".saving");
        const std::filesystem::path stagedGeometry =
            GetModelCollisionSetupGeometryPath(stagedSetup);
        const std::filesystem::path finalGeometry =
            GetModelCollisionSetupGeometryPath(path);
        std::filesystem::remove(stagedSetup, ec);
        ec.clear();
        std::filesystem::remove(stagedGeometry, ec);
        ec.clear();
        if (!SaveModelCollisionSetupGeometry(
                stagedSetup,
                setup,
                outMessage)) {
            return false;
        }

        {
            std::ofstream stream(stagedSetup, std::ios::trunc);
            if (!stream.is_open()) {
                std::filesystem::remove(stagedGeometry, ec);
                outMessage = "failed to open collision setup for write";
                return false;
            }
            stream << SerializeSetup(setup).dump(2) << '\n';
            stream.flush();
            if (!stream.good()) {
                stream.close();
                std::filesystem::remove(stagedSetup, ec);
                ec.clear();
                std::filesystem::remove(stagedGeometry, ec);
                outMessage = "failed while writing collision setup";
                return false;
            }
        }

        ModelCollisionSetup verified{};
        const ModelCollisionSetupLoadResult verification =
            LoadModelCollisionSetup(
                stagedSetup,
                setup.modelAssetGuid,
                verified);
        if (!verification.success) {
            std::filesystem::remove(stagedSetup, ec);
            ec.clear();
            std::filesystem::remove(stagedGeometry, ec);
            outMessage = "collision setup verification failed: " +
                verification.message;
            return false;
        }

        const bool hasStagedGeometry = std::filesystem::exists(
            stagedGeometry,
            ec);
        std::vector<IO::FileReplacementOperation> operations{};
        operations.push_back({ path, stagedSetup, false });
        operations.push_back({
            finalGeometry,
            stagedGeometry,
            !hasStagedGeometry
        });
        if (!IO::CommitFileReplacementTransaction(
                operations,
                outMessage)) {
            std::filesystem::remove(stagedSetup, ec);
            ec.clear();
            std::filesystem::remove(stagedGeometry, ec);
            outMessage = "failed to commit collision setup: " +
                outMessage;
            return false;
        }
        outMessage = "collision setup saved";
        return true;
    }

    bool ValidateModelCollisionSetup(
        const ModelCollisionSetup& setup,
        std::string& outMessage) {

        if (setup.version != kModelCollisionSetupVersion) {
            outMessage =
                "unsupported model collision setup version; regenerate and save collision with the current editor";
            return false;
        }
        if (setup.modelAssetGuid.empty()) {
            outMessage = "model collision setup has no model GUID";
            return false;
        }

        std::unordered_set<uint64_t> ids{};
        uint64_t maximumId = 0u;
        for (const ModelCollisionShape& shape : setup.shapes) {
            if (!IsShapeValid(shape)) {
                outMessage = "model collision setup has an invalid shape";
                return false;
            }
            if (!ids.insert(shape.id).second) {
                outMessage = "model collision setup has duplicate shape IDs";
                return false;
            }
            maximumId = (std::max)(maximumId, shape.id);
        }
        if (setup.nextShapeId <= maximumId) {
            outMessage = "model collision setup next shape ID is invalid";
            return false;
        }
        outMessage.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
