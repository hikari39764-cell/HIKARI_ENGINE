#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <d3dx12.h>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/HIKARI_FluidShapeVolume.h"
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

// 開いた境界の烟柱（Smoke_Puff と同じ設定：4 m の域を (0, 2, 4) に置き、源は下寄りの球、浮力で上がる）。
FluidDomainSubmission MakePlume(uint64_t playbackNamespace, uint32_t resolution) {
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
    return domain;
}

// 烟柱を 2 秒注入し続けて、投影後の散度が残らず、速度が浮力と注入の速さの程度に収まり、烟が域に柱として残る。
// 多重网格が 3 段以上になる分辨率で、開いた境界（面の上で圧力 0）の粗い段の修正が発散しないことを見る。細長い域
// （1 × 4 × 1 m、128 格 → 32 × 128 × 32）は少ない格で 5 段になる（浮游岛の 128³ と同じ段数）。
// 面ごとの境界：64 格（多重網格 4 段）で、上から吹き下ろす重い烟を 0.5 秒入れて 2 秒解く。開いた域では底から抜けて
// 減り、底だけ閉じた域（ClosedFloor）と上だけ開いた域（OpenTop）では床に溜まって残る。どの組み合わせでも投影が収束する。
// 返り値は止めた時点からの残量の割合。
float FloorBoundary(FluidGpu& fluid, uint64_t playbackNamespace, FluidBoundary boundary) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakeDomain(playbackNamespace, boundary, 0.0f));
    FluidDomainSubmission& domain = input.fluids[0];
    domain.settings.resolution = 64u;
    domain.settings.buoyancy = 0.0f;
    domain.settings.weight = 2.0f;
    FluidSourceSubmission& source = domain.sources[0];
    source.worldPosition = {0.0f, 0.5f, 0.0f};
    source.settings.temperature = 0.0f;
    source.settings.velocity = {0.0f, -1.5f, 0.0f};
    source.field = {};
    FluidFrameSettings settings{};
    settings.diagnostics = true;
    float maxDivergence = 0.0f;
    const auto step = [&] {
        domain.deltaSeconds = 1.0f / 60.0f;
        fluid.Frame(pass, input, settings);
        domain.resetSimulation = false;
        maxDivergence = (std::max)(maxDivergence, DomainStats(pass).maxDivergence);
    };
    for (uint32_t frame = 0u; frame < 30u; ++frame)
        step();
    source.ratePerSecond = 0.0f;
    step();
    step();
    const float injected = DomainStats(pass).densityTotal;
    for (uint32_t frame = 0u; frame < 120u; ++frame)
        step();
    const FluidDomainStats& stats = DomainStats(pass);
    const float kept = stats.densityTotal / (std::max)(injected, 1.0e-6f);
    std::fprintf(stderr, "fluid boundary %s: injected %.3f kept %.3f maxDivergence %.4f\n",
                 TEXT::EnumToName(kFluidBoundaryNames, boundary), injected, kept, maxDivergence);
    assert(stats.diagnosticsValid && injected > 0.1f && maxDivergence < 0.25f);
    pass.Shutdown();
    return kept;
}

