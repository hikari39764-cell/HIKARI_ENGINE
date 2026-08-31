#include "Scene/HIKARI_SystemScheduler.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

#include "Core/HIKARI_FrameContext.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Tasks/HIKARI_TaskExecutor.h"
#include "HIKARI/Project/MassSystem.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

struct SystemScheduler::ParallelRuntime {
    struct Scratch;

    struct Partition {
        Scratch* scratch = nullptr;
        const IMassWorkerSystem* system = nullptr;
        uint32_t firstRow = 0u;
        uint32_t rowCount = 0u;
        std::vector<uint32_t> destroyRows{};
        bool commandRejected = false;
        bool succeeded = false;
    };

    struct Scratch {
        FrameContext frame{};
        GameProjectMassSystemScheduleDesc schedule{};
        uint64_t worldIdentity = 0u;
        uint64_t mutationVersion = 0u;
        uint64_t layoutHash = 0u;
        uint32_t schemaVersion = 0u;
        uint32_t podStride = 0u;
        uint32_t podSize = 0u;
        uint32_t podAlignment = 0u;
        std::vector<RuntimeObjectHandle> runtimeObjects{};
        std::vector<GameProjectObjectHandle> objects{};
        std::vector<GameProjectFloat3> inputPositions{};
        std::vector<GameProjectQuaternion> inputRotations{};
        std::vector<GameProjectFloat3> inputScales{};
        std::vector<GameProjectFloat3> inputVelocities{};
        std::vector<std::byte> inputPod{};
        std::vector<GameProjectFloat3> outputPositions{};
        std::vector<GameProjectQuaternion> outputRotations{};
        std::vector<GameProjectFloat3> outputScales{};
        std::vector<GameProjectFloat3> outputVelocities{};
        std::vector<std::byte> outputPod{};
        std::vector<Partition> partitions{};
        std::vector<TASKS::TaskExecutorWork> work{};
        std::vector<WorldMassSchedulingOutputView> outputs{};
    };

    std::unique_ptr<TASKS::TaskExecutor> executor{};
    std::unordered_map<const ISystem*, std::unique_ptr<Scratch>> scratch{};
};

namespace {

    bool HasMassField(
        uint32_t mask,
        GameProjectMassSystemField field) noexcept {
        return (mask & static_cast<uint32_t>(field)) != 0u;
    }

} // namespace

SystemScheduler::SystemScheduler()
    : ownerThread_(std::this_thread::get_id()) {}

SystemScheduler::~SystemScheduler() = default;

void SystemScheduler::WaitForParallelIdle() noexcept {
    if (parallelRuntime_ && parallelRuntime_->executor) {
        parallelRuntime_->executor->WaitIdle();
    }
}

void SystemScheduler::ReleaseParallelRuntime() noexcept {
    if (parallelRuntime_ && parallelRuntime_->executor) {
        parallelRuntime_->executor->StopAccepting();
        (void)parallelRuntime_->executor->WaitIdle();
    }
    parallelRuntime_.reset();
}

void SystemScheduler::DiscardParallelSystemScratch(
    const ISystem* system) noexcept {
    WaitForParallelIdle();
    if (parallelRuntime_) {
        parallelRuntime_->scratch.erase(system);
    }
}

void SystemScheduler::Clear() {
    if (!IsOwnerThread()) {
        return;
    }
    ReleaseParallelRuntime();
    systems_.clear();
    attachedWorldIdentity_ = 0u;
    nextInsertionIndex_ = 0u;
    parallelMassFailureCount_ = 0u;
    renderObserverFailureCount_ = 0u;
    renderPassFailureCount_ = 0u;
}

