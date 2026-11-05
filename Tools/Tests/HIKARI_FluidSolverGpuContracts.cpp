#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <d3dx12.h>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"
#include "Internal/HIKARI_GpuContractDevice.h"

// 体積流体の解算（流体烟雾第 3 步の完了条件）：投影後の散度、消散なしの閉じた箱での密度の総量、30/60/144 fps で
// 同じゲーム時間なら同じ結果、一時停止と顿帧で解算が止まる。WARP で 16³ の小さな域を解く。
namespace {
using namespace HIKARI;
using namespace RENDER3D::FLUID;
using Microsoft::WRL::ComPtr;

struct FluidGpu {
    TESTS::INTERNAL::GpuContractDevice gpu;
    ComPtr<ID3D12DescriptorHeap> heap;
    GFX::Context graphics{};
    uint64_t fence = 0u;
    uint32_t frame = 0u;

    FluidGpu() {
        const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                  GFX::DESCRIPTOR::kSrvHeapCapacity,
                                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
        assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
        graphics.device = gpu.device.Get();
        graphics.deviceGeneration = 1;
        graphics.cmdList = gpu.commands.Get();
        graphics.queue = gpu.queue.Get();
        graphics.srvHeap = heap.Get();
        graphics.frameFence = gpu.fence.Get();
        RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphics);
    }

    // 1 フレームを記録して終わるまで待つ。after は同じ command list に足す記録（切片の写し）。
    template <class After>
    void Frame(FluidVolumePass& pass, RENDER3D::PARTICLES::ParticleRenderInput& input,
               const FluidFrameSettings& settings, After&& after) {
        // 再生側と同じく、描画入力の帧号は毎フレーム進む（一時停止中も）。
        input.frameIndex = frame;
        graphics.frameIndex = frame++ % GFX::kFrameResourceCount;
        graphics.currentFrameRetireFenceValue = ++fence;
        gpu.ResetCommands();
        assert(pass.PrepareFrame(graphics, &input, settings));
        after();
        gpu.SubmitAndWait(fence);
    }
    void Frame(FluidVolumePass& pass, RENDER3D::PARTICLES::ParticleRenderInput& input,
               const FluidFrameSettings& settings) {
        Frame(pass, input, settings, [] {});
    }
};

FluidDomainSubmission MakeDomain(uint64_t playbackNamespace, FluidBoundary boundary, float dissipation) {
    FluidDomainSubmission domain{};
    domain.key = {playbackNamespace, 1u, 1u, 0u};
    domain.resetSimulation = true;
    domain.settings.domainSize = {2.0f, 2.0f, 2.0f};
    domain.settings.resolution = 16u;
    domain.settings.boundary = boundary;
    domain.settings.densityDissipation = dissipation;
    domain.settings.temperatureDissipation = dissipation;
    FluidSourceSubmission source{};
    source.worldPosition = {0.0f, -0.4f, 0.0f};
    source.shape = FluidSourceShape::Sphere;
    source.radius = 0.35f;
    source.ratePerSecond = 4.0f;
    source.settings.temperature = 1.0f;
    source.settings.velocity = {0.6f, 0.0f, 0.0f};
    source.field.turbulence = 0.8f;
    domain.sources.push_back(source);
    return domain;
}

const FluidDomainStats& DomainStats(const FluidVolumePass& pass) {
    assert(pass.GetStats().perDomain.size() == 1u);
    return pass.GetStats().perDomain.front();
}

// 切片（密度、z の中央）を読み戻す。
struct SliceReadback {
    ComPtr<ID3D12Resource> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint32_t rows = 0u;