// expectSmallPressureRegion：烟柱が域の一部だけを占める大きな域で、圧力を解く窓が域より小さいことも確かめる。
void OpenPlume(FluidGpu& fluid, uint64_t playbackNamespace, uint32_t resolution, float minimumDensity,
               MATH::Vec3 domainSize = {4.0f, 4.0f, 4.0f}, bool expectSmallPressureRegion = false) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakePlume(playbackNamespace, resolution));
    input.fluids[0].settings.domainSize = domainSize;
    // 発光の回読（火の光源）：温度 1 の烟が起亮温度 0.3 を超えて光る。重心は源（高さ 0.7 m）より上で、域の x・z の中。
    input.fluids[0].reportGlow = true;
    input.fluids[0].settings.emissionIntensity = 1.0f;
    input.fluids[0].settings.emissionTemperature = 0.3f;
    input.fluids[0].settings.emissionHotTemperature = 0.9f;
    const FluidDomainSubmission& domain = input.fluids[0];
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
    std::vector<FluidGlowReport> glow;
    pass.TakeGlowReports(&input, glow);
    assert(!glow.empty());
    const FluidGlowReport& lit = glow.back();
    std::fprintf(stderr,
                 "fluid open plume %u: density %.3f maxDivergence %.4f maxSpeed %.3f bricks %u/%u glow %.3f cd at "
                 "(%.2f %.2f %.2f)\n",
                 resolution, stats.densityTotal, maxDivergence, maxSpeed, stats.activeBricks, stats.totalBricks,
                 lit.intensity.x, lit.worldPosition.x, lit.worldPosition.y, lit.worldPosition.z);
    assert(lit.key == domain.key && lit.intensity.x > 0.01f && lit.intensity.x > lit.intensity.z &&
           std::abs(lit.worldPosition.x) < 0.5f && std::abs(lit.worldPosition.z - 4.0f) < 0.5f &&
           lit.worldPosition.y > 0.7f && lit.worldPosition.y < 4.0f);
    // 子步ごとの V 循環 2 回で残る散度は 64 格で速度 / 格の約 0.5%（0.13 / s）。発散すると 1e6 を超える。
    assert(stats.diagnosticsValid && maxDivergence < 0.25f && maxSpeed < 8.0f);
    assert(stats.densityTotal > minimumDensity && stats.activeBricks > 1u);
    // 圧力は烟があり得る範囲（窓）だけ解く。上の散度は窓の中の値で、窓が域より小さくても投影は保たれている。
    const uint64_t pressureCells = uint64_t{stats.pressureCells[0]} * stats.pressureCells[1] * stats.pressureCells[2];
    const uint64_t allCells = uint64_t{stats.cells[0]} * stats.cells[1] * stats.cells[2];
    std::fprintf(stderr, "fluid pressure window %u: %u x %u x %u of %u x %u x %u\n", resolution, stats.pressureCells[0],
                 stats.pressureCells[1], stats.pressureCells[2], stats.cells[0], stats.cells[1], stats.cells[2]);
    assert(pressureCells > 0u && pressureCells <= allCells && (!expectSmallPressureRegion || pressureCells < allCells));
    // 休眠：源を止めて烟を速く消すと、烟が消えた後に域は眠って子步を進めない。源が戻ると起きて烟が出る。
    const FluidSourceSubmission feeding = input.fluids[0].sources[0];
    input.fluids[0].sources[0].ratePerSecond = 0.0f;
    input.fluids[0].sources[0].settings.velocity = {};
    input.fluids[0].sources[0].settings.radialSpeed = 0.0f;
    input.fluids[0].settings.densityDissipation = 20.0f;
    bool asleep = false;
    for (uint32_t frame = 0u; frame < 600u && !asleep; ++frame) {
        fluid.Frame(pass, input, settings);
        asleep = DomainStats(pass).asleep;
    }
    assert(asleep && DomainStats(pass).stepsThisFrame == 0u);
    input.fluids[0].sources[0] = feeding;
    for (uint32_t frame = 0u; frame < 30u; ++frame)
        fluid.Frame(pass, input, settings);
    const FluidDomainStats& woken = DomainStats(pass);
    std::fprintf(stderr, "fluid sleep %u: woke with density %.3f steps %u\n", resolution, woken.densityTotal,
                 woken.stepsThisFrame);
    assert(!woken.asleep && woken.stepsThisFrame > 0u && woken.densityTotal > 0.0f);
    pass.Shutdown();
}

// 32 格の烟柱に押すもの（と足す源）を置いて 2 秒解き、x の中央の切片（速度と、second の量）を返す。
struct PlumeSlices {
    std::vector<uint16_t> velocity;
    std::vector<uint16_t> second;
};

PlumeSlices PushedPlume(FluidGpu& fluid, uint64_t playbackNamespace, std::span<const FluidInteraction> interactions,
                        FluidDebugField second, std::span<const FluidSourceSubmission> sources = {}) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakePlume(playbackNamespace, 32u));
    input.fluids[0].sources.insert(input.fluids[0].sources.end(), sources.begin(), sources.end());
    const auto key = input.fluids[0].key;
    FluidFrameSettings settings{};
    settings.interactions = interactions;
    pass.RequestDebugSlice({key, FluidDebugField::Velocity, 0u, 0.5f});
    SliceReadback velocity, other;
    for (uint32_t frame = 0u; frame <= 120u; ++frame) {
        if (frame == 120u)
            pass.RequestDebugSlice({key, second, 0u, 0.5f});
        settings.interactionFrame = frame;
        input.fluids[0].deltaSeconds = 1.0f / 60.0f;
        fluid.Frame(pass, input, settings, [&] {
            if (frame == 119u)
                velocity.Record(fluid, pass);
            if (frame == 120u)
                other.Record(fluid, pass);
        });
        input.fluids[0].resetSimulation = false;
    }
    pass.Shutdown();
    return {velocity.Read(), other.Read()};
}

