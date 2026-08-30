#include "Scene/HIKARI_WorldMassStore.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "HIKARI/Project/MassSystem.h"

namespace HIKARI {

namespace INTERNAL {

    bool IsFiniteWorldMassVector(
        const GameProjectFloat3& value) noexcept {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    bool IsValidWorldMassTransform(
        const GameProjectFloat3& position,
        const GameProjectQuaternion& rotation,
        const GameProjectFloat3& scale) noexcept {
        const bool finiteRotation =
            std::isfinite(rotation.x) &&
            std::isfinite(rotation.y) &&
            std::isfinite(rotation.z) &&
            std::isfinite(rotation.w);
        const double rotationLengthSquared =
            static_cast<double>(rotation.x) * rotation.x +
            static_cast<double>(rotation.y) * rotation.y +
            static_cast<double>(rotation.z) * rotation.z +
            static_cast<double>(rotation.w) * rotation.w;
        return IsFiniteWorldMassVector(position) &&
            IsFiniteWorldMassVector(scale) &&
            finiteRotation &&
            rotationLengthSquared > 1.0e-12;
    }

} // namespace INTERNAL

namespace {

    constexpr uint32_t kMassSchedulingFieldMask =
        static_cast<uint32_t>(GameProjectMassSystemField::Transform) |
        static_cast<uint32_t>(GameProjectMassSystemField::Velocity) |
        static_cast<uint32_t>(GameProjectMassSystemField::ProjectData);

} // namespace

    void WorldMassStore::AdvanceMutationVersion(Store& store) noexcept {
        ++store.mutationVersion;
        if (store.mutationVersion == 0u) {
            store.mutationVersion = 1u;
        }
    }

    bool WorldMassStore::VisitMutable(
        ComponentTypeId typeId,
        void* context,
        MutableVisitorFn visitor) noexcept {
        Store* store = FindStore(typeId);
        if (store == nullptr) {
            return true;
        }
        if (visitor == nullptr) {
            return false;
        }
        bool completed = true;
        bool mutationChanged = false;
        bool renderChanged = false;
        std::array<std::byte, kWorldMassMaximumPodBytes> beforePod;
        for (size_t rowIndex = 0u;
            rowIndex < store->objects.size();
            ++rowIndex) {
            const WorldMassRenderInstance before =
                store->hasRenderProxy
                ? store->renderInstances[rowIndex]
                : WorldMassRenderInstance{};
            const GameProjectFloat3 beforePosition =
                store->positions[rowIndex];
            const GameProjectQuaternion beforeRotation =
                store->rotations[rowIndex];
            const GameProjectFloat3 beforeScale =
                store->scales[rowIndex];
            const GameProjectFloat3 beforeVelocity =
                store->velocities[rowIndex];
            std::memcpy(
                beforePod.data(),
                store->podBytes.data() + rowIndex * store->podStride,
                store->podStride);
            WorldMassMutableRow row{
                store->objects[rowIndex],
                &store->positions[rowIndex],
                &store->rotations[rowIndex],
                &store->scales[rowIndex],
                &store->velocities[rowIndex],
                store->podBytes.data() + rowIndex * store->podStride,
                store->schema.podSize,
                store->schema.podAlignment
            };
            const bool continueVisit = visitor(context, rowIndex, row);
            if (!INTERNAL::IsValidWorldMassTransform(
                    store->positions[rowIndex],
                    store->rotations[rowIndex],
                    store->scales[rowIndex]) ||
                !INTERNAL::IsFiniteWorldMassVector(
                    store->velocities[rowIndex])) {
                store->positions[rowIndex] = beforePosition;
                store->rotations[rowIndex] = beforeRotation;
                store->scales[rowIndex] = beforeScale;
                store->velocities[rowIndex] = beforeVelocity;
                std::memcpy(
                    store->podBytes.data() +
                        rowIndex * store->podStride,
                    beforePod.data(),
                    store->podStride);
                completed = false;
                break;
            }
            mutationChanged = mutationChanged ||
                std::memcmp(
                    &beforePosition,
                    &store->positions[rowIndex],
                    sizeof(beforePosition)) != 0 ||
                std::memcmp(
                    &beforeRotation,
                    &store->rotations[rowIndex],
                    sizeof(beforeRotation)) != 0 ||
                std::memcmp(
                    &beforeScale,
                    &store->scales[rowIndex],
                    sizeof(beforeScale)) != 0 ||
                std::memcmp(
                    &beforeVelocity,
                    &store->velocities[rowIndex],
                    sizeof(beforeVelocity)) != 0 ||
                std::memcmp(
                    beforePod.data(),
                    store->podBytes.data() +
                        rowIndex * store->podStride,
                    store->podStride) != 0;
            if (store->hasRenderProxy && HasChangedTransform(
                    store->positions[rowIndex],
                    store->rotations[rowIndex],
                    store->scales[rowIndex],
                    before)) {
                store->renderInstances[rowIndex] = BuildRenderInstance(
                    store->objects[rowIndex],
                    store->positions[rowIndex],
                    store->rotations[rowIndex],
                    store->scales[rowIndex]);
                MarkRenderDirty(*store, rowIndex);
                renderChanged = true;
            }
            if (!continueVisit) {
                completed = false;
                break;
            }
        }
        if (renderChanged) {
            AdvanceRenderSourceVersion(*store);
        }
        if (mutationChanged) {
            AdvanceMutationVersion(*store);
        }
        return completed;
    }

