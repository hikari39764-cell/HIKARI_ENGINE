#include "Scene/HIKARI_SystemScheduler.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "HIKARI/Project/MassSystem.h"
#include "HIKARI/Project/RenderFrame.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

namespace {

    bool MatchesMassSystemPhase(
        GameProjectMassSystemPhase schedulePhase,
        WorldCommandPhase phase) noexcept {
        switch (schedulePhase) {
        case GameProjectMassSystemPhase::PreUpdate:
            return phase == WorldCommandPhase::SystemPreUpdate;
        case GameProjectMassSystemPhase::PreFixedUpdate:
            return phase == WorldCommandPhase::SystemPreFixedUpdate;
        case GameProjectMassSystemPhase::FixedUpdate:
            return phase == WorldCommandPhase::SystemFixedUpdate;
        case GameProjectMassSystemPhase::PostFixedUpdate:
            return phase == WorldCommandPhase::SystemPostFixedUpdate;
        case GameProjectMassSystemPhase::Update:
            return phase == WorldCommandPhase::SystemUpdate;
        case GameProjectMassSystemPhase::LateUpdate:
            return phase == WorldCommandPhase::SystemLateUpdate;
        default:
            return false;
        }
    }

    template<size_t Count>
    bool HasNonZero(const uint32_t (&values)[Count]) noexcept {
        for (const uint32_t value : values) {
            if (value != 0u) {
                return true;
            }
        }
        return false;
    }

    bool IsFiniteRenderCamera(
        const GameProjectRenderCameraFrame& camera) noexcept {
        const auto finite = [](const float* values, size_t count) noexcept {
            for (size_t index = 0u; index < count; ++index) {
                if (!std::isfinite(values[index])) {
                    return false;
                }
            }
            return true;
        };
        return finite(camera.view, 16u) &&
            finite(camera.projection, 16u) &&
            finite(camera.viewProjection, 16u) &&
            finite(camera.inverseViewProjection, 16u) &&
            finite(camera.unjitteredViewProjection, 16u) &&
            finite(camera.previousViewProjection, 16u) &&
            finite(camera.position, 3u) &&
            finite(camera.previousPosition, 3u) &&
            finite(camera.jitterPixels, 2u) &&
            finite(camera.previousJitterPixels, 2u) &&
            std::isfinite(camera.nearZ) &&
            std::isfinite(camera.farZ) &&
            std::isfinite(camera.fovYRadians) &&
            std::isfinite(camera.aspect);
    }

} // namespace

bool SystemScheduler::IsOwnerThread() const noexcept {
    return std::this_thread::get_id() == ownerThread_;
}

bool SystemScheduler::AddSystem(
    std::string systemId,
    int executionOrder,
    std::unique_ptr<ISystem> system) {

    if (!IsOwnerThread() || systemId.empty() || !system ||
        HasSystem(systemId)) {
        return false;
    }

    const std::string frameTimingName =
        "System/" + systemId + "/Frame";
    const std::string fixedTimingName =
        "System/" + systemId + "/Fixed";

    systems_.push_back(SystemEntry{
        std::move(systemId),
        executionOrder,
        nextInsertionIndex_++,
        std::move(system),
        CPU_PROFILE::RegisterDynamicPass(frameTimingName),
        CPU_PROFILE::RegisterDynamicPass(fixedTimingName)
    });
    std::stable_sort(
        systems_.begin(),
        systems_.end(),
        [](const SystemEntry& lhs, const SystemEntry& rhs) {
            if (lhs.executionOrder != rhs.executionOrder) {
                return lhs.executionOrder < rhs.executionOrder;
            }
            return lhs.insertionIndex < rhs.insertionIndex;
        });
    return true;
}

bool SystemScheduler::HasSystem(std::string_view systemId) const {
    if (!IsOwnerThread()) {
        return false;
    }
    return std::any_of(
        systems_.begin(),
        systems_.end(),
        [systemId](const SystemEntry& entry) {
            return entry.systemId == systemId;
        });
}

size_t SystemScheduler::GetSystemCount() const {
    return IsOwnerThread() ? systems_.size() : 0u;
}

uint64_t SystemScheduler::GetParallelMassFailureCount() const noexcept {
    return IsOwnerThread() ? parallelMassFailureCount_ : 0u;
}

uint64_t SystemScheduler::GetRenderObserverFailureCount() const noexcept {
    return IsOwnerThread() ? renderObserverFailureCount_ : 0u;
}

std::vector<std::string> SystemScheduler::GetExecutionOrder() const {
    std::vector<std::string> order;
    if (!IsOwnerThread()) {
        return order;
    }
    order.reserve(systems_.size());
    for (const SystemEntry& entry : systems_) {
        order.push_back(entry.systemId);
    }
    return order;
}