// 切片（x = 16 の yz 面、32 × 32）の中で、域の局所の点 center から radius の内にある格の channel の平均。
float SliceMean(const std::vector<uint16_t>& texels, MATH::Vec3 center, float radius, uint32_t channel) {
    assert(texels.size() == 32u * 32u * 4u);
    float sum = 0.0f;
    uint32_t count = 0u;
    for (uint32_t z = 0u; z < 32u; ++z) {
        for (uint32_t y = 0u; y < 32u; ++y) {
            const MATH::Vec3 p{0.0625f, (static_cast<float>(y) + 0.5f - 16.0f) * 0.125f,
                               (static_cast<float>(z) + 0.5f - 16.0f) * 0.125f};
            if (MATH::Length(p - center) >= radius)
                continue;
            sum += HalfToFloat(texels[(z * 32u + y) * 4u + channel]);
            ++count;
        }
    }
    assert(count > 0u);
    return sum / static_cast<float>(count);
}

// 押すもの（流体烟雾第 6 步）：烟柱の上に止まった球の障碍物を置くと、球の中は上昇が止まって烟が入らない。向きの無い
// 力は投影後の散度（湧き出し）になり、向きのある力は投影の後も流れを残す。源の outflow（第 7 步、漏斗の底の汇）は形の中で
// 一様な散度 outflow × 表面積 / 体積（球は 3 S / R）になる。形状引导（第 7 步）は烟（密度）だけを表面へ寄せ、流れは
// 引導の無い烟柱と同じまま（速度へ入れると投影に打ち消され、残差で流れが乱れる）。
void PushedPlumes(FluidGpu& fluid) {
    const PlumeSlices plain = PushedPlume(fluid, 50u, {}, FluidDebugField::Density);
    // 障碍物を置く高さ：素の烟柱の中心の列で、源（局所 y -1.3）より 0.6 m 以上上の、上昇の最も速い格。
    uint32_t best = 11u;
    for (uint32_t y = 11u; y < 28u; ++y)
        if (HalfToFloat(plain.velocity[(16u * 32u + y) * 4u + 1u]) >
            HalfToFloat(plain.velocity[(16u * 32u + best) * 4u + 1u]))
            best = y;
    const MATH::Vec3 core{0.0f, (static_cast<float>(best) + 0.5f - 16.0f) * 0.125f, 0.0f};
    FluidInteraction obstacle{};
    obstacle.worldPosition = {0.0f, 2.0f + core.y, 4.0f};
    obstacle.extents = {0.4f, 0.4f, 0.4f};
    const PlumeSlices blocked = PushedPlume(fluid, 51u, std::span(&obstacle, 1u), FluidDebugField::Density);
    FluidInteraction expansion{};
    expansion.kind = FluidInteractionKind::Force;
    expansion.worldPosition = {0.0f, 2.2f, 4.0f};
    expansion.extents = {0.6f, 0.6f, 0.6f};
    expansion.strength = 1.0f;
    const PlumeSlices expanded = PushedPlume(fluid, 52u, std::span(&expansion, 1u), FluidDebugField::Divergence);
    FluidInteraction wind = expansion;
    wind.direction = {1.0f, 0.0f, 0.0f};
    wind.strength = 4.0f;
    const PlumeSlices blown = PushedPlume(fluid, 53u, std::span(&wind, 1u), FluidDebugField::Density);
    FluidSourceSubmission sink{};
    sink.worldPosition = expansion.worldPosition;
    sink.shape = FluidSourceShape::Sphere;
    sink.radius = 0.6f;
    sink.settings.outflow = -1.0f;
    const PlumeSlices sunk = PushedPlume(fluid, 54u, {}, FluidDebugField::Divergence, std::span(&sink, 1u));
    FluidSourceSubmission guide = sink;
    guide.settings.outflow = 0.0f;
    guide.settings.guideStrength = 3.0f;
    guide.settings.guideThickness = 0.5f;
    const PlumeSlices guided = PushedPlume(fluid, 55u, {}, FluidDebugField::Density, std::span(&guide, 1u));
    // 注入の揺らぎ（第 7 步）：同じ球の源で注入し、揺らぎ 1 は塊ごとに濃淡を付けるが切片の烟の総量はおよそ変えない。
    FluidSourceSubmission emitter = guide;
    emitter.settings.guideStrength = 0.0f;
    emitter.ratePerSecond = 2.0f;
    const PlumeSlices even = PushedPlume(fluid, 56u, {}, FluidDebugField::Density, std::span(&emitter, 1u));
    emitter.settings.emissionNoise = 1.0f;
    emitter.settings.emissionNoiseScale = 0.3f;
    const PlumeSlices lumpy = PushedPlume(fluid, 57u, {}, FluidDebugField::Density, std::span(&emitter, 1u));
    float evenTotal = 0.0f, lumpyTotal = 0.0f, lumpChange = 0.0f;
    for (size_t index = 0u; index < even.second.size(); index += 4u) {
        evenTotal += HalfToFloat(even.second[index]);
        lumpyTotal += HalfToFloat(lumpy.second[index]);
        lumpChange += std::abs(HalfToFloat(lumpy.second[index]) - HalfToFloat(even.second[index]));
    }
    float velocityChange = 0.0f, densityChange = 0.0f;
    for (size_t index = 0u; index < plain.velocity.size(); ++index)
        velocityChange = (std::max)(velocityChange,
                                    std::abs(HalfToFloat(guided.velocity[index]) - HalfToFloat(plain.velocity[index])));
    for (size_t index = 0u; index < plain.second.size(); index += 4u)
        densityChange += std::abs(HalfToFloat(guided.second[index]) - HalfToFloat(plain.second[index]));
    const float plainRise = SliceMean(plain.velocity, core, 0.25f, 1u);
    const float blockedSpeed = SliceMean(blocked.velocity, core, 0.25f, 3u);
    const float plainDensity = SliceMean(plain.second, core, 0.25f, 0u);
    const float blockedDensity = SliceMean(blocked.second, core, 0.25f, 0u);
    // 力（湧き出しと風）の中心は局所 (0, 0.2, 0)。それに最も近い格（局所 (0.0625, 0.1875, 0.0625)）の湧き出し率は
    // 30 S / R (1 - r / R)²。
    const MATH::Vec3 pushCenter{0.0f, 0.2f, 0.0f};
    const float r = MATH::Length(MATH::Vec3{0.0625f, 0.1875f, 0.0625f} - pushCenter);
    const float expected = 30.0f * 1.0f / 0.6f * (1.0f - r / 0.6f) * (1.0f - r / 0.6f);
    const float divergence = HalfToFloat(expanded.second[(16u * 32u + 17u) * 4u]);
    const float sinkDivergence = HalfToFloat(sunk.second[(16u * 32u + 17u) * 4u]);
    const float plainSide = SliceMean(plain.velocity, pushCenter, 0.25f, 0u);
    const float blownSide = SliceMean(blown.velocity, pushCenter, 0.25f, 0u);
    std::fprintf(stderr,
                 "fluid pushes: core y %.3f rise plain %.3f blocked speed %.3f density plain %.3f blocked %.3f "
                 "expansion %.2f "
                 "(expected %.2f) sink %.2f (expected -5.00) side plain %.3f blown %.3f guide velocity change %.5f "
                 "density change %.2f emission noise total %.1f -> %.1f change %.1f\n",
                 core.y, plainRise, blockedSpeed, plainDensity, blockedDensity, divergence, expected, sinkDivergence,
                 plainSide, blownSide, velocityChange, densityChange, evenTotal, lumpyTotal, lumpChange);
    assert(plainRise > 0.3f && blockedSpeed < 0.15f * plainRise);
    assert(plainDensity > 0.05f && blockedDensity < 0.3f * plainDensity);
    assert(std::abs(divergence - expected) < 0.3f * expected);
    // 疎な解算では烟が動くと解く砖块と圧力の窓も変わり流れがわずかに違う（0.02〜0.06 m/s 程度。引導を加速度で入れた時は
    // 0.5 m/s 超）。
    assert(std::abs(sinkDivergence + 5.0f) < 1.5f && velocityChange < 0.15f && densityChange > 1.0f);
    assert(blownSide > 0.5f && blownSide - plainSide > 0.4f);
    assert(std::abs(lumpyTotal - evenTotal) < 0.35f * evenTotal && lumpChange > 0.1f * evenTotal);
}

