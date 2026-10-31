#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalPassScratch.h"

namespace {

using HIKARI::Camera3D;
using HIKARI::RENDER3D::RenderFrameToken;
using HIKARI::RENDER3D::RenderViewId;
using namespace HIKARI::RENDER3D::TEMPORAL;

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool NearlyEqual(float lhs, float rhs) { return std::abs(lhs - rhs) <= 1.0e-5f; }

RenderFrameToken Token(uint64_t frameIndex, uint64_t viewId, uint64_t generation) {
    return {frameIndex, RenderViewId{viewId}, generation};
}

TemporalFrameDesc Frame(TemporalViewLease view, RenderFrameToken token, const Camera3D& camera,
                        uint64_t historyConfigurationKey = 1u,
                        HIKARI::RENDER3D::RenderViewPurpose purpose = HIKARI::RENDER3D::RenderViewPurpose::Game) {
    TemporalFrameDesc desc{};
    desc.view = view;
    desc.frame = token;
    desc.purpose = purpose;
    desc.camera = &camera;
    desc.renderWidth = 1280;
    desc.renderHeight = 720;
    desc.outputWidth = 1920;
    desc.outputHeight = 1080;
    desc.temporalResolveAllowed = true;
    desc.jitterEnabled = true;
    desc.historyConfigurationKey = historyConfigurationKey;
    return desc;
}

} // namespace