bool SystemScheduler::ObserveRenderFrame(
    const GameProjectRenderFrameView& frame) noexcept {
    constexpr uint32_t kReadAccessMask =
        static_cast<uint32_t>(GameProjectRenderSurfaceAccess::Sampled) |
        static_cast<uint32_t>(GameProjectRenderSurfaceAccess::ReadDepth) |
        static_cast<uint32_t>(GameProjectRenderSurfaceAccess::ReadUav) |
        static_cast<uint32_t>(GameProjectRenderSurfaceAccess::CopySource);
    constexpr uint32_t kExposureCapability = static_cast<uint32_t>(
        GameProjectRenderCapability::Exposure);
    if (!IsOwnerThread()) {
        return false;
    }
    if (frame.structSize != kGameProjectRenderFrameViewV1Size ||
        frame.version != kGameProjectRenderFrameVersion ||
        !frame.frame.IsValid() ||
        frame.stage >= GameProjectRenderObserverStage::Count ||
        frame.purpose >= GameProjectRenderViewPurpose::Count ||
        frame.viewGeneration == 0u ||
        frame.worldIdentity == 0u ||
        frame.worldIdentity != attachedWorldIdentity_ ||
        frame.surfaceCount > kGameProjectRenderMaximumSurfaceCount ||
        (frame.surfaceCount != 0u && frame.surfaces == nullptr) ||
        frame.camera == nullptr ||
        frame.camera->structSize != kGameProjectRenderCameraFrameV1Size ||
        frame.camera->version != kGameProjectRenderFrameVersion ||
        frame.camera->renderWidth == 0u ||
        frame.camera->renderHeight == 0u ||
        frame.camera->outputWidth == 0u ||
        frame.camera->outputHeight == 0u ||
        frame.camera->reserved0 != 0u ||
        HasNonZero(frame.camera->reserved) ||
        frame.reserved0 != 0u ||
        HasNonZero(frame.reserved) ||
        !IsFiniteRenderCamera(*frame.camera) ||
        (frame.availableCapabilityMask &
            ~frame.supportedCapabilityMask) != 0u ||
        (frame.validCapabilityMask &
            ~frame.availableCapabilityMask) != 0u ||
        frame.exposureValid > 1u ||
        frame.exposureMode >= GameProjectRenderExposureMode::Count ||
        (frame.exposureValid != 0u &&
            (!std::isfinite(frame.exposureMultiplier) ||
             !std::isfinite(frame.preExposure) ||
             (frame.validCapabilityMask & kExposureCapability) == 0u)) ||
        (frame.exposureValid == 0u &&
            (frame.validCapabilityMask & kExposureCapability) != 0u)) {
        ++renderObserverFailureCount_;
        return false;
    }
    for (uint32_t index = 0u; index < frame.surfaceCount; ++index) {
        const GameProjectRenderSurfaceDesc& surface = frame.surfaces[index];
        if (surface.structSize != kGameProjectRenderSurfaceDescV1Size ||
            surface.version != kGameProjectRenderFrameVersion ||
            !surface.handle.IsValid() ||
            !surface.handle.frame.Matches(frame.frame) ||
            surface.handle.reserved0 != 0u ||
            surface.handle.reserved1 != 0u ||
            surface.kind >= GameProjectRenderSurfaceKind::Count ||
            surface.availability >=
                GameProjectRenderSurfaceAvailability::Count ||
            surface.format >= GameProjectRenderSurfaceFormat::Count ||
            surface.lifetime >= GameProjectRenderSurfaceLifetime::Count ||
            surface.fallback >= GameProjectRenderSurfaceFallback::Count ||
            surface.sampleable > 1u ||
            surface.reserved0 != 0u ||
            surface.width == 0u || surface.height == 0u ||
            surface.mipCount == 0u ||
            (surface.readAccessMask & ~kReadAccessMask) != 0u ||
            HasNonZero(surface.reserved)) {
            ++renderObserverFailureCount_;
            return false;
        }
    }
    for (const SystemEntry& entry : systems_) {
        const auto* observer =
            dynamic_cast<const IRenderFrameObserverSystem*>(
                entry.system.get());
        if (observer == nullptr) {
            continue;
        }
        const GameProjectRenderObserverDesc& desc =
            observer->GetRenderObserverDesc();
        if (desc.structSize == 0u) {
            continue;
        }
        if (!IsValidRenderObserverDesc(desc)) {
            ++renderObserverFailureCount_;
            continue;
        }
        if (desc.stage != frame.stage) {
            continue;
        }
        if (!observer->ObserveRenderFrame(frame)) {
            ++renderObserverFailureCount_;
            continue;
        }
    }
    return true;
}

