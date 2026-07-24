#include "Physics/HIKARI_PhysicsSystem.h"

#include <bit>
#include <string>
#include <utility>
#include <vector>

#include "Physics/HIKARI_KinematicMotionTypes.h"
#include "Physics/HIKARI_PhysicsWorldService.h"

namespace HIKARI::PHYSICS {
    namespace {
        void HashValue(uint64_t& seed, uint64_t value) noexcept {
            seed ^= value + 0x9E3779B97F4A7C15ull +
                (seed << 6u) + (seed >> 2u);
        }

        void HashFloat(uint64_t& seed, float value) noexcept {
            HashValue(seed, std::bit_cast<uint32_t>(value));
        }

        uint64_t ComputeSolverSignature(
            uint64_t bodyDefinitionSignature,
            const KinematicControllerSettings& settings) noexcept {
            uint64_t result = bodyDefinitionSignature;
            HashFloat(result, settings.maximumSlopeAngleRadians);
            HashFloat(result, settings.characterPadding);
            HashFloat(result, settings.predictiveContactDistance);
            HashFloat(result, settings.penetrationRecoverySpeed);
            HashValue(result, settings.maximumCollisionHits);
            HashValue(result, settings.enhancedInternalEdgeRemoval);
            return result;
        }

        bool BuildSolverShapes(
            const std::vector<PhysicsShapeDesc>& source,
            std::vector<PhysicsShapeDesc>& outShapes,
            PhysicsCollisionFilter& outFilter,
            std::string& outError) {
            outShapes.clear();
            bool hasFilter = false;
            for (const PhysicsShapeDesc& shape : source) {
                if (shape.isTrigger) {
                    continue;
                }
                if (shape.type == PhysicsShapeType::TriangleMesh) {
                    outError =
                        "kinematic motion does not support moving triangle meshes";
                    return false;
                }
                if (!hasFilter) {
                    outFilter = shape.filter;
                    hasFilter = true;
                } else if (outFilter.layer != shape.filter.layer ||
                    outFilter.mask != shape.filter.mask) {
                    outError =
                        "solid colliders used for kinematic motion must share one collision filter";
                    return false;
                }
                outShapes.push_back(shape);
            }
            if (outShapes.empty()) {
                outError =
                    "kinematic motion requires at least one solid collider";
                return false;
            }
            return true;
        }
    }

    void PhysicsSystem::DestroyKinematicSolver(
        BodyBinding& binding) noexcept {
        if (service_ != nullptr && binding.kinematicSolver.IsValid()) {
            (void)service_->DestroyCharacter(
                binding.kinematicSolver);
        }
        binding.kinematicSolver = {};
        binding.kinematicSolverSignature = 0u;
        binding.kinematicJumpActive = false;
    }

    bool PhysicsSystem::EnsureKinematicSolver(
        BodyBinding& binding,
        const KinematicControllerSettings& settings,
        const PhysicsBodyState& bodyState,
        bool& outRetainedPrevious,
        std::string& outError) {
        outRetainedPrevious = false;
        outError.clear();

        std::vector<PhysicsShapeDesc> solverShapes{};
        PhysicsCollisionFilter solverFilter{};
        if (!BuildSolverShapes(
                binding.shapes,
                solverShapes,
                solverFilter,
                outError)) {
            return false;
        }

        const uint64_t solverSignature = ComputeSolverSignature(
            binding.definitionSignature,
            settings);
        if (binding.kinematicSolver.IsValid() &&
            binding.kinematicSolverSignature == solverSignature) {
            return true;
        }

        PhysicsCharacterCreateInfo createInfo{};
        createInfo.character.object = binding.object;
        createInfo.character.shapes = std::move(solverShapes);
        createInfo.character.mass = binding.bodyDesc.mass;
        createInfo.character.maxSlopeAngleRadians =
            settings.maximumSlopeAngleRadians;
        createInfo.character.characterPadding =
            settings.characterPadding;
        createInfo.character.predictiveContactDistance =
            settings.predictiveContactDistance;
        createInfo.character.penetrationRecoverySpeed =
            settings.penetrationRecoverySpeed;
        createInfo.character.maxCollisionHits =
            settings.maximumCollisionHits;
        createInfo.character.enhancedInternalEdgeRemoval =
            settings.enhancedInternalEdgeRemoval;
        createInfo.character.filter = solverFilter;
        createInfo.initialPose = bodyState.pose;
        createInfo.initialLinearVelocity = bodyState.linearVelocity;

        const PhysicsCharacterCreateResult created =
            service_->CreateCharacter(createInfo);
        if (!created.Succeeded()) {
            if (binding.kinematicSolver.IsValid()) {
                outRetainedPrevious = true;
                return true;
            }
            outError = created.message.empty()
                ? "unable to create the kinematic motion solver"
                : created.message;
            return false;
        }

        const PhysicsCharacterHandle previous =
            binding.kinematicSolver;
        binding.kinematicSolver = created.handle;
        binding.kinematicSolverSignature = solverSignature;
        binding.kinematicJumpActive = false;
        if (previous.IsValid()) {
            (void)service_->DestroyCharacter(previous);
        }
        return true;
    }

} // namespace HIKARI::PHYSICS