// 局所空間の域：挂点が止まった後に急に +x へ 2 m/s で動き出すと、域の中の流れは逆向きに約 2 m/s を受け（慣性）、
// 開いた境界の域では一様な向かい風として残る。後半はフレームの長さが 1/120 s と 1/40 s で交互に揺れ、挂点の位置は
// 1 フレーム前の長さで進む（VFX が LateUpdate の前に読む物体と同じ）。それでも向かい風は 2 m/s のまま育たない。
void LocalSpaceInertia(FluidGpu& fluid) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakeDomain(70u, FluidBoundary::Open, 0.1f));
    FluidDomainSubmission& domain = input.fluids[0];
    domain.localSpace = true;
    domain.settings.inertia = 1.0f;
    domain.settings.buoyancy = 0.0f;
    domain.settings.vorticity = 0.0f;
    domain.sources[0].settings.velocity = {};
    domain.sources[0].field = {};
    pass.RequestDebugSlice({domain.key, FluidDebugField::Velocity, 0u, 0.5f});
    SliceReadback readback;
    const MATH::Vec3 sourceOffset = domain.sources[0].worldPosition;
    float x = 0.0f;
    float previousDt = 1.0f / 60.0f;
    constexpr uint32_t kFrames = 90u;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const float dt = frame < 50u ? 1.0f / 60.0f : (frame % 2u == 0u ? 1.0f / 120.0f : 1.0f / 40.0f);
        if (frame >= 10u)
            x += 2.0f * previousDt;
        previousDt = dt;
        input.fluids[0].worldPosition = {x, 0.0f, 0.0f};
        input.fluids[0].sources[0].worldPosition = sourceOffset + MATH::Vec3{x, 0.0f, 0.0f};
        input.fluids[0].deltaSeconds = dt;
        fluid.Frame(pass, input, {}, [&] {
            if (frame + 1u == kFrames)
                readback.Record(fluid, pass);
        });
        input.fluids[0].resetSimulation = false;
    }
    const std::vector<uint16_t> texels = readback.Read();
    float sum = 0.0f;
    for (size_t index = 0u; index < texels.size(); index += 4u)
        sum += HalfToFloat(texels[index]);
    const float meanX = sum / static_cast<float>(texels.size() / 4u);
    std::fprintf(stderr, "fluid local inertia: mean x velocity %.3f (expected about -2)\n", meanX);
    assert(meanX < -1.6f && meanX > -2.4f);
    pass.Shutdown();
}

