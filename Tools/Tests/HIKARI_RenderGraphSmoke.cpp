#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>

#include "Render3D/Graph/HIKARI_RenderGraph.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

namespace {

static_assert(!std::is_copy_constructible_v<HIKARI::RENDER3D::GRAPH::RenderGraph>);
static_assert(!std::is_move_constructible_v<HIKARI::RENDER3D::GRAPH::RenderGraph>);

using HIKARI::RENDER3D::GRAPH::RenderGraph;
using HIKARI::RENDER3D::GRAPH::RenderGraphAccess;
using HIKARI::RENDER3D::GRAPH::RenderGraphDiagnostic;
using HIKARI::RENDER3D::GRAPH::RenderGraphExecuteFn;
using HIKARI::RENDER3D::GRAPH::RenderGraphPassContext;
using HIKARI::RENDER3D::GRAPH::RenderGraphPassDesc;
using HIKARI::RENDER3D::GRAPH::RenderGraphResourceDesc;
using HIKARI::RENDER3D::GRAPH::RenderGraphResourceId;
using HIKARI::RENDER3D::GRAPH::RenderGraphResourceInfo;
using HIKARI::RENDER3D::GRAPH::RenderGraphResourceKind;
using HIKARI::RENDER3D::GRAPH::RenderGraphResourceUse;
using HIKARI::RENDER3D::GRAPH::RenderGraphResultCode;
using HIKARI::RENDER3D::GRAPH::RenderGraphSemantic;
namespace SURFACE = HIKARI::RENDER3D::SURFACE;

RenderGraphResourceDesc MakeInternalResource(std::string_view id, bool imported = false) noexcept {
    RenderGraphResourceDesc desc{id, RenderGraphSemantic::None, imported};
    desc.kind = RenderGraphResourceKind::Internal;
    desc.declaredAccessMask = HIKARI::RENDER3D::GRAPH::RenderGraphAccessBit(RenderGraphAccess::ReadSampled) |
                              HIKARI::RENDER3D::GRAPH::RenderGraphAccessBit(RenderGraphAccess::WriteColor);
    return desc;
}

RenderGraphResourceDesc MakeSceneDepthResource(std::string_view id) noexcept {
    RenderGraphResourceDesc desc{id, RenderGraphSemantic::SceneDepth, true};
    desc.kind = RenderGraphResourceKind::SceneSurface;
    desc.expectedProducer = SURFACE::SceneSurfaceProducer::PostSceneDepth;
    desc.phase = HIKARI::RENDER3D::GRAPH::RenderGraphSemanticPhase::Imported;
    return desc;
}

RenderGraphPassDesc MakePass(std::string_view id, RenderGraphExecuteFn execute = nullptr,
                             std::span<const RenderGraphResourceUse> reads = {},
                             std::span<const RenderGraphResourceUse> writes = {}, void* userData = nullptr) noexcept {
    return RenderGraphPassDesc{id, reads, writes, execute, userData};
}

bool NoopExecute(const RenderGraphPassContext&, void*) noexcept { return true; }

bool FailExecute(const RenderGraphPassContext&, void*) noexcept { return false; }

void ExpectDiagnostic(const RenderGraph& graph, RenderGraphResultCode expected) {
    const RenderGraphDiagnostic& diagnostic = graph.GetDiagnostic();
    assert(diagnostic.code == expected);
}

void ExpectCompiledPassIds(const RenderGraph& graph, std::initializer_list<std::string_view> expected) {
    assert(graph.GetCompiledPassCount() == expected.size());
    uint32_t order = 0u;
    for (const std::string_view expectedId : expected) {
        uint32_t passIndex = 0u;
        assert(graph.GetCompiledPassIndex(order, passIndex));
        assert(graph.GetPassId(passIndex) == expectedId);
        ++order;
    }
}

void TestStableTopologicalOrder() {
    RenderGraph graph{};
    assert(graph.BeginFrame(1u));

    const RenderGraphResourceId produced = graph.AddResource(MakeInternalResource("StableProduced"));
    assert(produced.IsValid());

    assert(graph.AddPass(MakePass("IndependentA", &NoopExecute)));
    assert(graph.AddPass(MakePass("IndependentB", &NoopExecute)));

    const std::array<RenderGraphResourceUse, 1u> writes{
        RenderGraphResourceUse{produced, RenderGraphAccess::WriteColor}};
    const std::array<RenderGraphResourceUse, 1u> reads{
        RenderGraphResourceUse{produced, RenderGraphAccess::ReadSampled}};
    assert(graph.AddPass(MakePass("Producer", &NoopExecute, {}, std::span<const RenderGraphResourceUse>(writes))));
    assert(graph.AddPass(MakePass("Consumer", &NoopExecute, std::span<const RenderGraphResourceUse>(reads))));

    assert(graph.Compile());
    ExpectCompiledPassIds(graph, {"IndependentA", "IndependentB", "Producer", "Consumer"});
}

void TestExplicitFrameBoundary() {
    RenderGraph graph{};
    assert(!graph.GetFrameToken().IsValid());
    assert(!graph.AddResource(MakeInternalResource("NoFrame")).IsValid());
    ExpectDiagnostic(graph, RenderGraphResultCode::InvalidFrame);
    assert(!graph.BeginFrame(1u, HIKARI::RENDER3D::RenderViewId{}));
    ExpectDiagnostic(graph, RenderGraphResultCode::InvalidFrame);
    assert(graph.BeginFrame(1u));
    const HIKARI::RENDER3D::RenderFrameToken sharedFrame =
        HIKARI::RENDER3D::AllocateRenderFrameToken(2u, HIKARI::RENDER3D::kPrimaryRenderViewId);
    assert(graph.BeginFrame(sharedFrame));
    assert(graph.GetFrameToken() == sharedFrame);
    assert(sharedFrame.Matches(graph.GetFrameToken()));
    assert(sharedFrame.MatchesFrameView(2u, HIKARI::RENDER3D::kPrimaryRenderViewId));
    HIKARI::RENDER3D::RenderFrameToken forgedFrame = sharedFrame;
    ++forgedFrame.generation;
    assert(!sharedFrame.Matches(forgedFrame));
}

void TestProducerAddedAfterConsumer() {
    RenderGraph graph{};
    assert(graph.BeginFrame(2u));

    const RenderGraphResourceId resource = graph.AddResource(MakeInternalResource("LateProduced"));
    assert(resource.IsValid());

    const std::array<RenderGraphResourceUse, 1u> reads{
        RenderGraphResourceUse{resource, RenderGraphAccess::ReadSampled}};
    const std::array<RenderGraphResourceUse, 1u> writes{
        RenderGraphResourceUse{resource, RenderGraphAccess::WriteColor}};
    assert(graph.AddPass(MakePass("ConsumerAddedFirst", &NoopExecute, std::span<const RenderGraphResourceUse>(reads))));
    assert(graph.AddPass(
        MakePass("ProducerAddedSecond", &NoopExecute, {}, std::span<const RenderGraphResourceUse>(writes))));

    assert(graph.Compile());
    ExpectCompiledPassIds(graph, {"ProducerAddedSecond", "ConsumerAddedFirst"});
}

void TestMissingProducer() {
    RenderGraph graph{};
    assert(graph.BeginFrame(3u));
    const RenderGraphResourceId resource = graph.AddResource(MakeInternalResource("MissingProducer"));
    assert(resource.IsValid());

    const std::array<RenderGraphResourceUse, 1u> reads{
        RenderGraphResourceUse{resource, RenderGraphAccess::ReadSampled}};
    assert(graph.AddPass(MakePass("MissingReader", &NoopExecute, std::span<const RenderGraphResourceUse>(reads))));
    assert(!graph.Compile());
    ExpectDiagnostic(graph, RenderGraphResultCode::MissingProducer);
}

void TestDuplicateWriter() {
    RenderGraph graph{};
    assert(graph.BeginFrame(4u));
    const RenderGraphResourceId resource = graph.AddResource(MakeInternalResource("DuplicateWriter"));
    assert(resource.IsValid());

    const std::array<RenderGraphResourceUse, 1u> writes{
        RenderGraphResourceUse{resource, RenderGraphAccess::WriteColor}};
    assert(graph.AddPass(MakePass("WriterA", &NoopExecute, {}, std::span<const RenderGraphResourceUse>(writes))));
    assert(graph.AddPass(MakePass("WriterB", &NoopExecute, {}, std::span<const RenderGraphResourceUse>(writes))));
    assert(!graph.Compile());
    ExpectDiagnostic(graph, RenderGraphResultCode::DuplicateWriter);
}

void TestCycle() {
    RenderGraph graph{};
    assert(graph.BeginFrame(5u));
    const RenderGraphResourceId resourceA = graph.AddResource(MakeInternalResource("CycleA"));
    const RenderGraphResourceId resourceB = graph.AddResource(MakeInternalResource("CycleB"));
    assert(resourceA.IsValid());
    assert(resourceB.IsValid());

    const std::array<RenderGraphResourceUse, 1u> readsA{
        RenderGraphResourceUse{resourceB, RenderGraphAccess::ReadSampled}};
    const std::array<RenderGraphResourceUse, 1u> writesA{
        RenderGraphResourceUse{resourceA, RenderGraphAccess::WriteColor}};
    const std::array<RenderGraphResourceUse, 1u> readsB{
        RenderGraphResourceUse{resourceA, RenderGraphAccess::ReadSampled}};
    const std::array<RenderGraphResourceUse, 1u> writesB{
        RenderGraphResourceUse{resourceB, RenderGraphAccess::WriteColor}};

    assert(graph.AddPass(MakePass("CycleAWriter", &NoopExecute, std::span<const RenderGraphResourceUse>(readsA),
                                  std::span<const RenderGraphResourceUse>(writesA))));
    assert(graph.AddPass(MakePass("CycleBWriter", &NoopExecute, std::span<const RenderGraphResourceUse>(readsB),
                                  std::span<const RenderGraphResourceUse>(writesB))));
    assert(!graph.Compile());
    ExpectDiagnostic(graph, RenderGraphResultCode::CycleDetected);
}

void TestDuplicateResourceAndPassIdentity() {
    {
        RenderGraph graph{};
        assert(graph.BeginFrame(6u));
        const RenderGraphResourceId first = graph.AddResource(MakeInternalResource("DuplicateResourceId"));
        assert(first.IsValid());
        assert(!graph.AddResource(MakeInternalResource("DuplicateResourceId")).IsValid());
        ExpectDiagnostic(graph, RenderGraphResultCode::DuplicateResourceId);
    }

    {
        RenderGraph graph{};
        assert(graph.BeginFrame(7u));
        const RenderGraphResourceId first = graph.AddResource(MakeSceneDepthResource("DepthA"));
        assert(first.IsValid());
        assert(!graph.AddResource(MakeSceneDepthResource("DepthB")).IsValid());
        ExpectDiagnostic(graph, RenderGraphResultCode::DuplicateSemantic);
    }

    {
        RenderGraph graph{};
        assert(graph.BeginFrame(8u));
        assert(graph.AddPass(MakePass("DuplicatePass", &NoopExecute)));
        assert(!graph.AddPass(MakePass("DuplicatePass", &NoopExecute)));
        ExpectDiagnostic(graph, RenderGraphResultCode::DuplicatePassId);
    }
}

void TestFrameResetInvalidatesOldHandles() {
    RenderGraph graph{};
    assert(graph.BeginFrame(10u));
    const auto oldToken = graph.GetFrameToken();
    const RenderGraphResourceId oldResource = graph.AddResource(MakeSceneDepthResource("OldResource"));
    assert(oldResource.IsValid());
    assert(graph.IsResourceValid(oldResource));

    assert(graph.BeginFrame(11u));
    const auto newToken = graph.GetFrameToken();
    assert(newToken.generation != oldToken.generation);
    assert(!graph.IsResourceValid(oldResource));
    assert(!graph
                .FindSemanticResource(RenderGraphSemantic::SceneDepth,
                                      HIKARI::RENDER3D::GRAPH::RenderGraphSemanticPhase::Imported)
                .IsValid());
    assert(graph.GetResourceCount() == 0u);
    assert(graph.GetPassCount() == 0u);

    const RenderGraphResourceId newResource = graph.AddResource(MakeSceneDepthResource("NewResource"));
    assert(newResource.IsValid());
    assert(graph.FindSemanticResource(RenderGraphSemantic::SceneDepth,
                                      HIKARI::RENDER3D::GRAPH::RenderGraphSemanticPhase::Imported) == newResource);
    assert(newResource.generation == newToken.generation);
    assert(newResource.graphGeneration != oldResource.graphGeneration);
    assert(newResource.index == oldResource.index);
    assert(!graph.IsResourceValid(oldResource));
}

void TestGraphLeaseRejectsCrossGraphHandles() {
    const HIKARI::RENDER3D::RenderFrameToken sharedFrame =
        HIKARI::RENDER3D::AllocateRenderFrameToken(15u, HIKARI::RENDER3D::kPrimaryRenderViewId);
    RenderGraph firstGraph{};
    RenderGraph secondGraph{};
    assert(firstGraph.BeginFrame(sharedFrame));
    assert(secondGraph.BeginFrame(sharedFrame));

    const RenderGraphResourceId firstResource = firstGraph.AddResource(MakeInternalResource("FirstGraphResource"));
    const RenderGraphResourceId secondResource = secondGraph.AddResource(MakeInternalResource("SecondGraphResource"));
    assert(firstResource.IsValid());
    assert(secondResource.IsValid());
    assert(firstResource.index == secondResource.index);
    assert(firstResource.generation == secondResource.generation);
    assert(firstResource.graphGeneration != secondResource.graphGeneration);
    assert(firstGraph.IsResourceValid(firstResource));
    assert(secondGraph.IsResourceValid(secondResource));
    assert(!firstGraph.IsResourceValid(secondResource));
    assert(!secondGraph.IsResourceValid(firstResource));
}

void TestFirstAndLastUse() {
    RenderGraph graph{};
    assert(graph.BeginFrame(12u));
    const RenderGraphResourceId resource = graph.AddResource(MakeInternalResource("UseLifetime"));
    assert(resource.IsValid());

    const std::array<RenderGraphResourceUse, 1u> writes{
        RenderGraphResourceUse{resource, RenderGraphAccess::WriteColor}};
    const std::array<RenderGraphResourceUse, 1u> reads{
        RenderGraphResourceUse{resource, RenderGraphAccess::ReadSampled}};
    assert(
        graph.AddPass(MakePass("LifetimeProducer", &NoopExecute, {}, std::span<const RenderGraphResourceUse>(writes))));
    assert(graph.AddPass(MakePass("LifetimeReaderA", &NoopExecute, std::span<const RenderGraphResourceUse>(reads))));
    assert(graph.AddPass(MakePass("LifetimeReaderB", &NoopExecute, std::span<const RenderGraphResourceUse>(reads))));
    assert(graph.Compile());

    RenderGraphResourceInfo info{};
    assert(graph.GetResourceInfo(resource, info));
    assert(info.writerPassIndex == 0u);
    assert(info.firstUseOrder == 0u);
    assert(info.lastUseOrder == 2u);

    assert(graph.AddPass(MakePass("LifetimeIndependent", &NoopExecute)));
    assert(!graph.IsCompiled());
    assert(graph.GetResourceInfo(resource, info));
    assert(info.writerPassIndex == HIKARI::RENDER3D::GRAPH::kInvalidRenderGraphIndex);
    assert(info.firstUseOrder == HIKARI::RENDER3D::GRAPH::kInvalidRenderGraphIndex);
    assert(info.lastUseOrder == HIKARI::RENDER3D::GRAPH::kInvalidRenderGraphIndex);
    assert(graph.Compile());
}

void TestExecuteFailure() {
    RenderGraph graph{};
    assert(graph.BeginFrame(13u));
    assert(graph.AddPass(MakePass("FailingPass", &FailExecute)));
    assert(graph.Compile());
    assert(!graph.Execute());
    ExpectDiagnostic(graph, RenderGraphResultCode::ExecuteFailed);
    assert(graph.GetDiagnostic().passIndex == 0u);
}

struct MutationProbe {
    RenderGraphResultCode resourceCode = RenderGraphResultCode::Success;
    RenderGraphResultCode passCode = RenderGraphResultCode::Success;
    RenderGraphResultCode frameCode = RenderGraphResultCode::Success;
    bool resourceRejected = false;
    bool passRejected = false;
    bool frameRejected = false;
};

bool MutationExecute(const RenderGraphPassContext& context, void* userData) noexcept {
    auto* probe = static_cast<MutationProbe*>(userData);
    assert(context.graph != nullptr);
    RenderGraph* graph = const_cast<RenderGraph*>(context.graph);

    probe->resourceRejected = !graph->AddResource(MakeInternalResource("RejectedDuringExecute", true)).IsValid();
    probe->resourceCode = graph->GetDiagnostic().code;

    probe->passRejected = !graph->AddPass(MakePass("RejectedPass", &NoopExecute));
    probe->passCode = graph->GetDiagnostic().code;

    probe->frameRejected = !graph->BeginFrame(context.frame.frameIndex + 1u, context.frame.viewId);
    probe->frameCode = graph->GetDiagnostic().code;
    return true;
}

void TestMutationDuringExecuteIsRejected() {
    RenderGraph graph{};
    assert(graph.BeginFrame(14u));
    MutationProbe probe{};
    assert(graph.AddPass(MakePass("MutationProbe", &MutationExecute, {}, {}, &probe)));
    assert(graph.Compile());

    const auto tokenBeforeExecute = graph.GetFrameToken();
    assert(graph.Execute());
    assert(probe.resourceRejected);
    assert(probe.passRejected);
    assert(probe.frameRejected);
    assert(probe.resourceCode == RenderGraphResultCode::MutationDuringExecution);
    assert(probe.passCode == RenderGraphResultCode::MutationDuringExecution);
    assert(probe.frameCode == RenderGraphResultCode::MutationDuringExecution);
    assert(graph.GetResourceCount() == 0u);
    assert(graph.GetPassCount() == 1u);
    assert(graph.GetFrameToken().frameIndex == tokenBeforeExecute.frameIndex);
    assert(graph.GetFrameToken().generation == tokenBeforeExecute.generation);
    ExpectDiagnostic(graph, RenderGraphResultCode::Success);
}

} // namespace

