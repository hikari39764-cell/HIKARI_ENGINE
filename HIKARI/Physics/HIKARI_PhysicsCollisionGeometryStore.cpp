#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"

#include <algorithm>
#include <cmath>

#include "Assets/Collision/HIKARI_HcollisionFormat.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"

namespace HIKARI::PHYSICS {
    namespace {

        constexpr float kDegreesToRadians =
            0.01745329251994329577f;

        MATH::Vec3 Absolute(const MATH::Vec3& value) noexcept {
            return {
                std::abs(value.x),
                std::abs(value.y),
                std::abs(value.z)
            };
        }

        MATH::Vec3 Multiply(
            const MATH::Vec3& left,
            const MATH::Vec3& right) noexcept {
            return {
                left.x * right.x,
                left.y * right.y,
                left.z * right.z
            };
        }

        PhysicsShapeType ToRuntimeShapeType(
            ASSETS::COLLISION::CollisionGeometryShapeType type) noexcept {
            switch (type) {
            case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
                return PhysicsShapeType::Sphere;
            case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
                return PhysicsShapeType::Capsule;
            case ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull:
                return PhysicsShapeType::ConvexHull;
            case ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh:
                return PhysicsShapeType::TriangleMesh;
            case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
            default:
                return PhysicsShapeType::Box;
            }
        }

        PhysicsShapeDesc BuildShape(
            const ASSETS::COLLISION::CollisionGeometryShape& source,
            const ColliderComponent& collider,
            const MATH::Vec3& worldScale,
            uint32_t componentOrdinal,
            uint64_t contentRevision,
            std::shared_ptr<const PhysicsGeometryBuffer> geometry) {
            const MATH::Vec3 scale = Absolute(worldScale);
            PhysicsShapeDesc shape{};
            shape.key.sourceShapeId = source.id;
            shape.key.componentOrdinal = componentOrdinal;
            shape.type = ToRuntimeShapeType(source.type);
            shape.localCenter = Multiply(source.center, scale);
            shape.localRotation = MATH::NormalizeQ(
                MATH::Quat::FromEulerXYZ(
                    source.rotationEulerDegrees.x * kDegreesToRadians,
                    source.rotationEulerDegrees.y * kDegreesToRadians,
                    source.rotationEulerDegrees.z * kDegreesToRadians));
            shape.halfExtents = Multiply(source.size, scale) * 0.5f;
            const float radialScale = shape.type ==
                    PhysicsShapeType::Sphere
                ? (std::max)({ scale.x, scale.y, scale.z })
                : (std::max)(scale.x, scale.z);
            shape.radius = source.radius * radialScale;
            shape.height = source.height * scale.y;
            shape.localScale = scale;
            shape.geometry = std::move(geometry);
            shape.vertexOffset = source.vertexOffset;
            shape.vertexCount = source.vertexCount;
            shape.indexOffset = source.indexOffset;
            shape.indexCount = source.indexCount;
            shape.geometryContentRevision = contentRevision;
            shape.isTrigger = collider.IsTrigger();
            shape.material = PhysicsMaterialDesc{
                collider.GetFriction(),
                collider.GetRestitution(),
                collider.GetDensity()
            };
            shape.filter = PhysicsCollisionFilter{
                collider.GetCollisionLayer(),
                collider.GetCollisionMask()
            };
            return shape;
        }

    } // namespace

    void PhysicsCollisionGeometryStore::SetAssetDatabase(
        AssetDatabase* assetDatabase) noexcept {
        if (assetDatabase_ == assetDatabase) {
            return;
        }
        assetDatabase_ = assetDatabase;
        Clear();
    }

    void PhysicsCollisionGeometryStore::Clear() noexcept {
        cache_.clear();
        failures_.clear();
    }

    bool PhysicsCollisionGeometryStore::ResolveArtifactPath(
        const std::string& assetId,
        std::filesystem::path& outPath) const {
        if (assetDatabase_ == nullptr || assetId.empty()) {
            return false;
        }
        const AssetRecord* record = assetDatabase_->FindByGuid(
            AssetGuid{ assetId });
        if (record == nullptr) {
            return false;
        }
        const AssetArtifactDesc* artifact =
            ASSETS::SEMANTICS::FindCompatibleAssetArtifact(
                *record,
                ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry);
        if (artifact == nullptr) {
            return false;
        }
        outPath = std::filesystem::path(artifact->path);
        if (outPath.is_relative()) {
            outPath = assetDatabase_->GetProjectRoot() / outPath;
        }
        outPath = outPath.lexically_normal();
        return true;
    }