void SystemScheduler::ExecuteFramePhase(
    World& world,
    const FrameContext& frame,
    WorldCommandPhase phase,
    FrameCallback callback,
    bool flushAfterEach) {
    if (!IsOwnerThread() || !world.IsOwnerThread()) {
        return;
    }
    const bool fixedPhase =
        phase == WorldCommandPhase::SystemPreFixedUpdate ||
        phase == WorldCommandPhase::SystemFixedUpdate ||
        phase == WorldCommandPhase::SystemPostFixedUpdate;
    for (const SystemEntry& entry : systems_) {
        CPU_PROFILE::ScopedDynamicCpuTimer timer(
            fixedPhase ? entry.fixedTiming : entry.frameTiming);
        const auto* massSystem =
            dynamic_cast<const IMassWorkerSystem*>(entry.system.get());
        if (massSystem != nullptr &&
            massSystem->GetMassSystemSchedule().access.typeId.IsValid() &&
            MatchesMassSystemPhase(
                massSystem->GetMassSystemSchedule().phase,
                phase)) {
            bool completed = false;
            try {
                completed = ExecuteParallelMassSystem(
                    world,
                    frame,
                    entry,
                    *massSystem);
            }
            catch (...) {
                ++parallelMassFailureCount_;
                DiscardParallelSystemScratch(entry.system.get());
                world.RecordParallelMassSystemFailure(phase);
                if (flushAfterEach) {
                    (void)world.FlushQueuedCommands(phase);
                }
                throw;
            }
            if (flushAfterEach) {
                (void)world.FlushQueuedCommands(phase);
            }
            if (!completed) {
                ++parallelMassFailureCount_;
                DiscardParallelSystemScratch(entry.system.get());
                world.RecordParallelMassSystemFailure(phase);
                // Do not let later systems observe a partially failed Mass
                // phase. Earlier systems remain committed at their barriers.
                return;
            }
            continue;
        }
        try {
            const World::StructureReadScope structureRead =
                world.AcquireStructureReadScope();
            (entry.system.get()->*callback)(world, frame);
        }
        catch (...) {
            (void)world.FlushQueuedCommands(phase);
            throw;
        }
        if (flushAfterEach) {
            (void)world.FlushQueuedCommands(phase);
        }
    }
}

void SystemScheduler::AttachWorld(World& world) {
    if (!IsOwnerThread() || !world.IsOwnerThread()) {
        return;
    }
    const uint64_t worldIdentity = world.GetRuntimeIdentity();
    if (worldIdentity == 0u ||
        (attachedWorldIdentity_ != 0u &&
            attachedWorldIdentity_ != worldIdentity)) {
        return;
    }
    attachedWorldIdentity_ = worldIdentity;
    for (const SystemEntry& entry : systems_) {
        try {
            const World::StructureReadScope structureRead =
                world.AcquireStructureReadScope();
            entry.system->OnWorldAttached(world);
        }
        catch (...) {
            (void)world.FlushQueuedCommands(
                WorldCommandPhase::SystemAttach);
            throw;
        }
        (void)world.FlushQueuedCommands(
            WorldCommandPhase::SystemAttach);
    }
}

void SystemScheduler::DetachWorld(World& world) {
    if (!IsOwnerThread() || !world.IsOwnerThread()) {
        return;
    }
    if (attachedWorldIdentity_ != world.GetRuntimeIdentity()) {
        return;
    }
    ReleaseParallelRuntime();
    for (const SystemEntry& entry : systems_) {
        try {
            const World::StructureReadScope structureRead =
                world.AcquireStructureReadScope();
            entry.system->OnWorldDetached(world);
        }
        catch (...) {
            (void)world.FlushQueuedCommands(
                WorldCommandPhase::SystemDetach);
            throw;
        }
        (void)world.FlushQueuedCommands(
            WorldCommandPhase::SystemDetach);
    }
    attachedWorldIdentity_ = 0u;
}

void SystemScheduler::PreUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemPreUpdate,
        &ISystem::PreUpdate,
        true);
}

void SystemScheduler::PreFixedUpdate(
    World& world,
    const FrameContext& frame) {

    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemPreFixedUpdate,
        &ISystem::PreFixedUpdate,
        true);
}

void SystemScheduler::FixedUpdate(
    World& world,
    const FrameContext& frame) {

    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemFixedUpdate,
        &ISystem::FixedUpdate,
        true);
}

void SystemScheduler::PostFixedUpdate(
    World& world,
    const FrameContext& frame) {

    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemPostFixedUpdate,
        &ISystem::PostFixedUpdate,
        true);
}

void SystemScheduler::Update(World& world, const FrameContext& frame) {
    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemUpdate,
        &ISystem::Update,
        true);
}

void SystemScheduler::LateUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemLateUpdate,
        &ISystem::LateUpdate,
        true);
}

void SystemScheduler::PreRender(World& world, const FrameContext& frame) {
    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemPreRender,
        &ISystem::PreRender,
        true);
}

void SystemScheduler::Render(World& world, const FrameContext& frame) {
    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemRender,
        &ISystem::Render,
        true);
}

void SystemScheduler::PostRender(World& world, const FrameContext& frame) {
    ExecuteFramePhase(
        world,
        frame,
        WorldCommandPhase::SystemPostRender,
        &ISystem::PostRender,
        true);
    (void)world.FlushQueuedCommands(
        WorldCommandPhase::SystemPostRender);
}

} // namespace HIKARI
