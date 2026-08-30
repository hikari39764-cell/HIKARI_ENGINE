#include "Render3D/Pipeline/HIKARI_RenderFrameGraphSession.h"

#include <array>
#include <mutex>
#include <thread>

#include "Render3D/Graph/HIKARI_RenderGraph.h"
#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::PIPELINE {
    namespace {
        inline constexpr uint32_t kMaximumSessionCount =
            SURFACE::kRenderFrameSurfaceMaximumResidentViewCount;

        struct SessionSlot {
            GRAPH::RenderGraph graph{};
            INTERNAL::StageExecutionState stageState{};
            RenderFrameGraphSessionReceipt receipt{};
            bool active = false;
        };

        struct SessionState {
            std::mutex mutex{};
            std::array<SessionSlot, kMaximumSessionCount> slots{};
            uint64_t nextGeneration = 1u;
            std::thread::id ownerThread{};
            bool ownerThreadAssigned = false;
        };

        SessionState& State() {
            static SessionState state{};
            return state;
        }

        uint64_t AllocateGeneration(SessionState& state) noexcept {
            uint64_t generation = state.nextGeneration++;
            if (generation == 0u) {
                generation = state.nextGeneration++;
            }
            return generation;
        }

        bool CheckOwnerThread(
            SessionState& state,
            bool assignIfNeeded) noexcept {
            const std::thread::id current = std::this_thread::get_id();
            if (!state.ownerThreadAssigned) {
                if (!assignIfNeeded) {
                    return false;
                }
                state.ownerThread = current;
                state.ownerThreadAssigned = true;
                return true;
            }
            return state.ownerThread == current;
        }

        SessionSlot* FindSlot(
            SessionState& state,
            RenderFrameGraphSessionReceipt receipt) noexcept {
            if (!receipt.IsValid()) {
                return nullptr;
            }
            for (SessionSlot& slot : state.slots) {
                if (slot.active && slot.receipt.Matches(receipt)) {
                    return &slot;
                }
            }
            return nullptr;
        }

        bool CloseSlot(SessionSlot& slot, bool abort) noexcept {
            const auto status = slot.graph.GetExecutionState().status;
            if (abort) {
                if ((status ==
                        GRAPH::RenderGraphExecutionStatus::NotStarted ||
                     status ==
                        GRAPH::RenderGraphExecutionStatus::Paused) &&
                    !slot.graph.AbortExecution()) {
                    return false;
                }
            }
            if (!abort &&
                status != GRAPH::RenderGraphExecutionStatus::Completed) {
                return false;
            }
            slot.stageState = {};
            slot.receipt = {};
            slot.active = false;
            return true;
        }

        SessionSlot* BorrowLiveSlot(
            RenderFrameGraphSessionReceipt receipt) noexcept {
            if (SURFACE::TryGetRenderFrameSurfaces(
                    receipt.surfaces) == nullptr) {
                return nullptr;
            }
            SessionState& state = State();
            const std::lock_guard lock(state.mutex);
            return CheckOwnerThread(state, false)
                ? FindSlot(state, receipt)
                : nullptr;
        }
    }

    bool BeginRenderFrameGraphSession(
        SURFACE::RenderFrameSurfaceReceipt surfaces,
        RenderFrameGraphSessionReceipt& outReceipt) noexcept {
        outReceipt = {};
        SURFACE::SceneSurfaceSet* surfaceSet =
            SURFACE::TryGetRenderFrameSurfaces(surfaces);
        if (surfaceSet == nullptr) {
            return false;
        }
        SessionState& state = State();
        SessionSlot* selected = nullptr;
        {
            const std::lock_guard lock(state.mutex);
            if (!CheckOwnerThread(state, true)) {
                return false;
            }
            for (SessionSlot& slot : state.slots) {
                if (slot.active &&
                    slot.receipt.surfaces.frame.viewId ==
                        surfaces.frame.viewId) {
                    return false;
                }
                if (selected == nullptr && !slot.active) {
                    selected = &slot;
                }
            }
        }
        if (selected == nullptr ||
            !selected->graph.BeginFrame(*surfaceSet, surfaces)) {
            return false;
        }
        const std::lock_guard lock(state.mutex);
        if (!CheckOwnerThread(state, false) || selected->active) {
            return false;
        }
        const RenderFrameGraphSessionReceipt receipt{
            surfaces,
            AllocateGeneration(state)
        };
        selected->stageState = {};
        selected->receipt = receipt;
        selected->active = true;
        outReceipt = receipt;
        return true;
    }

    bool IsRenderFrameGraphSessionCurrent(
        RenderFrameGraphSessionReceipt receipt) noexcept {
        return BorrowLiveSlot(receipt) != nullptr;
    }

    bool ContinueRenderFrameGraphSession(
        RenderFrameGraphSessionReceipt receipt) noexcept {
        SessionSlot* slot = BorrowLiveSlot(receipt);
        return slot != nullptr &&
            slot->stageState.context == nullptr &&
            slot->graph.ContinueExecute() &&
            slot->graph.GetExecutionState().status ==
                GRAPH::RenderGraphExecutionStatus::Completed &&
            slot->stageState.presentationResumed;
    }

    bool EndRenderFrameGraphSession(
        RenderFrameGraphSessionReceipt receipt) noexcept {
        if (SURFACE::TryGetRenderFrameSurfaces(
                receipt.surfaces) == nullptr) {
            return false;
        }
        SessionState& state = State();
        const std::lock_guard lock(state.mutex);
        if (!CheckOwnerThread(state, false)) {
            return false;
        }
        SessionSlot* slot = FindSlot(state, receipt);
        return slot != nullptr &&
            slot->stageState.context == nullptr &&
            slot->stageState.presentationResumed &&
            CloseSlot(*slot, false);
    }

    bool AbortRenderFrameGraphSession(
        RenderFrameGraphSessionReceipt receipt) noexcept {
        SessionState& state = State();
        const std::lock_guard lock(state.mutex);
        if (!CheckOwnerThread(state, false)) {
            return false;
        }
        SessionSlot* slot = FindSlot(state, receipt);
        return slot != nullptr && CloseSlot(*slot, true);
    }

    bool ShutdownRenderFrameGraphSessions() noexcept {
        SessionState& state = State();
        const std::lock_guard lock(state.mutex);
        if (!state.ownerThreadAssigned) {
            return true;
        }
        if (!CheckOwnerThread(state, false)) {
            return false;
        }
        for (SessionSlot& slot : state.slots) {
            if (!slot.active) {
                continue;
            }
            if (!CloseSlot(slot, true)) {
                return false;
            }
        }
        state.ownerThread = {};
        state.ownerThreadAssigned = false;
        return true;
    }

} // namespace HIKARI::RENDER3D::PIPELINE

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

    GRAPH::RenderGraph* TryGetRenderFrameGraph(
        RenderFrameGraphSessionReceipt receipt) noexcept {
        SessionSlot* slot = BorrowLiveSlot(receipt);
        return slot != nullptr ? &slot->graph : nullptr;
    }

    StageExecutionState* TryGetRenderFrameGraphStageState(
        RenderFrameGraphSessionReceipt receipt) noexcept {
        SessionSlot* slot = BorrowLiveSlot(receipt);
        return slot != nullptr ? &slot->stageState : nullptr;
    }

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