// 長い預熱（TD2_2Test の地面雾 60 Hz × 6 秒、烟壳 30 Hz × 10 秒）は子步の数で数えて必ず終わり、終わったフレームから
// 描く。秒から子步の長さを引く数え方だと丸めの残りで預熱が終わらず、域が黙って描かれなかった。16³ は 1 フレーム 48 子步。
void LongPrewarm(FluidGpu& fluid, uint64_t playbackNamespace, float substepRate, float prewarmSeconds,
                 uint32_t expectedSteps) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakeDomain(playbackNamespace, FluidBoundary::Open, 0.1f));
    input.fluids[0].settings.substepRate = substepRate;
    input.fluids[0].settings.prewarmSeconds = prewarmSeconds;
    const uint32_t frames = (expectedSteps + 47u) / 48u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        assert(!pass.HasRenderableDomains());
        input.fluids[0].deltaSeconds = 1.0f / 60.0f;
        fluid.Frame(pass, input, {});
        input.fluids[0].resetSimulation = false;
        assert(DomainStats(pass).stepsThisFrame >= (std::min)(48u, expectedSteps - frame * 48u));
    }
    const FluidDomainStats& stats = DomainStats(pass);
    std::fprintf(stderr, "fluid prewarm %.0f Hz %.1f s: %u frames, steps %llu, remaining %.4f s\n", substepRate,
                 prewarmSeconds, frames, static_cast<unsigned long long>(stats.completedSteps),
                 stats.prewarmRemainingSeconds);
    assert(pass.HasRenderableDomains() && stats.prewarmRemainingSeconds == 0.0f);
    assert(stats.completedSteps >= expectedSteps && stats.completedSteps <= expectedSteps + 1u);
    pass.Shutdown();
}

// 注入（Emit）は球の中へ中心で毎秒 strength の密度を足す（総量は strength × 2πR³/15 × 時間）。汇（向きの無い負の力）が
// 回読で報告する吸い込み量は、汇がある間に域から消えた烟の量（Σ 密度 × 格の体積）とおおよそ一致する。吸収を付けた汇は
// 引き寄せた烟も消すので、同じ時間で消える割合が吸い込みだけの時より大きく、報告もその量と合う。静かな開いた箱
// （浮力・渦なし、消散 0）で、注入 30 フレーム → 汇 30 フレーム → 吸収付きの汇 30 フレーム。最後の回読は次のフレーム
// （子步なし）に届く。
void SinkAndEmit(FluidGpu& fluid) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    input.fluids.push_back(MakeDomain(90u, FluidBoundary::Open, 0.0f));
    FluidDomainSubmission& domain = input.fluids[0];
    domain.sources.clear();
    domain.settings.buoyancy = 0.0f;
    domain.settings.weight = 0.0f;
    domain.settings.vorticity = 0.0f;
    const float cellVolume =
        std::pow(domain.settings.domainSize.x / static_cast<float>(domain.settings.resolution), 3.0f);
    FluidInteraction emit{};
    emit.kind = FluidInteractionKind::Emit;
    emit.extents = {0.6f, 0.0f, 0.0f};
    emit.strength = 2.0f;
    FluidInteraction sink{};
    sink.kind = FluidInteractionKind::Force;
    sink.extents = {0.6f, 0.0f, 0.0f};
    sink.strength = -0.1f;
    sink.reportKey = 42u;
    FluidFrameSettings settings{};
    settings.diagnostics = true;
    std::vector<FluidSinkReport> reports;
    uint32_t frame = 0u;
    const auto run = [&](const FluidInteraction* push, uint32_t frames) {
        settings.interactions =
            push != nullptr ? std::span<const FluidInteraction>(push, 1u) : std::span<const FluidInteraction>{};
        for (uint32_t index = 0u; index <= frames; ++index) {
            // 最後の 1 フレームは時間を進めず、前のフレームの回読だけを受け取る。
            settings.interactionFrame = frame++;
            input.fluids[0].deltaSeconds = index < frames ? 1.0f / 60.0f : 0.0f;
            fluid.Frame(pass, input, settings);
            input.fluids[0].resetSimulation = false;
            pass.TakeSinkReports(reports);
        }
        return DomainStats(pass).densityTotal * cellVolume;
    };
    const float emitted = run(&emit, 30u);
    const float expected = emit.strength * 2.0f * 3.14159265f * std::pow(emit.extents.x, 3.0f) / 15.0f * 0.5f;
    assert(reports.empty());
    const float left = run(&sink, 30u);
    double reported = 0.0;
    for (const FluidSinkReport& report : reports) {
        assert(report.reportKey == sink.reportKey);
        reported += report.removed;
    }
    const float lost = emitted - left;
    reports.clear();
    sink.absorption = 4.0f;
    const float absorbedLeft = run(&sink, 30u);
    double absorbedReported = 0.0;
    for (const FluidSinkReport& report : reports)
        absorbedReported += report.removed;
    const float absorbed = left - absorbedLeft;
    std::fprintf(stderr,
                 "fluid emit %.4f (expected %.4f), sink lost %.4f reported %.4f, absorbing sink lost %.4f of %.4f "
                 "reported %.4f\n",
                 emitted, expected, lost, reported, absorbed, left, absorbedReported);
    assert(emitted > 0.7f * expected && emitted < 1.3f * expected);
    assert(lost > 0.05f * emitted && std::abs(reported - lost) < 0.3 * lost);
    assert(absorbed / left > 1.3f * lost / emitted && std::abs(absorbedReported - absorbed) < 0.3 * absorbed);
    pass.Shutdown();
}