    bool WorldMassStore::GetSchedulingSnapshot(
        ComponentTypeId typeId,
        WorldMassSchedulingSnapshotView& outView) const noexcept {
        outView = {};
        const Store* store = FindStore(typeId);
        if (store == nullptr || store->mutationVersion == 0u ||
            store->objects.size() != store->positions.size() ||
            store->objects.size() != store->rotations.size() ||
            store->objects.size() != store->scales.size() ||
            store->objects.size() != store->velocities.size() ||
            (store->podStride != 0u &&
                store->objects.size() >
                    (std::numeric_limits<size_t>::max)() /
                        store->podStride) ||
            store->podBytes.size() !=
                store->objects.size() * store->podStride) {
            return false;
        }
        outView.schema = store->schema;
        outView.podStride = store->podStride;
        outView.mutationVersion = store->mutationVersion;
        outView.objects = store->objects;
        outView.positions = store->positions;
        outView.rotations = store->rotations;
        outView.scales = store->scales;
        outView.velocities = store->velocities;
        outView.podBytes = store->podBytes;
        return true;
    }

    bool WorldMassStore::ApplySchedulingOutputs(
        ComponentTypeId typeId,
        uint64_t expectedMutationVersion,
        std::span<const WorldMassSchedulingOutputView> outputs) noexcept {
        Store* store = FindStore(typeId);
        if (store == nullptr || expectedMutationVersion == 0u ||
            store->mutationVersion != expectedMutationVersion) {
            return false;
        }

        size_t previousEnd = 0u;
        for (const WorldMassSchedulingOutputView& output : outputs) {
            const size_t count = output.expectedObjects.size();
            if (count == 0u || output.firstRow < previousEnd ||
                output.firstRow > store->objects.size() ||
                count > store->objects.size() - output.firstRow ||
                output.writeMask == 0u ||
                (output.writeMask & ~kMassSchedulingFieldMask) != 0u) {
                return false;
            }
            const bool writesTransform = (output.writeMask &
                static_cast<uint32_t>(
                    GameProjectMassSystemField::Transform)) != 0u;
            const bool writesVelocity = (output.writeMask &
                static_cast<uint32_t>(
                    GameProjectMassSystemField::Velocity)) != 0u;
            const bool writesProjectData = (output.writeMask &
                static_cast<uint32_t>(
                    GameProjectMassSystemField::ProjectData)) != 0u;
            if ((writesTransform &&
                    (output.positions.size() != count ||
                     output.rotations.size() != count ||
                     output.scales.size() != count)) ||
                (!writesTransform &&
                    (!output.positions.empty() ||
                     !output.rotations.empty() ||
                     !output.scales.empty())) ||
                (writesVelocity && output.velocities.size() != count) ||
                (!writesVelocity && !output.velocities.empty()) ||
                (writesProjectData &&
                    (store->podStride == 0u ||
                     count > (std::numeric_limits<size_t>::max)() /
                        store->podStride ||
                     output.podBytes.size() != count * store->podStride)) ||
                (!writesProjectData && !output.podBytes.empty())) {
                return false;
            }
            for (size_t localRow = 0u; localRow < count; ++localRow) {
                if (store->objects[output.firstRow + localRow] !=
                    output.expectedObjects[localRow]) {
                    return false;
                }
                if (writesTransform &&
                    !INTERNAL::IsValidWorldMassTransform(
                        output.positions[localRow],
                        output.rotations[localRow],
                        output.scales[localRow])) {
                    return false;
                }
                if (writesVelocity &&
                    !INTERNAL::IsFiniteWorldMassVector(
                        output.velocities[localRow])) {
                    return false;
                }
            }
            previousEnd = output.firstRow + count;
        }

        bool dataChanged = false;
        bool renderChanged = false;
        for (const WorldMassSchedulingOutputView& output : outputs) {
            const size_t count = output.expectedObjects.size();
            const bool writesTransform = (output.writeMask &
                static_cast<uint32_t>(
                    GameProjectMassSystemField::Transform)) != 0u;
            const bool writesVelocity = (output.writeMask &
                static_cast<uint32_t>(
                    GameProjectMassSystemField::Velocity)) != 0u;
            const bool writesProjectData = (output.writeMask &
                static_cast<uint32_t>(
                    GameProjectMassSystemField::ProjectData)) != 0u;
            for (size_t localRow = 0u; localRow < count; ++localRow) {
                const size_t row = output.firstRow + localRow;
                if (writesTransform) {
                    const bool transformChanged =
                        std::memcmp(
                            &store->positions[row],
                            &output.positions[localRow],
                            sizeof(GameProjectFloat3)) != 0 ||
                        std::memcmp(
                            &store->rotations[row],
                            &output.rotations[localRow],
                            sizeof(GameProjectQuaternion)) != 0 ||
                        std::memcmp(
                            &store->scales[row],
                            &output.scales[localRow],
                            sizeof(GameProjectFloat3)) != 0;
                    if (transformChanged) {
                        if (store->hasRenderProxy) {
                            store->renderInstances[row] = BuildRenderInstance(
                                store->objects[row],
                                output.positions[localRow],
                                output.rotations[localRow],
                                output.scales[localRow]);
                            MarkRenderDirty(*store, row);
                            renderChanged = true;
                        }
                        store->positions[row] = output.positions[localRow];
                        store->rotations[row] = output.rotations[localRow];
                        store->scales[row] = output.scales[localRow];
                        dataChanged = true;
                    }
                }
                if (writesVelocity) {
                    if (std::memcmp(
                            &store->velocities[row],
                            &output.velocities[localRow],
                            sizeof(GameProjectFloat3)) != 0) {
                        store->velocities[row] = output.velocities[localRow];
                        dataChanged = true;
                    }
                }
                if (writesProjectData) {
                    std::byte* destination =
                        store->podBytes.data() + row * store->podStride;
                    const std::byte* source =
                        output.podBytes.data() +
                            localRow * store->podStride;
                    if (std::memcmp(
                            destination,
                            source,
                            store->podStride) != 0) {
                        std::memcpy(
                            destination,
                            source,
                            store->podStride);
                        dataChanged = true;
                    }
                }
            }
        }
        if (dataChanged) {
            if (renderChanged) {
                AdvanceRenderSourceVersion(*store);
            }
            AdvanceMutationVersion(*store);
        }
        return true;
    }

} // namespace HIKARI