    void Record(FluidGpu& fluid, const FluidVolumePass& pass) {
        const FluidDebugSliceView view = pass.GetDebugSlice();
        assert(view.valid && view.resource != nullptr);
        const D3D12_RESOURCE_DESC description = view.resource->GetDesc();
        UINT64 bytes = 0u;
        fluid.gpu.device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, &rows, nullptr, &bytes);
        if (!buffer) {
            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            assert(SUCCEEDED(fluid.gpu.device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer))));
        }
        const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            view.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        fluid.graphics.cmdList->ResourceBarrier(1, &toCopy);
        const CD3DX12_TEXTURE_COPY_LOCATION destination(buffer.Get(), footprint);
        const CD3DX12_TEXTURE_COPY_LOCATION source(view.resource, 0);
        fluid.graphics.cmdList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const auto back = CD3DX12_RESOURCE_BARRIER::Transition(view.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        fluid.graphics.cmdList->ResourceBarrier(1, &back);
    }

    std::vector<uint16_t> Read() const {
        std::vector<uint16_t> texels;
        void* mapped = nullptr;
        assert(SUCCEEDED(buffer->Map(0, nullptr, &mapped)));
        const auto* bytes = static_cast<const uint8_t*>(mapped);
        for (uint32_t row = 0u; row < rows; ++row) {
            const auto* line =
                reinterpret_cast<const uint16_t*>(bytes + footprint.Offset + row * footprint.Footprint.RowPitch);
            texels.insert(texels.end(), line, line + footprint.Footprint.Width * 4u);
        }
        buffer->Unmap(0, nullptr);
        return texels;
    }
};

// half float の切片の 1 texel を float へ。
float HalfToFloat(uint16_t half) {
    const uint32_t sign = (half >> 15u) & 1u, exponent = (half >> 10u) & 31u, mantissa = half & 1023u;
    if (exponent == 0u)
        return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31u)
        return mantissa != 0u ? NAN : (sign ? -INFINITY : INFINITY);
    return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(mantissa + 1024u), static_cast<int>(exponent) - 25);
}

// 開いた境界の烟柱（Smoke_Puff と同じ設定：4 m の域、源は下寄りの球、浮力で上がる）。2 秒注入し続けて、投影後の
// 散度が残らず、速度が浮力と注入の速さの程度に収まり、烟が域に柱として残る。多重网格が 3 段以上になる分辨率で、
// 開いた境界（面の上で圧力 0）の粗い段の修正が発散しないことを見る。
void OpenPlume(FluidGpu& fluid, uint64_t playbackNamespace, uint32_t resolution, float minimumDensity) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    FluidDomainSubmission domain{};
    domain.key = {playbackNamespace, 1u, 1u, 0u};
    domain.resetSimulation = true;
    domain.worldPosition = {0.0f, 2.0f, 4.0f};
    domain.settings.domainSize = {4.0f, 4.0f, 4.0f};
    domain.settings.resolution = resolution;
    domain.settings.boundary = FluidBoundary::Open;
    domain.settings.vorticity = 0.3f;
    domain.settings.buoyancy = 1.5f;
    domain.settings.densityDissipation = 0.1f;
    domain.settings.temperatureDissipation = 0.5f;
    FluidSourceSubmission source{};
    source.worldPosition = {0.0f, 0.7f, 4.0f};
    source.shape = FluidSourceShape::Sphere;
    source.radius = 0.35f;
    source.ratePerSecond = 6.0f;
    source.settings.temperature = 1.0f;
    source.settings.velocity = {0.0f, 0.8f, 0.0f};
    source.field.turbulence = 0.6f;
    source.field.frequency = 2.0f;
    source.field.evolution = 0.5f;
    domain.sources.push_back(source);
    input.fluids.push_back(domain);
    FluidFrameSettings settings{};
    settings.diagnostics = true;
    pass.RequestDebugSlice({domain.key, FluidDebugField::Velocity, 0u, 0.5f});
    SliceReadback readback;
    float maxDivergence = 0.0f;
    for (uint32_t frame = 0u; frame < 120u; ++frame) {
        input.fluids[0].deltaSeconds = 1.0f / 60.0f;
        fluid.Frame(pass, input, settings, [&] {
            if (frame + 1u == 120u)
                readback.Record(fluid, pass);
        });
        input.fluids[0].resetSimulation = false;
        maxDivergence = (std::max)(maxDivergence, DomainStats(pass).maxDivergence);
    }
    float maxSpeed = 0.0f;
    const std::vector<uint16_t> texels = readback.Read();
    for (size_t index = 3u; index < texels.size(); index += 4u)
        maxSpeed = (std::max)(maxSpeed, HalfToFloat(texels[index]));
    const FluidDomainStats& stats = DomainStats(pass);
    std::fprintf(stderr, "fluid open plume %u: density %.3f maxDivergence %.4f maxSpeed %.3f bricks %u/%u\n",
                 resolution, stats.densityTotal, maxDivergence, maxSpeed, stats.activeBricks, stats.totalBricks);
    // 子步ごとの V 循環 2 回で残る散度は 64 格で速度 / 格の約 0.5%（0.13 / s）。発散すると 1e6 を超える。
    assert(stats.diagnosticsValid && maxDivergence < 0.25f && maxSpeed < 8.0f);
    assert(stats.densityTotal > minimumDensity && stats.activeBricks > 1u);
    pass.Shutdown();
}