// 通道と燃料の注入：同じ場所に重なる、燃える域（Channel1）と燃えない域（Default）。Channel1 へ出した燃料の注入（点火温度
// より熱い）は燃える域だけに入り、燃えて煤になる。Default へ出した燃料は燃えない域が受け取らない（烟にもならない）。
// Default へ出した烟の注入は燃えない域だけに入る。静かな開いた箱（浮力・渦なし、消散 0）で 30 フレームずつ。
void FuelAndChannels(FluidGpu& fluid) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    constexpr uint64_t kBurning = 92u;
    constexpr uint64_t kPlain = 93u;
    for (const uint64_t playbackNamespace : {kBurning, kPlain}) {
        FluidDomainSubmission& domain =
            input.fluids.emplace_back(MakeDomain(playbackNamespace, FluidBoundary::Open, 0.0f));
        domain.sources.clear();
        domain.settings.buoyancy = 0.0f;
        domain.settings.weight = 0.0f;
        domain.settings.vorticity = 0.0f;
    }
    input.fluids[0].settings.combustion = true;
    input.fluids[0].settings.channel = FluidChannel::Channel1;
    FluidInteraction emit{};
    emit.kind = FluidInteractionKind::Emit;
    emit.extents = {0.6f, 2.0f, 0.0f};
    emit.strength = 2.0f;
    FluidFrameSettings settings{};
    settings.diagnostics = true;
    uint32_t frame = 0u;
    const auto density = [&](uint64_t playbackNamespace) {
        for (const FluidDomainStats& stats : pass.GetStats().perDomain)
            if (stats.key.playbackNamespace == playbackNamespace)
                return stats.densityTotal;
        assert(false);
        return 0.0f;
    };
    const auto run = [&](bool fuel, uint32_t channels) {
        emit.fuel = fuel;
        emit.channels = channels;
        settings.interactions = std::span<const FluidInteraction>(&emit, 1u);
        for (uint32_t index = 0u; index <= 30u; ++index) {
            // 最後の 1 フレームは時間を進めず、前のフレームの回読だけを受け取る。
            settings.interactionFrame = frame++;
            for (FluidDomainSubmission& domain : input.fluids) {
                domain.deltaSeconds = index < 30u ? 1.0f / 60.0f : 0.0f;
            }
            fluid.Frame(pass, input, settings);
            for (FluidDomainSubmission& domain : input.fluids)
                domain.resetSimulation = false;
        }
    };
    run(true, FluidChannelMask(FluidChannel::Channel1));
    const float soot = density(kBurning);
    const float untouched = density(kPlain);
    run(true, kFluidDefaultChannelMask);
    const float ignored = density(kPlain);
    run(false, kFluidDefaultChannelMask);
    const float smoke = density(kPlain);
    std::fprintf(stderr,
                 "fluid channels: fuel on channel 1 -> burning %.4f plain %.4f, fuel on default -> plain %.4f, "
                 "smoke on default -> plain %.4f\n",
                 soot, untouched, ignored, smoke);
    assert(soot > 0.0f && untouched == 0.0f && ignored == 0.0f && smoke > 0.0f);
    pass.Shutdown();
}