bool SystemScheduler::ExecuteParallelMassSystem(
    World& world,
    const FrameContext& frame,
    const SystemEntry& entry,
    const IMassWorkerSystem& system) {
    const GameProjectMassSystemScheduleDesc& schedule =
        system.GetMassSystemSchedule();
    WorldMassSchedulingSnapshotView source{};
    if (!world.IsOwnerThread() || world.worldClosing_ ||
        !IsValidParallelMassSystemSchedule(schedule)) {
        HIKARI_LOG_ERROR(
            "[SystemScheduler] Mass snapshot failed: " + entry.systemId);
        return false;
    }
    if (!world.massStore_.GetSchedulingSnapshot(
            schedule.access.typeId,
            source)) {
        if (world.FindMassSchema(schedule.access.typeId) != nullptr &&
            world.massStore_.GetCount(schedule.access.typeId) == 0u) {
            return true;
        }
        HIKARI_LOG_ERROR(
            "[SystemScheduler] Mass snapshot failed: " + entry.systemId);
        return false;
    }
    if (source.objects.empty()) {
        return true;
    }
    if (source.objects.size() >
        (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }
    if (!parallelRuntime_) {
        parallelRuntime_ = std::make_unique<ParallelRuntime>();
    }
    auto& owned = parallelRuntime_->scratch[entry.system.get()];
    if (!owned) {
        owned = std::make_unique<ParallelRuntime::Scratch>();
    }
    ParallelRuntime::Scratch& scratch = *owned;
    scratch.frame = frame;
    scratch.schedule = schedule;
    scratch.worldIdentity = world.GetRuntimeIdentity();
    scratch.mutationVersion = source.mutationVersion;
    scratch.layoutHash = source.schema.layoutHash;
    scratch.schemaVersion = source.schema.schemaVersion;
    scratch.podStride = static_cast<uint32_t>(source.podStride);
    scratch.podSize = source.schema.podSize;
    scratch.podAlignment = source.schema.podAlignment;
    scratch.runtimeObjects.assign(source.objects.begin(), source.objects.end());
    scratch.objects.resize(source.objects.size());
    for (size_t index = 0u; index < source.objects.size(); ++index) {
        scratch.objects[index] = GameProjectObjectHandle{
            GameProjectWorldHandle{ scratch.worldIdentity },
            source.objects[index].ToValue()
        };
    }

    const uint32_t readWriteMask =
        schedule.access.readMask | schedule.access.writeMask;
    const auto copyIf = []<class Value>(
        bool required,
        std::span<const Value> sourceValues,
        std::vector<Value>& destination) {
        if (required) {
            destination.assign(sourceValues.begin(), sourceValues.end());
        } else {
            destination.clear();
        }
    };
    copyIf(
        HasMassField(readWriteMask, GameProjectMassSystemField::Transform),
        source.positions,
        scratch.inputPositions);
    copyIf(
        HasMassField(readWriteMask, GameProjectMassSystemField::Transform),
        source.rotations,
        scratch.inputRotations);
    copyIf(
        HasMassField(readWriteMask, GameProjectMassSystemField::Transform),
        source.scales,
        scratch.inputScales);
    copyIf(
        HasMassField(readWriteMask, GameProjectMassSystemField::Velocity),
        source.velocities,
        scratch.inputVelocities);
    if (HasMassField(
            readWriteMask,
            GameProjectMassSystemField::ProjectData)) {
        scratch.inputPod.assign(
            source.podBytes.begin(),
            source.podBytes.end());
    } else {
        scratch.inputPod.clear();
    }
    copyIf(
        HasMassField(
            schedule.access.writeMask,
            GameProjectMassSystemField::Transform),
        source.positions,
        scratch.outputPositions);
    copyIf(
        HasMassField(
            schedule.access.writeMask,
            GameProjectMassSystemField::Transform),
        source.rotations,
        scratch.outputRotations);
    copyIf(
        HasMassField(
            schedule.access.writeMask,
            GameProjectMassSystemField::Transform),
        source.scales,
        scratch.outputScales);
    copyIf(
        HasMassField(
            schedule.access.writeMask,
            GameProjectMassSystemField::Velocity),
        source.velocities,
        scratch.outputVelocities);
    if (HasMassField(
            schedule.access.writeMask,
            GameProjectMassSystemField::ProjectData)) {
        scratch.outputPod = scratch.inputPod;
    } else {
        scratch.outputPod.clear();
    }

    const size_t partitionCount =
        (source.objects.size() + schedule.partitionSize - 1u) /
        schedule.partitionSize;
    scratch.partitions.resize(partitionCount);
    scratch.work.resize(partitionCount);
    for (size_t index = 0u; index < partitionCount; ++index) {
        ParallelRuntime::Partition& partition = scratch.partitions[index];
        partition.scratch = &scratch;
        partition.system = &system;
        partition.firstRow = static_cast<uint32_t>(
            index * schedule.partitionSize);
        partition.rowCount = static_cast<uint32_t>((std::min)(
            static_cast<size_t>(schedule.partitionSize),
            source.objects.size() - partition.firstRow));
        partition.destroyRows.clear();
        partition.destroyRows.reserve(
            schedule.maximumDestroyCommandsPerPartition);
        partition.commandRejected = false;
        partition.succeeded = false;
        scratch.work[index] = TASKS::TaskExecutorWork{
            [](void* context) noexcept {
                auto& task = *static_cast<ParallelRuntime::Partition*>(context);
                ParallelRuntime::Scratch& state = *task.scratch;
                const uint32_t first = task.firstRow;
                const uint32_t count = task.rowCount;
                const auto pointerAt = [first]<class Value>(
                    std::vector<Value>& values) -> Value* {
                    return values.empty() ? nullptr : values.data() + first;
                };
                GameProjectMassPartitionInput input{};
                input.world = GameProjectWorldHandle{ state.worldIdentity };
                input.typeId = state.schedule.access.typeId;
                input.layoutHash = state.layoutHash;
                input.schemaVersion = state.schemaVersion;
                input.firstRow = first;
                input.rowCount = count;
                input.podStride = state.podStride;
                input.podSize = state.podSize;
                input.podAlignment = state.podAlignment;
                input.readMask = state.schedule.access.readMask;
                input.writeMask = state.schedule.access.writeMask;
                input.objects = state.objects.data() + first;
                input.positions = pointerAt(state.inputPositions);
                input.rotations = pointerAt(state.inputRotations);
                input.scales = pointerAt(state.inputScales);
                input.velocities = pointerAt(state.inputVelocities);
                input.podData = state.inputPod.empty()
                    ? nullptr
                    : state.inputPod.data() +
                        static_cast<size_t>(first) * state.podStride;

                GameProjectMassPartitionOutput output{};
                output.firstRow = first;
                output.rowCount = count;
                output.writeMask = state.schedule.access.writeMask;
                output.positions = pointerAt(state.outputPositions);
                output.rotations = pointerAt(state.outputRotations);
                output.scales = pointerAt(state.outputScales);
                output.velocities = pointerAt(state.outputVelocities);
                output.podData = state.outputPod.empty()
                    ? nullptr
                    : state.outputPod.data() +
                        static_cast<size_t>(first) * state.podStride;
                output.podDataByteCount = state.outputPod.empty()
                    ? 0u
                    : static_cast<uint64_t>(count) * state.podStride;

                GameProjectMassCommandWriter commands{};
                commands.context = &task;
                commands.capacity =
                    state.schedule.maximumDestroyCommandsPerPartition;
                commands.destroyRow = commands.capacity == 0u
                    ? nullptr
                    : [](void* raw, uint32_t row) noexcept {
                    auto& current =
                        *static_cast<ParallelRuntime::Partition*>(raw);
                    if (row >= current.rowCount) {
                        current.commandRejected = true;
                        return GameProjectWorldResult{
                            GameProjectWorldResultCode::InvalidObject,
                            {},
                            0u
                        };
                    }
                    if (current.destroyRows.size() >=
                        current.scratch->schedule.
                            maximumDestroyCommandsPerPartition) {
                        current.commandRejected = true;
                        return GameProjectWorldResult{
                            GameProjectWorldResultCode::CommandQueueFull,
                            {},
                            0u
                        };
                    }
                    if (std::find(
                            current.destroyRows.begin(),
                            current.destroyRows.end(),
                            row) != current.destroyRows.end()) {
                        current.commandRejected = true;
                        return GameProjectWorldResult{
                            GameProjectWorldResultCode::InvalidRequest,
                            {},
                            0u
                        };
                    }
                    current.destroyRows.push_back(row);
                    return GameProjectWorldResult{
                        GameProjectWorldResultCode::Queued,
                        current.scratch->objects[
                            current.firstRow + row],
                        0u
                    };
                    };
                task.succeeded = task.system->ExecuteMassPartition(
                    input,
                    output,
                    commands,
                    state.frame);
            },
            &partition
        };
    }

    bool dispatched = true;
    if (scratch.work.size() == 1u) {
        scratch.work.front().execute(scratch.work.front().context);
    } else {
        if (!parallelRuntime_->executor) {
            parallelRuntime_->executor =
                std::make_unique<TASKS::TaskExecutor>();
        }
        dispatched = parallelRuntime_->executor->RunBatch(scratch.work);
    }
    if (!dispatched ||
        std::any_of(
            scratch.partitions.begin(),
            scratch.partitions.end(),
            [](const ParallelRuntime::Partition& partition) {
                return !partition.succeeded || partition.commandRejected;
            })) {
        HIKARI_LOG_ERROR(
            "[SystemScheduler] Mass partition failed: " + entry.systemId);
        return false;
    }

    scratch.outputs.clear();
    if (schedule.access.writeMask != 0u) {
        scratch.outputs.reserve(scratch.partitions.size());
        for (const ParallelRuntime::Partition& partition :
                scratch.partitions) {
            const size_t first = partition.firstRow;
            const size_t count = partition.rowCount;
            WorldMassSchedulingOutputView output{};
            output.firstRow = first;
            output.expectedObjects = std::span{
                scratch.runtimeObjects.data() + first,
                count
            };
            output.writeMask = schedule.access.writeMask;
            if (HasMassField(
                    output.writeMask,
                    GameProjectMassSystemField::Transform)) {
                output.positions = std::span{
                    scratch.outputPositions.data() + first,
                    count
                };
                output.rotations = std::span{
                    scratch.outputRotations.data() + first,
                    count
                };
                output.scales = std::span{
                    scratch.outputScales.data() + first,
                    count
                };
            }
            if (HasMassField(
                    output.writeMask,
                    GameProjectMassSystemField::Velocity)) {
                output.velocities = std::span{
                    scratch.outputVelocities.data() + first,
                    count
                };
            }
            if (HasMassField(
                    output.writeMask,
                    GameProjectMassSystemField::ProjectData)) {
                output.podBytes = std::span{
                    scratch.outputPod.data() + first * scratch.podStride,
                    count * scratch.podStride
                };
            }
            scratch.outputs.push_back(output);
        }
    }

    size_t destroyCount = 0u;
    for (const ParallelRuntime::Partition& partition : scratch.partitions) {
        destroyCount += partition.destroyRows.size();
    }
    const World::WorldCommandCheckpoint checkpoint{
        world.pendingCommands_.size(),
        world.reservationStateJournal_.size()
    };
    bool commandsAccepted = true;
    if (destroyCount != 0u) {
        const World::StructureReadScope structureRead =
            world.AcquireStructureReadScope();
        for (const ParallelRuntime::Partition& partition :
                scratch.partitions) {
            for (const uint32_t localRow : partition.destroyRows) {
                const RuntimeObjectHandle object = scratch.runtimeObjects[
                    partition.firstRow + localRow];
                const WorldCommandSubmission submitted =
                    world.SubmitDestroy(object);
                if (submitted.result != WorldMutationResult::Queued) {
                    commandsAccepted = false;
                    break;
                }
            }
            if (!commandsAccepted) {
                break;
            }
        }
    }
    if (!commandsAccepted) {
        world.RollbackCommandCheckpoint(checkpoint);
        HIKARI_LOG_ERROR(
            "[SystemScheduler] Mass commands rejected: " + entry.systemId);
        return false;
    }
    if (!scratch.outputs.empty() &&
        !world.massStore_.ApplySchedulingOutputs(
            schedule.access.typeId,
            scratch.mutationVersion,
            scratch.outputs)) {
        world.RollbackCommandCheckpoint(checkpoint);
        HIKARI_LOG_ERROR(
            "[SystemScheduler] Mass merge rejected: " + entry.systemId);
        return false;
    }
    return true;
}

} // namespace HIKARI
