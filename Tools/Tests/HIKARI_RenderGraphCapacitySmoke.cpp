#include <array>
#include <cstdio>
#include <span>
#include <string_view>

#include "Render3D/Graph/HIKARI_RenderGraph.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphSession.h"
#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"
#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

namespace {

using namespace HIKARI::RENDER3D::GRAPH;
namespace PIPELINE = HIKARI::RENDER3D::PIPELINE;
namespace SURFACE = HIKARI::RENDER3D::SURFACE;

RenderGraphResourceDesc MakeResource(std::string_view id, bool imported = false) noexcept {
    RenderGraphResourceDesc desc{};
    desc.id = id;
    desc.imported = imported;
    desc.kind = RenderGraphResourceKind::Internal;
    desc.declaredAccessMask =
        RenderGraphAccessBit(RenderGraphAccess::ReadSampled) | RenderGraphAccessBit(RenderGraphAccess::WriteColor);
    return desc;
}

RenderGraphResourceDesc MakeSemanticResource(std::string_view id, RenderGraphSemantic semantic,
                                             RenderGraphSemanticPhase phase, SURFACE::SceneSurfaceProducer producer,
                                             bool imported = false) noexcept {
    RenderGraphResourceDesc desc{id, semantic, imported};
    desc.kind = RenderGraphResourceKind::SceneSurface;
    desc.phase = phase;
    desc.expectedProducer = producer;
    return desc;
}

bool NoopExecute(const RenderGraphPassContext&, void*) noexcept { return true; }

void TestRenderTextureViewIdentity() {
    HIKARI::RENDER3D::RenderTextureView source{};
    source.srv.ptr = 7u;
    source.width = 1u;
    source.height = 1u;
    source.valid = true;
    auto candidate = source;
    assert(source.MatchesPhysicalView(candidate));
    source.resource = reinterpret_cast<ID3D12Resource*>(1u);
    candidate.resource = reinterpret_cast<ID3D12Resource*>(2u);
    assert(!source.MatchesPhysicalView(candidate));
    candidate.resource = source.resource;
    candidate.srv.ptr = 8u;
    assert(!source.MatchesPhysicalView(candidate));
}

RenderGraphPassDesc MakePass(std::string_view id, std::span<const RenderGraphResourceUse> reads = {},
                             std::span<const RenderGraphResourceUse> writes = {}) noexcept {
    return RenderGraphPassDesc{id, reads, writes, &NoopExecute, nullptr};
}

void TestResourceCapacity() {
    RenderGraph graph{};
    assert(graph.BeginFrame(1u));
    std::array<std::array<char, kRenderGraphIdentifierCapacity>, kRenderGraphMaximumResourceCount + 1u> ids{};
    for (uint32_t index = 0u; index < ids.size(); ++index) {
        std::snprintf(ids[index].data(), ids[index].size(), "CapacityResource%u", index);
    }
    for (uint32_t index = 0u; index < kRenderGraphMaximumResourceCount; ++index) {
        assert(graph.AddResource(MakeResource(std::string_view(ids[index].data()), true)).IsValid());
    }
    assert(graph.GetResourceCount() == kRenderGraphMaximumResourceCount);
    assert(!graph.AddResource(MakeResource(std::string_view(ids.back().data()), true)).IsValid());
    assert(graph.GetDiagnostic().code == RenderGraphResultCode::CapacityExceeded);
}

void TestPassCapacity() {
    RenderGraph graph{};
    assert(graph.BeginFrame(2u));
    std::array<std::array<char, kRenderGraphIdentifierCapacity>, kRenderGraphMaximumPassCount + 1u> ids{};
    for (uint32_t index = 0u; index < ids.size(); ++index) {
        std::snprintf(ids[index].data(), ids[index].size(), "CapacityPass%u", index);
    }
    for (uint32_t index = 0u; index < kRenderGraphMaximumPassCount; ++index) {
        assert(graph.AddPass(MakePass(std::string_view(ids[index].data()))));
    }
    assert(graph.GetPassCount() == kRenderGraphMaximumPassCount);
    assert(!graph.AddPass(MakePass(std::string_view(ids.back().data()))));
    assert(graph.GetDiagnostic().code == RenderGraphResultCode::CapacityExceeded);
    assert(graph.Compile());
}

void TestUseCapacity() {
    RenderGraph graph{};
    assert(graph.BeginFrame(3u));
    std::array<std::array<char, kRenderGraphIdentifierCapacity>, kRenderGraphMaximumResourceCount> ids{};
    std::array<RenderGraphResourceUse, kRenderGraphMaximumResourceCount> writes{};
    std::array<RenderGraphResourceUse, kRenderGraphMaximumResourceCount> reads{};
    for (uint32_t index = 0u; index < ids.size(); ++index) {
        std::snprintf(ids[index].data(), ids[index].size(), "CapacityUseResource%u", index);
        const RenderGraphResourceId resource = graph.AddResource(MakeResource(std::string_view(ids[index].data())));
        assert(resource.IsValid());
        writes[index] = RenderGraphResourceUse{resource, RenderGraphAccess::WriteColor};
        reads[index] = RenderGraphResourceUse{resource, RenderGraphAccess::ReadSampled};
    }

    assert(graph.AddPass(MakePass("CapacityUseProducer", {}, std::span<const RenderGraphResourceUse>(writes))));
    assert(graph.AddPass(MakePass("CapacityUseConsumer", std::span<const RenderGraphResourceUse>(reads))));
    assert(graph.AddPass(MakePass("CapacityUseConsumer2", std::span<const RenderGraphResourceUse>(reads))));
    assert(graph.AddPass(MakePass("CapacityUseConsumer3", std::span<const RenderGraphResourceUse>(reads))));
    assert(graph.Compile());

    const std::array<RenderGraphResourceUse, 1u> extraUse{reads[0]};
    assert(!graph.AddPass(MakePass("CapacityUseOverflow", std::span<const RenderGraphResourceUse>(extraUse))));
    assert(graph.GetDiagnostic().code == RenderGraphResultCode::CapacityExceeded);
    assert(graph.GetPassCount() == 4u);
}

void TestReadWriteConflict() {
    RenderGraph graph{};
    assert(graph.BeginFrame(4u));
    const RenderGraphResourceId resource = graph.AddResource(MakeSemanticResource(
        "ReadWriteConflict", RenderGraphSemantic::ProjectDispatchOutput, RenderGraphSemanticPhase::PostOpaque,
        SURFACE::SceneSurfaceProducer::ProjectDispatch, true));
    assert(resource.IsValid());
    const std::array<RenderGraphResourceUse, 1u> reads{
        RenderGraphResourceUse{resource, RenderGraphAccess::ReadSampled}};
    const std::array<RenderGraphResourceUse, 1u> writes{
        RenderGraphResourceUse{resource, RenderGraphAccess::WriteUav, SURFACE::SceneSurfaceProducer::ProjectDispatch}};
    assert(!graph.AddPass(MakePass("ConflictingPass", std::span<const RenderGraphResourceUse>(reads),
                                   std::span<const RenderGraphResourceUse>(writes))));
    assert(graph.GetDiagnostic().code == RenderGraphResultCode::ReadWriteConflict);
}

void TestSemanticContractAndPhaseValidation() {
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(5u));
        const auto preOpaque = graph.AddResource(MakeSemanticResource(
            "Packed.PreOpaque", RenderGraphSemantic::PackedSurface, RenderGraphSemanticPhase::PreOpaque,
            SURFACE::SceneSurfaceProducer::SceneSurfaceAux, true));
        const auto postOpaque = graph.AddResource(
            MakeSemanticResource("Packed.PostOpaque", RenderGraphSemantic::PackedSurface,
                                 RenderGraphSemanticPhase::PostOpaque, SURFACE::SceneSurfaceProducer::SceneSurfaceAux));
        assert(preOpaque.IsValid() && postOpaque.IsValid());
        assert(graph.FindSemanticResource(RenderGraphSemantic::PackedSurface, RenderGraphSemanticPhase::PreOpaque) ==
               preOpaque);
        assert(graph.FindSemanticResource(RenderGraphSemantic::PackedSurface, RenderGraphSemanticPhase::PostOpaque) ==
               postOpaque);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(7u));
        const auto wrongProducer =
            MakeSemanticResource("WrongProducer", RenderGraphSemantic::SceneDepth, RenderGraphSemanticPhase::Imported,
                                 SURFACE::SceneSurfaceProducer::TemporalMotion, true);
        assert(!graph.AddResource(wrongProducer).IsValid());
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::ProducerNotAllowed);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(8u));
        const auto depth = graph.AddResource(MakeSemanticResource("DepthAccess", RenderGraphSemantic::SceneDepth,
                                                                  RenderGraphSemanticPhase::Imported,
                                                                  SURFACE::SceneSurfaceProducer::PostSceneDepth, true));
        const std::array<RenderGraphResourceUse, 1u> writes{RenderGraphResourceUse{
            depth, RenderGraphAccess::WriteColor, SURFACE::SceneSurfaceProducer::PostSceneDepth}};
        assert(!graph.AddPass(MakePass("WrongDepthWriter", {}, std::span<const RenderGraphResourceUse>(writes))));
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::AccessNotAllowed);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(9u));
        auto optionalDepth =
            MakeSemanticResource("OptionalDepth", RenderGraphSemantic::SceneDepth, RenderGraphSemanticPhase::Imported,
                                 SURFACE::SceneSurfaceProducer::PostSceneDepth, true);
        optionalDepth.requirement = RenderGraphResourceRequirement::Optional;
        assert(!graph.AddResource(optionalDepth).IsValid());
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::SemanticContractMismatch);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(10u));
        const auto wrongHistoryPhase =
            MakeSemanticResource("WrongHistoryPhase", RenderGraphSemantic::HistoryColor,
                                 RenderGraphSemanticPhase::Temporal, SURFACE::SceneSurfaceProducer::TemporalHistory);
        assert(!graph.AddResource(wrongHistoryPhase).IsValid());
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::SemanticContractMismatch);
    }
}

