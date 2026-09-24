#pragma once

#include <cstdint>
#include <unordered_map>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"

namespace HIKARI {

class GameObject;
class RuntimeObjectActivityService;
class World;
class WorldEventStream;
struct RuntimePlayStateService;
namespace PHYSICS {
class KinematicMotionService;
class PhysicsRuntimeStatusService;
class PhysicsWorldService;
} // namespace PHYSICS

// Physics collider / follower compatibility を唯一の
// TerrainInteractionSample 契約へ適配する producer。consumer である
// SandSurfaceSystem は source component を直接走査しない。
class TerrainInteractionSourceSystem final : public ISystem {
public:
    std::string_view GetName() const override { return "TerrainInteractionSourceSystem"; }

    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void PreFixedUpdate(World& world, const FrameContext& frame) override;
    void PostFixedUpdate(World& world, const FrameContext& frame) override;
    void Update(World& world, const FrameContext& frame) override;

private:
    struct ContactKey {
        TERRAIN::TerrainInteractionSourceKey source{};
        RuntimeObjectHandle target{};
        // Physics shape identity is part of the contact generation. A
        // live collider rebuild must not inherit the previous shape's
        // Begin/Persist history even when the component ordinal is same.
        uint64_t sourceShapeId = 0u;
        uint64_t definitionSignature = 0u;

        bool operator==(const ContactKey& rhs) const noexcept {
            return source.object == rhs.source.object && source.featureId == rhs.source.featureId &&
                   target == rhs.target && sourceShapeId == rhs.sourceShapeId &&
                   definitionSignature == rhs.definitionSignature;
        }
    };

    struct ContactKeyHash {
        std::size_t operator()(const ContactKey& key) const noexcept;
    };

    struct SourceKeyHash {
        std::size_t operator()(const TERRAIN::TerrainInteractionSourceKey& key) const noexcept;
    };

    struct ActiveRigidContact {
        uint32_t referenceCount = 0u;
        bool submitted = false;
        bool kinematic = false;
        bool endedThisTick = false;
        MATH::Vec3 normalWS{0.0f, 1.0f, 0.0f};
        float penetrationDepthWS = 0.0f;
        float normalImpulseNs = 0.0f;
        MATH::Vec3 positionWS{};
        TERRAIN::TerrainInteractionFootprint footprint{};
        float normalLoadN = 0.0f;
        float contactAgeSeconds = 0.0f;
        float stationaryPublishRemainingSeconds = 0.0f;
        uint64_t sourceShapeId = 0u;
        uint64_t definitionSignature = 0u;
    };

    struct ColliderObservation {
        MATH::Vec3 positionWS{};
        TERRAIN::TerrainInteractionFootprint footprint{};
    };

    struct ActiveFollowerContact {
        RuntimeObjectHandle target{};
        MATH::Vec3 positionWS{};
        TERRAIN::TerrainInteractionFootprint footprint{};
        float contactAgeSeconds = 0.0f;
        float stationaryPublishRemainingSeconds = 0.0f;
        bool submitted = false;
    };

    struct ActiveCharacterContact {
        RuntimeObjectHandle target{};
        MATH::Vec3 positionWS{};
        MATH::Vec3 normalWS{0.0f, 1.0f, 0.0f};
        TERRAIN::TerrainInteractionFootprint footprint{};
        float normalLoadN = 0.0f;
        float contactAgeSeconds = 0.0f;
        float stationaryPublishRemainingSeconds = 0.0f;
    };

    struct EditorPreviewContact {
        RuntimeObjectHandle target{};
        MATH::Vec3 positionWS{};
        MATH::Vec3 normalWS{0.0f, 1.0f, 0.0f};
        TERRAIN::TerrainInteractionFootprint footprint{};
        float normalLoadN = 0.0f;
        float penetrationDepthWS = 0.0f;
        float contactAgeSeconds = 0.0f;
        bool submitted = false;
    };

    void CapturePhysicsSources(World& world);
    void ProcessPhysicsContacts(World& world, const FrameContext& frame);
    void PublishRigidContacts(World& world, const FrameContext& frame);
    void PublishCharacterSources(World& world, const FrameContext& frame);
    void PublishFollowerSources(World& world, const FrameContext& frame);
    void PublishEditorPreviewSources(World& world, const FrameContext& frame);
    void PublishSweptInteraction(World& world, const FrameContext& frame,
                                 const TERRAIN::TerrainInteractionSourceKey& source, RuntimeObjectHandle targetSurface,
                                 TERRAIN::TerrainInteractionPhase phase,
                                 TERRAIN::TerrainInteractionSourceKind sourceKind, const MATH::Vec3& positionWS,
                                 const TERRAIN::TerrainInteractionFootprint& footprint, float normalLoadN,
                                 const MATH::Vec3& normalWS = {0.0f, 1.0f, 0.0f}, float penetrationDepthWS = 0.0f,
                                 float normalImpulseNs = 0.0f, float strength = 1.0f, float contactAgeSeconds = 0.0f,
                                 uint64_t sourceShapeId = 0u, uint64_t definitionSignature = 0u);
    void PublishSweptInteraction(WorldEventStream& events, float stepSeconds, uint64_t scopeIndex,
                                 const TERRAIN::TerrainInteractionSourceKey& source, RuntimeObjectHandle targetSurface,
                                 TERRAIN::TerrainInteractionPhase phase,
                                 TERRAIN::TerrainInteractionSourceKind sourceKind, const MATH::Vec3& positionWS,
                                 const TERRAIN::TerrainInteractionFootprint& footprint, float normalLoadN,
                                 const MATH::Vec3& normalWS, float penetrationDepthWS, float normalImpulseNs,
                                 float strength, float contactAgeSeconds, uint64_t sourceShapeId = 0u,
                                 uint64_t definitionSignature = 0u);

    const PHYSICS::KinematicMotionService* kinematicMotionService_ = nullptr;
    PHYSICS::PhysicsRuntimeStatusService* runtimeStatus_ = nullptr;
    PHYSICS::PhysicsWorldService* physicsWorld_ = nullptr;
    const RuntimePlayStateService* runtimePlayState_ = nullptr;
    const RuntimeObjectActivityService* runtimeActivityService_ = nullptr;
    std::unordered_map<ContactKey, ActiveRigidContact, ContactKeyHash> activeRigidContacts_{};
    std::unordered_map<ContactKey, MATH::Vec3, ContactKeyHash> previousCenters_{};
    std::unordered_map<TERRAIN::TerrainInteractionSourceKey, ColliderObservation, SourceKeyHash>
        preFixedPhysicsSources_{};
    std::unordered_map<TERRAIN::TerrainInteractionSourceKey, ActiveCharacterContact, SourceKeyHash>
        activeCharacterContacts_{};
    std::unordered_map<TERRAIN::TerrainInteractionSourceKey, ActiveFollowerContact, SourceKeyHash>
        activeFollowerTargets_{};
    std::unordered_map<TERRAIN::TerrainInteractionSourceKey, EditorPreviewContact, SourceKeyHash>
        editorPreviewContacts_{};
};

} // namespace HIKARI