// 決まったゲーム時間を frame の長さで刻んで解き、最後の密度の切片を返す。
std::vector<uint16_t> SolveAtFrameRate(FluidGpu& fluid, uint64_t playbackNamespace, double frameSeconds,
                                       uint32_t frames, uint64_t& outSteps) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakeDomain(playbackNamespace, FluidBoundary::Open, 0.1f));
    pass.RequestDebugSlice({input.fluids[0].key, FluidDebugField::Density, 2u, 0.5f});
    SliceReadback readback;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        input.fluids[0].deltaSeconds = static_cast<float>(frameSeconds);
        const bool last = frame + 1u == frames;
        fluid.Frame(pass, input, {}, [&] {
            if (last)
                readback.Record(fluid, pass);
        });
        input.fluids[0].resetSimulation = false;
    }
    outSteps = DomainStats(pass).completedSteps;
    pass.Shutdown();
    return readback.Read();
}
} // namespace

void CheckFluidSolver() {
    assert(CONTENT::GetEngineContentContext().IsOpen() ||
           CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    // 子步の時計：同じゲーム時間なら刻み方に関わらず同じ数、上限を超えた分は捨てる。
    for (const double frameSeconds : {1.0 / 30.0, 1.0 / 60.0, 1.0 / 144.0}) {
        FluidStepClock clock{};
        const uint32_t frames = static_cast<uint32_t>(std::lround(1.0 / frameSeconds));
        for (uint32_t frame = 0u; frame < frames; ++frame)
            (void)clock.Advance(frameSeconds, 1.0 / 60.0, 3u);
        assert(clock.completedSteps == 60u);
    }
    FluidStepClock capped{};
    assert(capped.Advance(0.5, 1.0 / 60.0, 3u) == 3u && capped.pendingSeconds == 0.0);
    assert(capped.Advance(0.0, 1.0 / 60.0, 3u) == 0u);
    // 格子：切り上げた後でも最も粗い段は各辺 8 以下で、各辺は砖块と段の倍率で割り切れる（半端な分辨率も）。
    for (const uint32_t resolution : {16u, 17u, 64u, 65u, 66u, 71u, 136u, 143u, 144u, 256u}) {
        const FluidGridLayout layout = ResolveFluidGridLayout({144.0f, 48.0f, 100.0f}, resolution);
        const uint32_t unit = (std::max)(8u, 1u << (layout.levels - 1u));
        for (const uint32_t cells : layout.cells)
            assert(cells % unit == 0u && (cells >> (layout.levels - 1u)) <= 8u);
    }

    FluidGpu fluid;
    // 閉じた箱：注入して止め、消散なしで 1 秒回した時の密度の総量と、投影後の散度。静止した烟は量が変わらない。
    // 半ラグランジュ系の平流は保存形ではなく、速い旋回（加速し続ける旋涡）や壁へぶつかって溜まる流れ（天井に溜まる
    // 上昇流）で量が増減する。16³ の粗い格子で 1 秒、旋回は 6%、乱流の烟柱は 12% までを許す。
    const auto closedBox = [&](int mode, float tolerance) {
        const bool plume = mode == 2;
        FluidVolumePass pass;
        RENDER3D::PARTICLES::ParticleRenderInput input{};
        input.fluids.push_back(MakeDomain(7u + static_cast<uint64_t>(mode), FluidBoundary::Closed, 0.0f));
        if (!plume) {
            // 中心のまわりを回るだけの烟（浮力なし、旋涡の力場、源は中心から 0.3 m 横）。
            FluidDomainSubmission& domain = input.fluids[0];
            domain.settings.buoyancy = 0.0f;
            domain.settings.vorticity = 0.0f;
            FluidSourceSubmission& source = domain.sources[0];
            source.worldPosition = {0.3f, 0.0f, 0.0f};
            source.radius = 0.2f;
            source.settings.velocity = {};
            source.field = {};
            source.field.center = {-0.3f, 0.0f, 0.0f};
            source.field.radius = 0.9f;
            source.field.vortex = mode == 1 ? 1.5f : 0.0f;
        }
        FluidFrameSettings settings{};
        settings.diagnostics = true;
        for (uint32_t frame = 0u; frame < 20u; ++frame) {
            input.fluids[0].deltaSeconds = 1.0f / 60.0f;
            fluid.Frame(pass, input, settings);
            input.fluids[0].resetSimulation = false;
        }
        input.fluids[0].sources[0].ratePerSecond = 0.0f;
        fluid.Frame(pass, input, settings);
        fluid.Frame(pass, input, settings);
        const float injected = DomainStats(pass).densityTotal;
        float maxDivergence = 0.0f;
        for (uint32_t frame = 0u; frame < 60u; ++frame) {
            fluid.Frame(pass, input, settings);
            maxDivergence = (std::max)(maxDivergence, DomainStats(pass).maxDivergence);
        }
        const FluidDomainStats& stats = DomainStats(pass);
        const float drift = std::abs(stats.densityTotal - injected) / (std::max)(injected, 1.0e-6f);
        std::fprintf(
            stderr, "fluid closed box (%s): injected %.4f after %.4f drift %.4f maxDivergence %.5f steps %llu\n",
            mode == 2   ? "plume"
            : mode == 1 ? "swirl"
                        : "still",
            injected, stats.densityTotal, drift, maxDivergence, static_cast<unsigned long long>(stats.completedSteps));
        assert(stats.diagnosticsValid && injected > 0.1f);
        assert(drift < tolerance);
        assert(maxDivergence < 0.05f);
        // 一時停止・顿帧（ゲーム時間 0）は子步を進めない。
        const uint64_t frozen = stats.completedSteps;
        input.fluids[0].deltaSeconds = 0.0f;
        fluid.Frame(pass, input, settings);
        fluid.Frame(pass, input, settings);
        assert(DomainStats(pass).completedSteps == frozen && DomainStats(pass).stepsThisFrame == 0u);
        pass.Shutdown();
    };
    // 密度の総量は格の値の和（格の数に比例）。実測 32 格 1091、64 格 8194 の半分を下限にする。
    OpenPlume(fluid, 40u, 32u, 500.0f);
    OpenPlume(fluid, 41u, 64u, 4000.0f);
    closedBox(0, 1.0e-4f);
    closedBox(1, 0.06f);
    closedBox(2, 0.12f);
    // 30 / 60 / 144 fps で同じ 1 秒を解くと、子步の数も密度の切片も同じ。
    uint64_t steps30 = 0u, steps60 = 0u, steps144 = 0u;
    const auto at30 = SolveAtFrameRate(fluid, 30u, 1.0 / 30.0, 30u, steps30);
    const auto at60 = SolveAtFrameRate(fluid, 60u, 1.0 / 60.0, 60u, steps60);
    const auto at144 = SolveAtFrameRate(fluid, 144u, 1.0 / 144.0, 144u, steps144);
    std::fprintf(stderr, "fluid frame rates: steps %llu %llu %llu\n", static_cast<unsigned long long>(steps30),
                 static_cast<unsigned long long>(steps60), static_cast<unsigned long long>(steps144));
    assert(steps30 == 60u && steps60 == 60u && steps144 == 60u);
    assert(at30 == at60 && at60 == at144);
    bool anyDensity = false;
    for (size_t index = 0; index < at60.size(); index += 4u)
        anyDensity = anyDensity || at60[index] != 0u;
    assert(anyDensity);
}