void TestResourceKindsAndProducerBinding() {
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(11u));
        RenderGraphResourceDesc implicitInternal{};
        implicitInternal.id = "ImplicitInternal";
        assert(!graph.AddResource(implicitInternal).IsValid());
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::SemanticContractMismatch);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(12u));
        RenderGraphResourceDesc marker{};
        marker.id = "Complete";
        marker.kind = RenderGraphResourceKind::CompletionMarker;
        const auto completion = graph.AddResource(marker);
        assert(completion.IsValid());
        const std::array invalidWrites{RenderGraphResourceUse{completion, RenderGraphAccess::WriteColor}};
        assert(!graph.AddPass(MakePass("InvalidMarkerWriter", {}, invalidWrites)));
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::AccessNotAllowed);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(13u));
        RenderGraphResourceDesc marker{};
        marker.id = "Complete";
        marker.kind = RenderGraphResourceKind::CompletionMarker;
        const auto completion = graph.AddResource(marker);
        const std::array writes{RenderGraphResourceUse{completion, RenderGraphAccess::WriteDependency}};
        const std::array reads{RenderGraphResourceUse{completion, RenderGraphAccess::ReadDependency}};
        assert(graph.AddPass(MakePass("Producer", {}, writes)));
        assert(graph.AddPass(MakePass("Consumer", reads)));
        assert(graph.Compile());
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(14u));
        const auto sceneColor = graph.AddResource(
            MakeSemanticResource("SceneColorFinal", RenderGraphSemantic::SceneColorFinal,
                                 RenderGraphSemanticPhase::Final, SURFACE::SceneSurfaceProducer::PostFinal));
        const std::array writes{RenderGraphResourceUse{sceneColor, RenderGraphAccess::WriteColor,
                                                       SURFACE::SceneSurfaceProducer::FeatureComposite}};
        assert(!graph.AddPass(MakePass("WrongProducer", {}, writes)));
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::ProducerNotAllowed);
    }
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(15u));
        RenderGraphResourceDesc marker{};
        marker.id = "UnproducedComplete";
        marker.kind = RenderGraphResourceKind::CompletionMarker;
        assert(graph.AddResource(marker).IsValid());
        assert(!graph.Compile());
        assert(graph.GetDiagnostic().code == RenderGraphResultCode::MissingProducer);
    }
}

