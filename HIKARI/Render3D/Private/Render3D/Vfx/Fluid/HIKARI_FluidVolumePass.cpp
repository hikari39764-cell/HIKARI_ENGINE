#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

#include <d3dx12.h>

#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidRender.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"

namespace HIKARI::RENDER3D::FLUID {
namespace {
using INTERNAL::FluidDomainResources;
using INTERNAL::FluidRecorder;
using PARTICLES::ParticleEmitterInstanceKey;

// 作り直した直後の預熱で 1 フレームに進める子步の上限（預熱の間は域を描かない）。
constexpr uint32_t kPrewarmStepsPerFrame = 48u;
// 局所空間の慣性力に使う挂点の加速度の上限 [m/s²]（顿帧の後の 1 フレームで烟が吹き飛ばないように）。
constexpr float kMaximumFrameAcceleration = 400.0f;
// 占有：この密度を超える砖块を「烟がある」とみなす。
constexpr float kOccupiedDensity = 1.0e-3f;

struct alignas(16) DomainConstants {
    uint32_t size[4];
    float cell[4];
    float motion[4];
    float dissipation[4];
    float up[4];
    float inertia[4];
};
static_assert(sizeof(DomainConstants) == 96u);

struct DomainState {
    ParticleEmitterInstanceKey key{};
    FluidDomainResources resources{};
    MATH::Vec3 layoutSize{};
    uint32_t layoutResolution = 0u;
    FluidStepClock clock{};
    double prewarmRemaining = 0.0;
    uint32_t velocityIndex = 0u;
    uint32_t densityIndex = 0u;
    bool needsClear = true;
    // 最後に時間を進めた描画入力の帧号。同じ入力を別の視図や交付待ちで再び受けても二度は進めない。
    uint64_t lastSimulatedFrame = UINT64_MAX;
    // 解く空間（世界空間の域は作り直した時の変換に固定、局所空間の域は毎フレームの変換）。
    MATH::Vec3 framePosition{};
    MATH::Quat frameRotation = MATH::Quat::Identity();
    MATH::Vec3 previousPosition{};
    MATH::Vec3 previousVelocity{};
    uint32_t motionSamples = 0u;
    // 解いたゲーム時間（細部のノイズを流れに沿って運ぶ時計。float の精度のため周期で折る）。
    float simulationSeconds = 0.0f;
    std::array<float, kMaximumFluidSourcesPerDomain> pendingBurst{};
    std::array<uint64_t, GFX::kFrameResourceCount> readbackFence{};
    std::array<uint64_t, GFX::kFrameResourceCount> readbackSteps{};
    std::array<bool, GFX::kFrameResourceCount> readbackDiagnostics{};
    std::array<bool, GFX::kFrameResourceCount> readbackPending{};
    FluidDomainStats stats{};
};

bool IsFenceComplete(const GFX::Context& context, uint64_t fence) noexcept {
    return fence == 0u || (context.frameFence != nullptr && context.frameFence->GetCompletedValue() >= fence);
}
} // namespace

struct FluidVolumePass::Impl {
    INTERNAL::FluidSolverPipeline pipeline{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads{};
    std::vector<std::unique_ptr<DomainState>> domains{};
    GFX::Context context{};
    FluidVolumePassStats stats{};
    uint64_t statsFrameFence = 0u;
    std::optional<FluidDebugSliceRequest> sliceRequest{};
    INTERNAL::FluidVolume slice{};
    FluidDebugSliceView sliceView{};
    INTERNAL::FluidRenderer renderer{};
    // 最後の PrepareFrame で解いた、描く域（預熱の済んだもの）。続く RenderVolumes がその視図へ描く。
    std::vector<INTERNAL::FluidRenderDomain> renderList{};

    DomainState* Find(const ParticleEmitterInstanceKey& key) noexcept {
        for (auto& domain : domains)
            if (domain->key == key)
                return domain.get();
        return nullptr;
    }

    void Retire(size_t index) {
        INTERNAL::RetireFluidDomainResources(context, domains[index]->resources);
        domains.erase(domains.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void ReadCompleted(DomainState& domain);
    bool PrepareDomain(DomainState& domain, const FluidDomainSubmission& submission, const FluidQualityPolicy& policy);
    void Solve(DomainState& domain, const FluidDomainSubmission& submission, const FluidQualityPolicy& policy,
               const FluidFrameSettings& settings, bool advance);
};

// 回読の輪で GPU が終えた分を読む：砖块ごとの最大密度（占有）と、診断を記録した時は散度・密度の総量。
void FluidVolumePass::Impl::ReadCompleted(DomainState& domain) {
    for (uint32_t slot = 0u; slot < GFX::kFrameResourceCount; ++slot) {
        if (!domain.readbackPending[slot] || !IsFenceComplete(context, domain.readbackFence[slot]))
            continue;
        // 届いた槽は必ず空ける（次のフレームの回読に使う）。読んだものより古い回読は捨てる。
        domain.readbackPending[slot] = false;
        if (domain.readbackSteps[slot] < domain.stats.readbackSteps)
            continue;
        ID3D12Resource* readback = domain.resources.readback[slot].Get();
        const D3D12_RANGE range{0, domain.resources.statsBytes};
        void* mapped = nullptr;
        if (FAILED(readback->Map(0, &range, &mapped)) || mapped == nullptr)
            continue;
        const auto* words = static_cast<const float*>(mapped);
        const uint32_t bricks = domain.resources.BrickCount();
        uint32_t active = 0u;
        for (uint32_t brick = 0u; brick < bricks; ++brick)
            active += words[brick] > kOccupiedDensity ? 1u : 0u;
        domain.stats.activeBricks = active;
        domain.stats.totalBricks = bricks;
        domain.stats.readbackSteps = domain.readbackSteps[slot];
        domain.stats.diagnosticsValid = domain.readbackDiagnostics[slot];
        if (domain.readbackDiagnostics[slot]) {
            const float* diagnostics = words + bricks;
            float maxDivergence = 0.0f;
            double total = 0.0;
            for (uint32_t brick = 0u; brick < bricks; ++brick) {
                maxDivergence = (std::max)(maxDivergence, diagnostics[brick * 2u]);
                total += diagnostics[brick * 2u + 1u];
            }
            domain.stats.maxDivergence = maxDivergence;
            domain.stats.densityTotal = static_cast<float>(total);
        }
        const D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
    }
}

// 作り直しの判断：初めて・再生側の作り直し・格子の大きさ（域の大きさ、分辨率、画質の倍率）が変わった時。
bool FluidVolumePass::Impl::PrepareDomain(DomainState& domain, const FluidDomainSubmission& submission,
                                          const FluidQualityPolicy& policy) {
    // 域の大きさは作者の domainSize だけで決める（層の拡縮・その動画で格子を作り直さない）。
    const MATH::Vec3 size = submission.settings.domainSize;
    const uint32_t resolution = std::clamp(
        static_cast<uint32_t>(std::lround(static_cast<float>(submission.settings.resolution) * policy.resolutionScale)),
        kFluidResolutionMin, kFluidResolutionMax);
    const bool relayout = !domain.resources.stats || domain.layoutResolution != resolution ||
                          std::abs(domain.layoutSize.x - size.x) > 1.0e-4f ||
                          std::abs(domain.layoutSize.y - size.y) > 1.0e-4f ||
                          std::abs(domain.layoutSize.z - size.z) > 1.0e-4f;
    if (relayout) {
        INTERNAL::RetireFluidDomainResources(context, domain.resources);
        if (!INTERNAL::CreateFluidDomainResources(context.device, ResolveFluidGridLayout(size, resolution),
                                                  domain.resources)) {
            INTERNAL::RetireFluidDomainResources(context, domain.resources);
            ++stats.resourceFailures;
            return false;
        }
        domain.layoutSize = size;
        domain.layoutResolution = resolution;
        domain.readbackPending = {};
    }
    if (relayout || submission.resetSimulation) {
        domain.needsClear = true;
        domain.clock = {};
        domain.prewarmRemaining = (std::max)(0.0f, submission.settings.prewarmSeconds);
        domain.velocityIndex = domain.densityIndex = 0u;
        domain.framePosition = submission.worldPosition;
        domain.frameRotation = submission.worldRotation;
        domain.motionSamples = 0u;
        domain.pendingBurst = {};
        domain.stats = {};
        // 再起動の前に記録した回読（大きな子步数）が新しい統計を上書きしないよう、輪ごと捨てる。
        domain.readbackPending = {};
    }
    return true;
}

void FluidVolumePass::Impl::Solve(DomainState& domain, const FluidDomainSubmission& submission,
                                  const FluidQualityPolicy& policy, const FluidFrameSettings& settings, bool advance) {
    const FluidDomainSettings& authored = submission.settings;
    const FluidGridLayout& layout = domain.resources.layout;
    const double step = 1.0 / static_cast<double>(std::clamp(authored.substepRate, 15.0f, 240.0f));
    // 局所空間の域は毎フレーム層の変換へ付いていき、挂点の加速度を逆向きの力として入れる（冲刺の拖尾）。
    MATH::Vec3 inertia{};
    if (submission.localSpace) {
        domain.framePosition = submission.worldPosition;
        domain.frameRotation = submission.worldRotation;
        if (advance && submission.deltaSeconds > 0.0f) {
            const float dt = submission.deltaSeconds;
            const MATH::Vec3 velocity = (submission.worldPosition - domain.previousPosition) * (1.0f / dt);
            MATH::Vec3 acceleration =
                domain.motionSamples >= 2u ? (velocity - domain.previousVelocity) * (1.0f / dt) : MATH::Vec3{};
            const float magnitude = MATH::Length(acceleration);
            if (magnitude > kMaximumFrameAcceleration)
                acceleration = acceleration * (kMaximumFrameAcceleration / magnitude);
            inertia = INTERNAL::RotateToFluidLocal(domain.frameRotation, acceleration) * -authored.inertia;
            if (domain.motionSamples >= 1u)
                domain.previousVelocity = velocity;
            domain.motionSamples = (std::min)(domain.motionSamples + 1u, 2u);
        }
        domain.previousPosition = submission.worldPosition;
    }
    for (size_t index = 0; advance && index < submission.sources.size() && index < domain.pendingBurst.size(); ++index)
        domain.pendingBurst[index] += submission.sources[index].burstDensity;

    uint32_t prewarmSteps = 0u;
    if (advance && domain.prewarmRemaining > 0.0) {
        prewarmSteps = (std::min)(kPrewarmStepsPerFrame,
                                  static_cast<uint32_t>(std::ceil(domain.prewarmRemaining / step - 1.0e-7)));
        domain.prewarmRemaining = (std::max)(0.0, domain.prewarmRemaining - prewarmSteps * step);
        domain.clock.completedSteps += prewarmSteps;
    }
    const uint32_t steps = prewarmSteps + (domain.prewarmRemaining > 0.0
                                               ? 0u
                                               : domain.clock.Advance(advance ? submission.deltaSeconds : 0.0, step,
                                                                      (std::max)(authored.maxSubsteps, 1u)));
    domain.stats.key = submission.key;
    std::copy(std::begin(layout.cells), std::end(layout.cells), domain.stats.cells);
    domain.stats.levels = layout.levels;
    domain.stats.bytes = domain.resources.bytes;
    domain.stats.completedSteps = domain.clock.completedSteps;
    domain.stats.stepsThisFrame = steps;
    domain.stats.prewarmRemainingSeconds = static_cast<float>(domain.prewarmRemaining);
    domain.simulationSeconds =
        static_cast<float>(std::fmod(static_cast<double>(domain.clock.completedSteps) * step, 4096.0));

    auto& upload = uploads[context.frameIndex % GFX::kFrameResourceCount];
    ID3D12GraphicsCommandList* commands = context.cmdList;
    const MATH::Vec3 up = INTERNAL::RotateToFluidLocal(domain.frameRotation, MATH::Vec3{0.0f, 1.0f, 0.0f});
    const auto uploadConstants = [&](uint64_t stepIndex) -> D3D12_GPU_VIRTUAL_ADDRESS {
        DomainConstants constants{};
        constants.size[0] = layout.cells[0];
        constants.size[1] = layout.cells[1];
        constants.size[2] = layout.cells[2];
        constants.size[3] = static_cast<uint32_t>(
            (std::min)(submission.sources.size(), static_cast<size_t>(kMaximumFluidSourcesPerDomain)));
        constants.cell[0] = layout.cellSize;
        constants.cell[1] = 1.0f / layout.cellSize;
        constants.cell[2] = static_cast<float>(step);
        constants.cell[3] = static_cast<float>(std::fmod(static_cast<double>(stepIndex) * step, 65536.0));
        constants.motion[0] = authored.boundary == FluidBoundary::Closed ? 1.0f : 0.0f;
        constants.motion[1] = authored.vorticity;
        constants.motion[2] = authored.buoyancy;
        constants.motion[3] = authored.weight;
        constants.dissipation[0] = authored.densityDissipation;
        constants.dissipation[1] = authored.temperatureDissipation;
        constants.up[0] = up.x;
        constants.up[1] = up.y;
        constants.up[2] = up.z;
        constants.inertia[0] = inertia.x;
        constants.inertia[1] = inertia.y;
        constants.inertia[2] = inertia.z;
        const auto allocation = upload.Allocate(sizeof(constants));
        if (!allocation)
            return 0u;
        std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
        return allocation.gpuAddress;
    };
    const auto uploadSources = [&](bool firstStep) -> D3D12_GPU_VIRTUAL_ADDRESS {
        const size_t count = (std::max<size_t>)(1u, (std::min)(submission.sources.size(),
                                                               static_cast<size_t>(kMaximumFluidSourcesPerDomain)));
        const auto allocation = upload.Allocate(count * sizeof(INTERNAL::FluidSourceGpu));
        if (!allocation)
            return 0u;
        auto* table = static_cast<INTERNAL::FluidSourceGpu*>(allocation.cpuAddress);
        std::memset(table, 0, count * sizeof(INTERNAL::FluidSourceGpu));
        for (size_t index = 0; index < submission.sources.size() && index < count; ++index) {
            table[index] =
                INTERNAL::PackFluidSource(submission.sources[index], domain.framePosition, domain.frameRotation,
                                          layout.cellSize, firstStep ? domain.pendingBurst[index] : 0.0f);
        }
        return allocation.gpuAddress;
    };

    const D3D12_GPU_VIRTUAL_ADDRESS baseConstants = uploadConstants(domain.clock.completedSteps);
    if (baseConstants == 0u)
        return;
    FluidRecorder recorder(commands, pipeline, domain.resources, baseConstants);
    if (domain.needsClear) {
        for (auto& volume : domain.resources.velocity)
            recorder.Clear(volume);
        for (auto& volume : domain.resources.density)
            recorder.Clear(volume);
        for (auto& volume : domain.resources.pressure)
            recorder.Clear(volume);
        domain.needsClear = false;
    }
    const uint64_t firstStep = domain.clock.completedSteps - steps;
    for (uint32_t index = 0u; index < steps; ++index) {
        const D3D12_GPU_VIRTUAL_ADDRESS constants = uploadConstants(firstStep + index);
        const D3D12_GPU_VIRTUAL_ADDRESS sources = uploadSources(index == 0u);
        if (constants == 0u || sources == 0u)
            break;
        FluidRecorder stepRecorder(commands, pipeline, domain.resources, constants);
        stepRecorder.Substep(domain.velocityIndex, domain.densityIndex, sources, policy.vCycles);
        if (index == 0u)
            domain.pendingBurst = {};
    }
    stats.stepsThisFrame += steps;

    FluidRecorder statsRecorder(commands, pipeline, domain.resources, baseConstants);
    statsRecorder.Occupancy(domain.densityIndex);
    if (settings.diagnostics)
        statsRecorder.Diagnostics(domain.velocityIndex, domain.densityIndex);
    const uint32_t slot = context.frameIndex % GFX::kFrameResourceCount;
    if (!domain.readbackPending[slot]) {
        statsRecorder.CopyStats(domain.resources.readback[slot].Get());
        domain.readbackPending[slot] = true;
        domain.readbackFence[slot] = context.currentFrameRetireFenceValue;
        domain.readbackSteps[slot] = domain.clock.completedSteps;
        domain.readbackDiagnostics[slot] = settings.diagnostics;
    }
    if (sliceRequest && sliceRequest->key == domain.key) {
        const uint32_t axis = (std::min)(sliceRequest->axis, 2u);
        const uint32_t u = axis == 0u ? 1u : 0u, v = axis == 2u ? 1u : 2u;
        const uint32_t width = layout.cells[u], height = layout.cells[v];
        if (slice.size[0] != width || slice.size[1] != height) {
            INTERNAL::RetireVolume(context, slice);
            if (!INTERNAL::CreateFluidSliceTexture(context.device, width, height, slice))
                INTERNAL::RetireVolume(context, slice);
        }
        if (slice.resource) {
            const uint32_t index =
                (std::min)(static_cast<uint32_t>(std::clamp(sliceRequest->position, 0.0f, 1.0f) * layout.cells[axis]),
                           layout.cells[axis] - 1u);
            statsRecorder.Slice(domain.velocityIndex, domain.densityIndex, slice, sliceRequest->field, axis, index,
                                sliceRequest->visualize);
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(slice.resource.Get(), slice.state,
                                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commands->ResourceBarrier(1, &barrier);
            slice.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            sliceView = {slice.resource.Get(), slice.srv.gpu, width, height, true};
        }
    }
}

FluidVolumePass::FluidVolumePass() : impl_(std::make_unique<Impl>()) {}
FluidVolumePass::~FluidVolumePass() = default;

bool FluidVolumePass::PrepareFrame(const GFX::Context& context, const PARTICLES::ParticleRenderInput* input,
                                   const FluidFrameSettings& settings) {
    Impl& impl = *impl_;
    // 1 フレームに主視図と編集器の視図が続けて呼ぶ。数はフレーム（GPU の退役 fence）ごとに足し合わせる。
    if (context.currentFrameRetireFenceValue != impl.statsFrameFence || context.currentFrameRetireFenceValue == 0u) {
        impl.statsFrameFence = context.currentFrameRetireFenceValue;
        impl.stats.domains = 0u;
        impl.stats.skippedDomains = 0u;
        impl.stats.stepsThisFrame = 0u;
        impl.stats.renderedDomains = 0u;
        impl.sliceView.valid = false;
    }
    impl.stats.perDomain.clear();
    impl.renderList.clear();
    if (context.device == nullptr || context.cmdList == nullptr)
        return true;
    if (impl.context.device != nullptr && impl.context.device != context.device) {
        Shutdown();
    }
    impl.context = context;
    // 透明物体の描画は流体の無いフレームでも相機体積を束ねる。
    if (!impl.renderer.BeginFrame(context))
        ++impl.stats.resourceFailures;
    if (input == nullptr) {
        // 入力の無い視図でも、他の視図の域の統計は見せ続ける（編集器が読む）。
        for (const auto& domain : impl.domains)
            impl.stats.perDomain.push_back(domain->stats);
        return true;
    }
    for (const auto& key : input->retiredEmitters)
        for (size_t index = impl.domains.size(); index-- > 0u;)
            if (impl.domains[index]->key == key)
                impl.Retire(index);
    for (auto& domain : impl.domains)
        impl.ReadCompleted(*domain);
    if (input->fluids.empty()) {
        for (const auto& domain : impl.domains)
            impl.stats.perDomain.push_back(domain->stats);
        return true;
    }
    if (!impl.pipeline.Ensure(context.device))
        return false;
    auto& uploads = impl.uploads[context.frameIndex % GFX::kFrameResourceCount];
    if (!uploads.IsInitialized() && !uploads.Init(context.device, 64u * 1024u, GFX::UPLOAD_TRAFFIC::Owner::Fluids))
        return false;
    uploads.ResetForFrame(context.currentFrameRetireFenceValue);

    const FluidQualityPolicy policy = ResolveFluidQualityPolicy(settings.quality);
    GFX::PIX::ScopedGpuEvent event(context.cmdList, GFX::PIX::kColorRender, "FluidSmoke.Simulate");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(context.cmdList, GFX::GPU_PROFILE::Pass::FluidSimulation);
    ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
    if (context.srvHeap != nullptr)
        context.cmdList->SetDescriptorHeaps(1, heaps);
    uint32_t solved = 0u;
    for (const FluidDomainSubmission& submission : input->fluids) {
        DomainState* domain = impl.Find(submission.key);
        if (domain == nullptr) {
            impl.domains.push_back(std::make_unique<DomainState>());
            domain = impl.domains.back().get();
            domain->key = submission.key;
        }
        // 画質の上限を超えた域と、見えない（层の有効区間の外の）域は解かない（状態はそのまま残る）。
        if (!submission.visible || solved >= policy.maxDomains) {
            ++impl.stats.skippedDomains;
            continue;
        }
        if (!impl.PrepareDomain(*domain, submission, policy))
            continue;
        const bool advance = domain->lastSimulatedFrame != input->frameIndex;
        domain->lastSimulatedFrame = input->frameIndex;
        impl.Solve(*domain, submission, policy, settings, advance);
        ++solved;
        if (domain->prewarmRemaining > 0.0 || impl.renderList.size() >= INTERNAL::kMaximumRenderedFluidDomains)
            continue;
        INTERNAL::FluidRenderDomain& render = impl.renderList.emplace_back();
        render.density = &domain->resources.density[domain->densityIndex];
        render.velocity = &domain->resources.velocity[domain->velocityIndex];
        render.light = &domain->resources.light;
        render.layout = domain->resources.layout;
        render.position = domain->framePosition;
        render.rotation = domain->frameRotation;
        render.settings = submission.settings;
        render.colorMultiplier = submission.colorMultiplier;
        render.simulationSeconds = domain->simulationSeconds;
    }
    impl.stats.domains += solved;
    impl.stats.bytes = 0u;
    for (const auto& domain : impl.domains) {
        impl.stats.bytes += domain->resources.bytes;
        impl.stats.perDomain.push_back(domain->stats);
    }
    return true;
}

bool FluidVolumePass::RenderVolumes(const GFX::Context& context, const FluidRenderView& view,
                                    const std::function<bool()>& bindSceneTarget) {
    Impl& impl = *impl_;
    if (impl.renderList.empty())
        return true;
    if (!impl.renderer.Render(context, view, impl.renderList, ResolveFluidQualityPolicy(view.quality).marchSteps,
                              bindSceneTarget)) {
        ++impl.stats.resourceFailures;
        return false;
    }
    impl.stats.renderedDomains = (std::max)(impl.stats.renderedDomains, static_cast<uint32_t>(impl.renderList.size()));
    return true;
}

bool FluidVolumePass::HasRenderableDomains() const noexcept { return !impl_->renderList.empty(); }
FluidCameraVolumeView FluidVolumePass::GetCameraVolume() const noexcept { return impl_->renderer.GetCameraVolume(); }

void FluidVolumePass::ReleaseNamespace(uint64_t playbackNamespace) {
    impl_->renderList.clear();
    for (size_t index = impl_->domains.size(); index-- > 0u;)
        if (impl_->domains[index]->key.playbackNamespace == playbackNamespace)
            impl_->Retire(index);
}

void FluidVolumePass::Shutdown() {
    Impl& impl = *impl_;
    impl.renderList.clear();
    impl.renderer.Shutdown(impl.context);
    while (!impl.domains.empty())
        impl.Retire(impl.domains.size() - 1u);
    INTERNAL::RetireVolume(impl.context, impl.slice);
    impl.pipeline.Retire(impl.context);
    for (auto& upload : impl.uploads)
        upload.Finalize(&impl.context);
    impl.stats = {};
    impl.sliceView = {};
}

void FluidVolumePass::RequestDebugSlice(const FluidDebugSliceRequest& request) { impl_->sliceRequest = request; }
void FluidVolumePass::ClearDebugSlice() {
    impl_->sliceRequest.reset();
    impl_->sliceView = {};
}
FluidDebugSliceView FluidVolumePass::GetDebugSlice() const noexcept { return impl_->sliceView; }
const FluidVolumePassStats& FluidVolumePass::GetStats() const noexcept { return impl_->stats; }

} // namespace HIKARI::RENDER3D::FLUID