// 模型の形の源：距離の体積（半径 0.6 m の球を手で作る。箱は ±1 m、band は 4 格）の内側だけに烟が入る。填充 0.5 では
// 手前（−y）の半分だけ、填充 0 と、体積の無い源は何も入れない。静かな開いた箱（浮力・渦・力場なし、消散 0）で 30 フレームずつ。
void ModelShape(FluidGpu& fluid) {
    constexpr uint32_t kCells = 32u;
    constexpr float kCellSize = 2.0f / static_cast<float>(kCells);
    auto volume = std::make_shared<FluidShapeVolume>();
    volume->cells[0] = volume->cells[1] = volume->cells[2] = kCells;
    volume->boundsMin = {-1.0f, -1.0f, -1.0f};
    volume->boundsMax = {1.0f, 1.0f, 1.0f};
    volume->band = kFluidShapeVolumeBandCells * kCellSize;
    volume->distance.resize(size_t{kCells} * kCells * kCells);
    for (uint32_t z = 0u; z < kCells; ++z)
        for (uint32_t y = 0u; y < kCells; ++y)
            for (uint32_t x = 0u; x < kCells; ++x) {
                const MATH::Vec3 center{-1.0f + (static_cast<float>(x) + 0.5f) * kCellSize,
                                        -1.0f + (static_cast<float>(y) + 0.5f) * kCellSize,
                                        -1.0f + (static_cast<float>(z) + 0.5f) * kCellSize};
                const float normalized = std::clamp((MATH::Length(center) - 0.6f) / volume->band, -1.0f, 1.0f);
                volume->distance[x + kCells * (y + size_t{kCells} * z)] =
                    static_cast<int8_t>(std::lround(normalized * 127.0f));
            }
    const auto run = [&](uint64_t playbackNamespace, float fill, bool withVolume) {
        FluidVolumePass pass;
        RENDER3D::PARTICLES::ParticleRenderInput input{};
        FluidDomainSubmission& domain =
            input.fluids.emplace_back(MakeDomain(playbackNamespace, FluidBoundary::Open, 0.0f));
        domain.settings.domainSize = {3.0f, 3.0f, 3.0f};
        domain.settings.resolution = 32u;
        domain.settings.buoyancy = 0.0f;
        domain.settings.weight = 0.0f;
        domain.settings.vorticity = 0.0f;
        FluidSourceSubmission& source = domain.sources.front();
        source.worldPosition = {};
        source.shape = FluidSourceShape::Model;
        source.settings.temperature = 0.0f;
        source.settings.velocity = {};
        source.field = {};
        source.shapeVolume = withVolume ? volume : nullptr;
        source.shapeFill = fill;
        FluidFrameSettings settings{};
        settings.diagnostics = true;
        for (uint32_t index = 0u; index <= 30u; ++index) {
            domain.deltaSeconds = index < 30u ? 1.0f / 60.0f : 0.0f;
            fluid.Frame(pass, input, settings);
            domain.resetSimulation = false;
        }
        const float total = DomainStats(pass).densityTotal;
        pass.Shutdown();
        return total;
    };
    const float full = run(94u, 1.0f, true);
    const float half = run(95u, 0.5f, true);
    const float none = run(96u, 0.0f, true);
    const float missing = run(97u, 1.0f, false);
    // 球の体積 0.905 m³ × 毎秒 4 × 0.5 秒 ÷ 格の体積（0.094 m）³ ≒ 2180（縁の柔らかさの分だけ増える）。
    std::fprintf(stderr, "fluid model shape: full %.1f half %.1f (ratio %.3f) none %.3f missing %.3f\n", full, half,
                 half / (std::max)(full, 1.0e-6f), none, missing);
    assert(full > 1500.0f && full < 4500.0f);
    assert(half > 0.35f * full && half < 0.65f * full);
    assert(none == 0.0f && missing == 0.0f);
}