    const PhysicsCollisionGeometryStore::CacheEntry*
        PhysicsCollisionGeometryStore::Load(
            const std::string& assetId,
            PhysicsErrorCode& outError,
            std::string& outMessage) {
        outError = PhysicsErrorCode::None;
        outMessage.clear();
        const uint64_t databaseRevision = assetDatabase_ != nullptr
            ? assetDatabase_->GetContentRevision()
            : 0u;
        const auto cachedFailure = failures_.find(assetId);
        if (cachedFailure != failures_.end() &&
            cachedFailure->second.artifactPath.empty() &&
            cachedFailure->second.databaseRevision == databaseRevision) {
            outError = cachedFailure->second.error;
            outMessage = cachedFailure->second.message;
            return nullptr;
        }
        std::filesystem::path artifactPath{};
        if (!ResolveArtifactPath(assetId, artifactPath)) {
            outError = PhysicsErrorCode::CollisionAssetMissing;
            outMessage = "collision artifact is missing from the asset manifest";
            failures_.insert_or_assign(
                assetId,
                FailureEntry{
                    {}, databaseRevision, outError, outMessage
                });
            return nullptr;
        }

        const auto failed = failures_.find(assetId);
        if (failed != failures_.end() &&
            failed->second.artifactPath == artifactPath &&
            failed->second.databaseRevision == databaseRevision) {
            outError = failed->second.error;
            outMessage = failed->second.message;
            return nullptr;
        }

        auto found = cache_.find(assetId);
        if (found != cache_.end() &&
            found->second.artifactPath == artifactPath &&
            found->second.databaseRevision == databaseRevision) {
            return &found->second;
        }

        ASSETS::COLLISION::CollisionGeometryAsset asset{};
        std::string message{};
        ASSETS::COLLISION::HcollisionReadInfo readInfo{};
        if (!ASSETS::COLLISION::ReadHcollisionFile(
                artifactPath,
                asset,
                message,
                &readInfo)) {
            HIKARI_LOG_ERROR(
                "[Physics] failed to load collision geometry: " +
                message);
            outError = PhysicsErrorCode::CollisionAssetInvalid;
            outMessage = std::move(message);
            failures_.insert_or_assign(
                assetId,
                FailureEntry{
                    artifactPath,
                    databaseRevision,
                    outError,
                    outMessage
                });
            return nullptr;
        }

        CacheEntry& entry = cache_[assetId];
        entry.artifactPath = std::move(artifactPath);
        entry.databaseRevision = databaseRevision;
        entry.contentRevision = readInfo.contentHash;
        entry.shapes = std::move(asset.shapes);
        auto geometry = std::make_shared<PhysicsGeometryBuffer>();
        geometry->vertices = std::move(asset.vertices);
        geometry->indices = std::move(asset.indices);
        entry.geometry = std::move(geometry);
        failures_.erase(assetId);
        return &entry;
    }

    PhysicsCollisionGeometryStore::AppendResult
        PhysicsCollisionGeometryStore::AppendShapes(
        const ColliderComponent& collider,
        const MATH::Vec3& worldScale,
        uint32_t componentOrdinal,
        std::vector<PhysicsShapeDesc>& outShapes) {
        AppendResult result{};
        if (!collider.IsEnabled() ||
            !collider.UsesCollisionGeometryAsset()) {
            result.error = PhysicsErrorCode::InvalidBodyDefinition;
            result.message = "collider does not reference collision geometry";
            return result;
        }
        PhysicsErrorCode error = PhysicsErrorCode::None;
        std::string message{};
        const CacheEntry* entry = Load(
            collider.GetCollisionGeometryAssetId(),
            error,
            message);
        if (entry == nullptr || entry->shapes.empty() ||
            !entry->geometry) {
            result.error = error == PhysicsErrorCode::None
                ? PhysicsErrorCode::CollisionAssetInvalid
                : error;
            result.message = message.empty()
                ? "collision artifact contains no usable shapes"
                : std::move(message);
            return result;
        }
        outShapes.reserve(outShapes.size() + entry->shapes.size());
        for (const ASSETS::COLLISION::CollisionGeometryShape& source :
                entry->shapes) {
            outShapes.push_back(BuildShape(
                source,
                collider,
                worldScale,
                componentOrdinal,
                entry->contentRevision,
                entry->geometry));
        }
        result.success = true;
        result.contentRevision = entry->contentRevision;
        result.appendedShapeCount = static_cast<uint32_t>(
            entry->shapes.size());
        result.message = "collision geometry ready";
        return result;
    }

    uint64_t PhysicsCollisionGeometryStore::GetSourceRevision()
        const noexcept {
        return assetDatabase_ != nullptr
            ? assetDatabase_->GetContentRevision()
            : 0u;
    }

} // namespace HIKARI::PHYSICS