int main() {
    using namespace HIKARI::RENDER3D::GPUDRIVEN;
    GpuDrivenDrawCommandRange range;
    assert(!range.TraditionalStream(false));
    std::vector<HIKARI::VFX::VariantKey> variants(2);
    GpuDrivenTraditionalIndirectView view;
    view.bucketVariants = &variants;
    range.traditionalIndirect = &view;
    range.gpuAuthored = range.gpuCounterBacked = true;
    range.argumentBuffer = range.counterBuffer = reinterpret_cast<ID3D12Resource*>(uintptr_t{1});
    range.commandSignature = reinterpret_cast<ID3D12CommandSignature*>(uintptr_t{1});
    range.argumentBucketStride = 64;
    range.counterBucketStride = 4;
    range.commandCount = range.staticCommandCount = 200;
    range.commandBucketCapacity = 100;
    range.commandBucketCount = 3;
    const auto stream = range.TraditionalStream(false);
    assert(stream && stream->commandLimit == 100 && stream->bucketCount == 2);
    assert(!range.TraditionalStream(true)); // Static readiness does not imply skinned readiness.
    range.skinnedArgumentBuffer = range.argumentBuffer;
    range.skinnedCommandSignature = range.commandSignature;
    range.skinnedArgumentBucketStride = 128;
    range.skinnedArgumentBufferOffset = 256;
    range.skinnedCounterBufferOffset = 32;
    range.skinnedCommandCount = 12;
    const auto skinned = range.TraditionalStream(true);
    assert(skinned && skinned->commandLimit == 12 && skinned->argumentOffset == 256 && skinned->counterOffset == 32 &&
           skinned->argumentStride == 128);
    range.counterBucketStride = 0;
    assert(!range.TraditionalStream(false) && !range.TraditionalStream(true));
    using namespace HIKARI::RENDER3D::PIPELINE::INTERNAL;
    for (uint32_t i = 0; i < static_cast<uint32_t>(HIKARI::GameProjectRenderSurfaceSemantic::Count); ++i) {
        const auto value = static_cast<HIKARI::GameProjectRenderSurfaceSemantic>(i);
        assert(MapProjectRenderSemantic(MapProjectRenderSemantic(value)) == value);
    }
    assert(MapProjectRenderSemantic(static_cast<HIKARI::GameProjectRenderSurfaceSemantic>(999)) ==
           RenderGraphSemantic::None);
    TestExplicitFrameBoundary();
    TestStableTopologicalOrder();
    TestProducerAddedAfterConsumer();
    TestMissingProducer();
    TestDuplicateWriter();
    TestCycle();
    TestDuplicateResourceAndPassIdentity();
    TestFrameResetInvalidatesOldHandles();
    TestFirstAndLastUse();
    TestExecuteFailure();
    TestMutationDuringExecuteIsRejected();
    TestGraphLeaseRejectsCrossGraphHandles();
    std::cout << "Render graph smoke test passed.\n";
    return 0;
}