// 域の数の上限（画質の maxDomains）：上限 + 2 個の域のうち、遠くても優先度の高い域は必ず解き、同じ優先度では相機から
// 遠い 2 つが外れて統計に印が付く。
void DomainLimit(FluidGpu& fluid) {
    FluidVolumePass pass;
    RENDER3D::PARTICLES::ParticleRenderInput input{};
    const uint32_t limit = ResolveFluidQualityPolicy(FluidFrameSettings{}.quality).maxDomains;
    for (uint32_t index = 0u; index < limit + 2u; ++index) {
        FluidDomainSubmission domain = MakeDomain(100u + index, FluidBoundary::Open, 0.0f);
        domain.worldPosition = {0.0f, 0.0f, 10.0f * static_cast<float>(index + 1u)};
        domain.deltaSeconds = 1.0f / 60.0f;
        input.fluids.push_back(domain);
    }
    input.fluids[limit + 1u].settings.priority = 10u;
    FluidFrameSettings settings{};
    settings.viewerPosition = {};
    fluid.Frame(pass, input, settings);
    const auto skipped = [&](uint64_t playbackNamespace) {
        for (const FluidDomainStats& stats : pass.GetStats().perDomain)
            if (stats.key.playbackNamespace == playbackNamespace)
                return stats.skippedByLimit;
        assert(false);
        return true;
    };
    std::fprintf(stderr, "fluid domain limit %u: skipped %u, far priority domain skipped %d\n", limit,
                 pass.GetStats().skippedDomains, skipped(100u + limit + 1u) ? 1 : 0);
    assert(pass.GetStats().skippedDomains == 2u && !skipped(100u + limit + 1u));
    for (uint32_t index = 0u; index <= limit; ++index)
        assert(skipped(100u + index) == (index + 1u >= limit));
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
    // 圧力を解く窓：砖块と窓の段の倍率の倍数で、最も粗い段は各辺 8 以下、要る砖块を余白ごと含み、域を出ない。域の面に
    // 重なる面だけが域の閉じた印を引き継ぐ。域とほぼ同じ箱は域全体（域の段の数）になる。
    {
        const FluidGridLayout layout = ResolveFluidGridLayout({16.0f, 3.0f, 16.0f}, 176u);
        struct BrickBox {
            uint32_t low[3];
            uint32_t high[3];
            bool whole;
        };
        const uint32_t last[3] = {layout.cells[0] / 8u - 1u, layout.cells[1] / 8u - 1u, layout.cells[2] / 8u - 1u};
        for (const BrickBox& box :
             {BrickBox{{0u, 0u, 0u}, {0u, 0u, 0u}, false}, BrickBox{{10u, 2u, 11u}, {12u, 3u, 13u}, false},
              BrickBox{{0u, 0u, 0u}, {last[0], last[1], last[2]}, true}}) {
            const uint32_t margin = 2u;
            const FluidPressureWindow window =
                ResolveFluidPressureWindow(layout, box.low, box.high, margin, kFluidAllFaces);
            const uint32_t unit = (std::max)(8u, 1u << (window.levels - 1u));
            assert(!window.empty && window.levels <= layout.levels && (window.levels == layout.levels) == box.whole);
            for (uint32_t axis = 0u; axis < 3u; ++axis) {
                const uint32_t end = window.origin[axis] + window.size[axis];
                assert(window.origin[axis] % unit == 0u && window.size[axis] % unit == 0u &&
                       (window.size[axis] >> (window.levels - 1u)) <= 8u && end <= layout.cells[axis]);
                assert(window.origin[axis] <= (box.low[axis] - (std::min)(box.low[axis], margin)) * 8u &&
                       end >= (std::min)((box.high[axis] + 1u + margin) * 8u, layout.cells[axis]));
                assert(((window.closedFaces >> (axis * 2u)) & 1u) == (window.origin[axis] == 0u ? 1u : 0u) &&
                       ((window.closedFaces >> (axis * 2u + 1u)) & 1u) == (end == layout.cells[axis] ? 1u : 0u));
            }
        }
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
    OpenPlume(fluid, 42u, 128u, 20000.0f, {1.0f, 4.0f, 1.0f});
    // 烟柱が中央の一部だけを占める広い域（32 格の域と同じ格の大きさ、160 × 32 × 160 格で 6 段）：圧力の窓は域より小さい。
    OpenPlume(fluid, 43u, 160u, 500.0f, {20.0f, 4.0f, 20.0f}, true);
    PushedPlumes(fluid);
    LocalSpaceInertia(fluid);
    LongPrewarm(fluid, 80u, 60.0f, 6.0f, 360u);
    LongPrewarm(fluid, 81u, 30.0f, 10.0f, 300u);
    SinkAndEmit(fluid);
    FuelAndChannels(fluid);
    ModelShape(fluid);
    DomainLimit(fluid);
    closedBox(0, 1.0e-4f);
    closedBox(1, 0.06f);
    closedBox(2, 0.12f);
    const float keptOpen = FloorBoundary(fluid, 60u, FluidBoundary::Open);
    const float keptFloor = FloorBoundary(fluid, 61u, FluidBoundary::ClosedFloor);
    const float keptOpenTop = FloorBoundary(fluid, 62u, FluidBoundary::OpenTop);
    (void)FloorBoundary(fluid, 63u, FluidBoundary::Closed);
    assert(keptFloor > keptOpen + 0.2f && keptOpenTop > keptOpen + 0.2f);
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