void TestPausedAbortAllowsFrameReset() {
    RenderGraph graph{};
    assert(graph.BeginFrame(16u));
    assert(graph.AddPass(MakePass("Prefix")));
    assert(graph.AddPass(MakePass("Suffix")));
    assert(graph.Compile());
    assert(graph.ExecuteThrough("Prefix"));
    assert(graph.GetExecutionState().status == RenderGraphExecutionStatus::Paused);
    assert(graph.AbortExecution());
    assert(graph.GetExecutionState().status == RenderGraphExecutionStatus::Failed);
    assert(graph.GetDiagnostic().code == RenderGraphResultCode::ExecutionAborted);
    assert(!graph.ContinueExecute());
    assert(graph.GetDiagnostic().code == RenderGraphResultCode::ExecutionAborted);
    assert(graph.BeginFrame(17u));
    assert(graph.GetExecutionState().status == RenderGraphExecutionStatus::NotStarted);
}

void TestCrossStageSessionLifecycle() {
    const auto frame = HIKARI::RENDER3D::AllocateRenderFrameToken(18u, HIKARI::RENDER3D::RenderViewId{1u});
    SURFACE::RenderFrameSurfaceReceipt surfaces{};
    assert(SURFACE::BeginRenderFrameSurfaces(frame, 19u, surfaces));

    PIPELINE::RenderFrameGraphSessionReceipt session{};
    assert(PIPELINE::BeginRenderFrameGraphSession(surfaces, session));
    PIPELINE::RenderFrameGraphSessionReceipt duplicate{};
    assert(!PIPELINE::BeginRenderFrameGraphSession(surfaces, duplicate));
    assert(!duplicate.IsValid());
    assert(PIPELINE::IsRenderFrameGraphSessionCurrent(session));

    RenderGraph* graph = PIPELINE::INTERNAL::TryGetRenderFrameGraph(session);
    PIPELINE::INTERNAL::StageExecutionState* state = PIPELINE::INTERNAL::TryGetRenderFrameGraphStageState(session);
    assert(graph != nullptr && state != nullptr);
    state->session = session;
    assert(graph->AddPass(MakePass("Session.Prefix")));
    assert(graph->AddPass(
        RenderGraphPassDesc{"Session.Resume", {}, {}, &PIPELINE::INTERNAL::ExecutePresentationResume, state}));
    assert(graph->Compile());
    assert(graph->ExecuteThrough("Session.Prefix"));
    assert(graph->GetExecutionState().status == RenderGraphExecutionStatus::Paused);
    assert(PIPELINE::ContinueRenderFrameGraphSession(session));
    assert(state->presentationResumed);
    assert(PIPELINE::EndRenderFrameGraphSession(session));
    assert(!PIPELINE::IsRenderFrameGraphSessionCurrent(session));
    assert(SURFACE::TryGetRenderFrameSurfaces(surfaces) != nullptr);

    PIPELINE::RenderFrameGraphSessionReceipt aborted{};
    assert(PIPELINE::BeginRenderFrameGraphSession(surfaces, aborted));
    graph = PIPELINE::INTERNAL::TryGetRenderFrameGraph(aborted);
    assert(graph != nullptr);
    assert(graph->AddPass(MakePass("Abort.Prefix")));
    assert(graph->AddPass(MakePass("Abort.Suffix")));
    assert(graph->Compile());
    assert(graph->ExecuteThrough("Abort.Prefix"));
    assert(PIPELINE::AbortRenderFrameGraphSession(aborted));
    assert(!PIPELINE::IsRenderFrameGraphSessionCurrent(aborted));

    PIPELINE::RenderFrameGraphSessionReceipt orphaned{};
    assert(PIPELINE::BeginRenderFrameGraphSession(surfaces, orphaned));
    assert(SURFACE::EndRenderFrameSurfaces(surfaces));
    assert(!PIPELINE::IsRenderFrameGraphSessionCurrent(orphaned));
    assert(PIPELINE::AbortRenderFrameGraphSession(orphaned));
    assert(PIPELINE::ShutdownRenderFrameGraphSessions());
    assert(SURFACE::ShutdownRenderFrameSurfaceRegistry());
}

} // namespace

int main() {
    TestRenderTextureViewIdentity();
    TestResourceCapacity();
    TestPassCapacity();
    TestUseCapacity();
    TestReadWriteConflict();
    TestSemanticContractAndPhaseValidation();
    TestResourceKindsAndProducerBinding();
    TestPausedAbortAllowsFrameReset();
    TestCrossStageSessionLifecycle();
    return 0;
}