int main() {
    using namespace HIKARI::RENDER3D::TEMPORAL;

    ShutdownTemporalFrameStateSystem();

    Camera3D cameraA{};
    cameraA.SetPerspective(Camera3D::kDefaultFovYRad, 16.0f / 9.0f, Camera3D::kDefaultNearZ, Camera3D::kDefaultFarZ);
    cameraA.SetLookAt({1.0f, 2.0f, -5.0f}, {0.0f, 1.0f, 0.0f});

    Camera3D cameraB{};
    cameraB.SetPerspective(Camera3D::kDefaultFovYRad, 16.0f / 9.0f, Camera3D::kDefaultNearZ, Camera3D::kDefaultFarZ);
    cameraB.SetLookAt({20.0f, 4.0f, -8.0f}, {20.0f, 1.0f, 0.0f});

    const RenderFrameToken a1 = Token(1u, 101u, 1u);
    const RenderFrameToken b1 = Token(1u, 202u, 2u);
    bool ok = true;
    const TemporalResourceFrame resourceA{TemporalViewLease{a1.viewId, 10u}, a1, 100u, 1000u};
    const TemporalResourceFrame resourceAExact = resourceA;
    TemporalResourceFrame resourceWrongGeneration = resourceA;
    resourceWrongGeneration.resourceGeneration++;
    TemporalResourceFrame resourceWrongDevice = resourceA;
    resourceWrongDevice.deviceGeneration++;
    TemporalResourceFrame resourceWrongView = resourceA;
    resourceWrongView.view.viewId = b1.viewId;
    ok =
        Check(resourceA.IsValid() && resourceA.Matches(resourceAExact) && !resourceA.Matches(resourceWrongGeneration) &&
                  !resourceA.Matches(resourceWrongDevice) && !resourceA.Matches(resourceWrongView),
              "Temporal resource receipt accepted a stale identity.") &&
        ok;
    TemporalResourceFrame resourceA4 = resourceA;
    resourceA4.publication = Token(4u, 101u, 4u);
    ok = Check(DETAIL::CanReuseTemporalPassSlot({}, 0u, resourceA, 0u) &&
                   !DETAIL::CanReuseTemporalPassSlot(resourceA, 7u, resourceA, 7u) &&
                   !DETAIL::CanReuseTemporalPassSlot(resourceA, 7u, resourceA4, 6u) &&
                   DETAIL::CanReuseTemporalPassSlot(resourceA, 7u, resourceA4, 7u),
               "Temporal pass scratch overwrote an exact or in-flight slot.") &&
         ok;
    TemporalViewLease viewA{};
    TemporalViewLease viewB{};
    ok = Check(AcquireTemporalView(a1.viewId, viewA), "Could not acquire View A temporal lease.") && ok;
    ok = Check(AcquireTemporalView(b1.viewId, viewB), "Could not acquire View B temporal lease.") && ok;
    const TemporalFrameDesc firstADesc = Frame(viewA, a1, cameraA);
    const TemporalFrameDesc firstBDesc = Frame(viewB, b1, cameraB);
    const TemporalFrameState firstA = BeginTemporalFrame(firstADesc);
    const TemporalFrameState firstB = BeginTemporalFrame(firstBDesc);
    ok = Check(firstA.resetReason == TemporalHistoryResetReason::FirstFrame,
               "View A did not begin with isolated first-frame history.") &&
         ok;
    ok = Check(firstB.resetReason == TemporalHistoryResetReason::FirstFrame, "View B inherited history from View A.") &&
         ok;
    ok = Check(GetTemporalFrameStateViewCount() == 2u, "Temporal frame state did not retain two independent views.") &&
         ok;
    const TemporalFrameState duplicateA = BeginTemporalFrame(firstADesc);
    ok = Check(duplicateA.publication.Matches(a1) && duplicateA.resetReason == firstA.resetReason,
               "An exact duplicate Begin was not idempotent.") &&
         ok;
    TemporalFrameDesc sameFrameA = firstADesc;
    sameFrameA.frame = Token(1u, 101u, 1000u);
    const TemporalFrameState rejectedSameFrameA = BeginTemporalFrame(sameFrameA);
    ok = Check(rejectedSameFrameA.resetReason == TemporalHistoryResetReason::InvalidFrameToken &&
                   GetLatestTemporalFrameState(a1.viewId).publication.Matches(a1),
               "A second token for the same view/frame replaced the publication.") &&
         ok;
    const TemporalViewLease forgedViewA{viewA.viewId, viewA.generation + 1000u};
    ok = Check(!CompleteTemporalFrame(forgedViewA, a1, true) && !ReleaseTemporalFrameViewState(forgedViewA),
               "A forged temporal view lease mutated active state.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewA, a1, true), "View A history commit was rejected.") && ok;
    ok = Check(CompleteTemporalFrame(viewB, b1, true), "View B history commit was rejected.") && ok;

    cameraA.SetLookAt({3.0f, 2.0f, -5.0f}, {0.0f, 1.0f, 0.0f});
    const RenderFrameToken a2 = Token(2u, 101u, 3u);
    const TemporalFrameState secondA = BeginTemporalFrame(Frame(viewA, a2, cameraA));
    ok = Check(secondA.resetReason == TemporalHistoryResetReason::None && secondA.historyValid,
               "View A did not preserve its own valid history.") &&
         ok;
    ok = Check(NearlyEqual(secondA.camera.prevCameraPos.x, 1.0f),
               "View A previous camera was contaminated by View B.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewA, a2, true), "View A second-frame history commit was rejected.") && ok;

    TemporalFrameDesc projectionB = Frame(viewB, Token(2u, 202u, 4u), cameraB);
    projectionB.projectionChanged = true;
    const TemporalFrameState secondB = BeginTemporalFrame(projectionB);
    ok = Check(secondB.resetReason == TemporalHistoryResetReason::ProjectionChanged,
               "Projection changes must invalidate only the affected view.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewB, projectionB.frame, true),
               "View B projection-reset history commit was rejected.") &&
         ok;
    const TemporalFrameState currentA = GetLatestTemporalFrameState(RenderViewId{101u});
    ok = Check(currentA.publication.Matches(a2) && currentA.historyValid,
               "View B reset mutated View A current state.") &&
         ok;

    TemporalFrameDesc resizedA = Frame(viewA, Token(3u, 101u, 5u), cameraA);
    resizedA.renderWidth = 1600;
    const TemporalFrameState thirdA = BeginTemporalFrame(resizedA);
    ok = Check(thirdA.resetReason == TemporalHistoryResetReason::Resize,
               "Per-view resize did not invalidate temporal history.") &&
         ok;

    TemporalFrameDesc configB = Frame(viewB, Token(3u, 202u, 6u), cameraB, 2u);
    const TemporalFrameState thirdB = BeginTemporalFrame(configB);
    ok = Check(thirdB.resetReason == TemporalHistoryResetReason::ConfigurationChanged,
               "Per-view temporal configuration change was not detected.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewB, configB.frame, true),
               "View B configuration-reset history commit was rejected.") &&
         ok;

    TemporalFrameDesc purposeB =
        Frame(viewB, Token(4u, 202u, 7u), cameraB, 2u, HIKARI::RENDER3D::RenderViewPurpose::EditorScene);
    const TemporalFrameState fourthB = BeginTemporalFrame(purposeB);
    ok = Check(fourthB.resetReason == TemporalHistoryResetReason::ViewPurposeChanged,
               "A render-view purpose change reused incompatible history.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewB, purposeB.frame, true),
               "View B purpose-reset history commit was rejected.") &&
         ok;

    TemporalFrameDesc gapB =
        Frame(viewB, Token(6u, 202u, 8u), cameraB, 2u, HIKARI::RENDER3D::RenderViewPurpose::EditorScene);
    const TemporalFrameState discontinuousB = BeginTemporalFrame(gapB);
    ok = Check(discontinuousB.resetReason == TemporalHistoryResetReason::FrameDiscontinuity,
               "A skipped render frame reused stale history.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewB, gapB.frame, true), "View B discontinuity history commit was rejected.") &&
         ok;

    TemporalFrameDesc unfinishedB =
        Frame(viewB, Token(7u, 202u, 9u), cameraB, 2u, HIKARI::RENDER3D::RenderViewPurpose::EditorScene);
    const TemporalFrameState begunB = BeginTemporalFrame(unfinishedB);
    ok = Check(begunB.resetReason == TemporalHistoryResetReason::None,
               "A continuous committed View B frame unexpectedly reset.") &&
         ok;
    TemporalFrameDesc afterFailureB =
        Frame(viewB, Token(8u, 202u, 10u), cameraB, 2u, HIKARI::RENDER3D::RenderViewPurpose::EditorScene);
    const TemporalFrameState recoveredB = BeginTemporalFrame(afterFailureB);
    ok = Check(recoveredB.resetReason == TemporalHistoryResetReason::PreviousFrameIncomplete,
               "An uncommitted frame advanced the previous-camera history.") &&
         ok;
    ok = Check(CompleteTemporalFrame(viewB, afterFailureB.frame, true),
               "View B recovery history commit was rejected.") &&
         ok;

    TemporalFrameDesc sourceB =
        Frame(viewB, Token(9u, 202u, 11u), cameraB, 2u, HIKARI::RENDER3D::RenderViewPurpose::EditorScene);
    sourceB.cameraSourceObjectId = 42u;
    const TemporalFrameState sourceChangedB = BeginTemporalFrame(sourceB);
    ok = Check(sourceChangedB.resetReason == TemporalHistoryResetReason::CameraSourceChanged,
               "A camera source change reused incompatible history.") &&
         ok;
    ok =
        Check(CompleteTemporalFrame(viewB, sourceB.frame, true), "View B camera-source history commit was rejected.") &&
        ok;

    ok = Check(ResetTemporalFrameHistory(viewA, TemporalHistoryResetReason::ExplicitReset),
               "Explicit reset request was rejected for a valid view.") &&
         ok;
    ok = Check(!CompleteTemporalFrame(viewA, resizedA.frame, true),
               "A reset active frame was allowed to commit stale history.") &&
         ok;
    TemporalFrameDesc resetA = Frame(viewA, Token(4u, 101u, 12u), cameraA);
    resetA.renderWidth = 1600;
    const TemporalFrameState fourthA = BeginTemporalFrame(resetA);
    ok = Check(fourthA.resetReason == TemporalHistoryResetReason::ExplicitReset,
               "Explicit reset was not consumed by the requested view.") &&
         ok;

    TemporalFrameState exactB{};
    ok = Check(TryGetTemporalFrameState(viewB, sourceB.frame, exactB) && exactB.publication.Matches(sourceB.frame),
               "Exact frame-token lookup rejected a current publication.") &&
         ok;
    TemporalFrameState staleB{};
    ok = Check(!TryGetTemporalFrameState(viewB, b1, staleB), "Stale frame-token lookup did not fail closed.") && ok;
    TemporalFrameState wrongView{};
    ok = Check(!TryGetTemporalFrameState(viewA, sourceB.frame, wrongView),
               "A frame token from another view was accepted.") &&
         ok;

    const TemporalFrameState staleA = BeginTemporalFrame(Frame(viewA, Token(3u, 101u, 4u), cameraA));
    ok = Check(staleA.resetReason == TemporalHistoryResetReason::InvalidFrameToken &&
                   GetLatestTemporalFrameState(RenderViewId{101u}).publication.Matches(resetA.frame),
               "Out-of-order publication replaced the current per-view state.") &&
         ok;

    ok = Check(AbortTemporalFrame(viewA, fourthA.publication, TemporalHistoryResetReason::ExplicitReset) &&
                   ReleaseTemporalFrameViewState(viewA) && GetTemporalFrameStateViewCount() == 1u,
               "Released temporal view state remained resident.") &&
         ok;
    TemporalViewLease reusedViewA{};
    ok = Check(AcquireTemporalView(RenderViewId{101u}, reusedViewA) && reusedViewA.generation != viewA.generation,
               "Reused view identity did not receive a new lease generation.") &&
         ok;
    const TemporalFrameState reusedA = BeginTemporalFrame(Frame(reusedViewA, Token(5u, 101u, 13u), cameraA));
    ok = Check(reusedA.resetReason == TemporalHistoryResetReason::FirstFrame,
               "Released view identity reused stale history.") &&
         ok;

    const uint32_t countBeforeInvalid = GetTemporalFrameStateViewCount();
    const TemporalFrameState invalid = BeginTemporalFrame(Frame(reusedViewA, RenderFrameToken{}, cameraA));
    ok = Check(invalid.resetReason == TemporalHistoryResetReason::InvalidFrameToken &&
                   GetTemporalFrameStateViewCount() == countBeforeInvalid,
               "Invalid frame token created a temporal view state.") &&
         ok;
    ok = Check(!ReleaseTemporalFrameViewState(reusedViewA),
               "An active temporal frame was released without a terminal state.") &&
         ok;
    ok = Check(CompleteTemporalFrame(reusedViewA, reusedA.publication, false) &&
                   ReleaseTemporalFrameViewState(reusedViewA),
               "A completed temporal view could not be released.") &&
         ok;

    TemporalFrameState retiredLeaseState{};
    ok = Check(!TryGetTemporalFrameState(viewA, a2, retiredLeaseState),
               "A retired temporal view lease remained valid.") &&
         ok;
    ok = Check(!CompleteTemporalFrame(viewA, a2, false) &&
                   !AbortTemporalFrame(viewA, a2, TemporalHistoryResetReason::ExplicitReset),
               "A retired lease completed or aborted a frame.") &&
         ok;

    std::atomic<bool> wrongThreadRejected{false};
    std::thread wrongThread([&wrongThreadRejected]() {
        TemporalViewLease lease{};
        const bool acquired = AcquireTemporalView(RenderViewId{777u}, lease);
        const TemporalFrameState latest = GetLatestTemporalFrameState(RenderViewId{101u});
        wrongThreadRejected.store(!acquired && !lease.IsValid() && !latest.publication.IsValid(),
                                  std::memory_order_relaxed);
    });
    wrongThread.join();
    ok = Check(wrongThreadRejected.load(std::memory_order_relaxed),
               "A non-owner thread entered temporal frame state.") &&
         ok;

    TemporalViewLease noHistoryView{};
    ok = Check(AcquireTemporalView(RenderViewId{250u}, noHistoryView),
               "Could not acquire the no-history completion test view.") &&
         ok;
    const RenderFrameToken noHistory1 = Token(1u, 250u, 20u);
    auto noHistoryDesc = Frame(noHistoryView, noHistory1, cameraA);
    noHistoryDesc.jitterEnabled = false;
    const TemporalFrameState noHistoryFirst = BeginTemporalFrame(noHistoryDesc);
    ok = Check(noHistoryFirst.resetReason == TemporalHistoryResetReason::FirstFrame &&
                   CompleteTemporalFrame(noHistoryView, noHistory1, false),
               "A normal no-history frame did not complete.") &&
         ok;
    const RenderFrameToken noHistory2 = Token(2u, 250u, 21u);
    noHistoryDesc.frame = noHistory2;
    noHistoryDesc.camera = &cameraB;
    const TemporalFrameState noHistorySecond = BeginTemporalFrame(noHistoryDesc);
    ok = Check(noHistorySecond.resetReason == TemporalHistoryResetReason::None && !noHistorySecond.historyValid &&
                   !noHistorySecond.jitterEnabled && noHistorySecond.camera.previousValid,
               "AA Off did not preserve camera continuity for independent effect histories.") &&
         ok;
    ok = Check(NearlyEqual(noHistorySecond.camera.prevCameraPos.x, cameraA.GetPosition().x) &&
                   NearlyEqual(noHistorySecond.camera.cameraPos.x, cameraB.GetPosition().x),
               "AA Off replaced the previous camera with the moving camera.") &&
         ok;
    ok = Check(AbortTemporalFrame(noHistoryView, noHistory2, TemporalHistoryResetReason::CameraCut),
               "The explicit abort path rejected an active frame.") &&
         ok;
    const RenderFrameToken afterAbort = Token(3u, 250u, 22u);
    const TemporalFrameState abortedRecovery = BeginTemporalFrame(Frame(noHistoryView, afterAbort, cameraA));
    ok = Check(abortedRecovery.resetReason == TemporalHistoryResetReason::CameraCut &&
                   !abortedRecovery.camera.previousValid,
               "Abort did not preserve its reset reason for the next frame.") &&
         ok;
    ok = Check(CompleteTemporalFrame(noHistoryView, afterAbort, false) && ReleaseTemporalFrameViewState(noHistoryView),
               "The no-history test view did not close cleanly.") &&
         ok;

    // 連続交代の許可は source ID だけに作用し、実カット・投影変更を無効化しない。
    for (int scenario = 0; scenario < 4; ++scenario) {
        TemporalViewLease handoffView{};
        ok = Check(AcquireTemporalView(RenderViewId{250u}, handoffView), "Handoff view acquisition failed.") && ok;
        auto before = Frame(handoffView, Token(1u, 250u, 1u), cameraA);
        before.cameraSourceObjectId = 10;
        (void)BeginTemporalFrame(before);
        ok = Check(CompleteTemporalFrame(handoffView, before.frame, true), "Handoff baseline commit failed.") && ok;
        auto after = Frame(handoffView, Token(2u, 250u, 2u), cameraA);
        after.cameraSourceObjectId = 20;
        after.continuousCameraSource = scenario != 1;
        after.cameraCut = scenario == 2;
        after.projectionChanged = scenario == 3;
        const auto result = BeginTemporalFrame(after);
        const auto expected = scenario == 0   ? TemporalHistoryResetReason::None
                              : scenario == 1 ? TemporalHistoryResetReason::CameraSourceChanged
                              : scenario == 2 ? TemporalHistoryResetReason::CameraCut
                                              : TemporalHistoryResetReason::ProjectionChanged;
        ok = Check(result.resetReason == expected && result.camera.previousValid == (scenario == 0),
                   "Camera-source handoff broke temporal reset boundaries.") &&
             ok;
        ok = Check(CompleteTemporalFrame(handoffView, after.frame, true), "Handoff frame commit failed.") && ok;
        ok = Check(ReleaseTemporalFrameViewState(handoffView), "Handoff view release failed.") && ok;
    }

    std::vector<TemporalViewLease> capacityLeases{};
    for (uint64_t viewId = 300u; GetTemporalFrameStateViewCount() < kTemporalMaximumViewCount; ++viewId) {
        TemporalViewLease lease{};
        if (!AcquireTemporalView(RenderViewId{viewId}, lease)) {
            ok = Check(false, "Temporal view capacity rejected an in-budget view.") && ok;
            break;
        }
        capacityLeases.push_back(lease);
    }
    TemporalViewLease overCapacity{};
    ok = Check(!AcquireTemporalView(RenderViewId{999u}, overCapacity) &&
                   GetTemporalFrameStateViewCount() == kTemporalMaximumViewCount,
               "Temporal view capacity did not fail closed.") &&
         ok;
    if (!capacityLeases.empty()) {
        const TemporalViewLease retired = capacityLeases.back();
        capacityLeases.pop_back();
        ok = Check(ReleaseTemporalFrameViewState(retired), "Could not release a capacity test view.") && ok;
        TemporalViewLease replacement{};
        ok =
            Check(AcquireTemporalView(RenderViewId{1000u}, replacement) && replacement.generation != retired.generation,
                  "Released capacity was not reusable with a fresh lease.") &&
            ok;
    }

    ShutdownTemporalFrameStateSystem();
    return ok ? 0 : 1;
}
