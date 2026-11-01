#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <d3dx12.h>
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_TimeService.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Render/HIKARI_SceneCaptureStylePass.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Internal/HIKARI_GpuContractDevice.h"

namespace {
void CheckCaptureDepthIsolation() {
    using namespace HIKARI;
    using namespace RENDER3D;
    using Microsoft::WRL::ComPtr;
    TESTS::INTERNAL::GpuContractDevice gpu;
    GFX::GpuDeferredReleaseQueue retirement;
    const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GFX::DESCRIPTOR::kSrvHeapCapacity,
                                              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
    GFX::Context graphics{};
    graphics.device = gpu.device.Get();
    graphics.deviceGeneration = 11;
    graphics.cmdList = gpu.commands.Get();
    graphics.queue = gpu.queue.Get();
    graphics.srvHeap = heap.Get();
    graphics.deferredReleaseQueue = &retirement;
    graphics.currentFrameRetireFenceValue = 1;
    graphics.frameFence = gpu.fence.Get();
    UpdateRenderResourceDescriptorPoolContext(graphics);
    POST::QuadDrawer quad;
    assert(quad.Init(graphics) && quad.SetOutputFormat(DXGI_FORMAT_R32G32B32A32_FLOAT));
    const CD3DX12_HEAP_PROPERTIES readHeap(D3D12_HEAP_TYPE_READBACK);
    const auto readDesc = CD3DX12_RESOURCE_DESC::Buffer(3 * 512);
    ComPtr<ID3D12Resource> readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(
        &readHeap, D3D12_HEAP_FLAG_NONE, &readDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))));
    gpu.ResetCommands();
    RenderTarget2D main, secondary, swapped;
    std::array<RenderTarget2D, 3> outputs;
    const auto begin = [&](RenderTarget2D& target, float depth) {
        target.UpdateContext(graphics);
        assert(target.Init(4, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, true, {0, 0, 0, 1}, true));
        target.BeginCapture(0, 0, 0, 1, depth);
        assert(target.BeginDepthShaderRead());
    };
    const auto sample = [&](RenderTarget2D& target, UINT index) {
        auto& output = outputs[index];
        output.UpdateContext(graphics);
        assert(output.Init(4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT));
        output.BeginCapture();
        quad.DrawFullscreen(heap.Get(), target.GetDepthSrvGpu());
        output.EndCapture();
        output.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        footprint.Offset = index * 512;
        footprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 1, 1, 256};
        const CD3DX12_TEXTURE_COPY_LOCATION from(output.GetResource(), 0), to(readback.Get(), footprint);
        gpu.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    };
    // 同じフレームで main を記録した後、副 view の公開と main の再作成を行う。
    // 実行はすべての CPU 操作後。以前の共有 SRV なら既存 draw の参照先が変わる。
    begin(main, .25f);
    const auto original = main.GetDepthSrvGpu();
    sample(main, 0);
    begin(secondary, .75f);
    assert(main.GetDepthSrvGpu().ptr != secondary.GetDepthSrvGpu().ptr);
    sample(secondary, 1);
    main.Finalize();
    begin(main, .5f);
    assert(main.GetDepthSrvGpu().ptr != original.ptr);
    main.Swap(swapped);
    sample(swapped, 2);
    main.Finalize();
    secondary.Finalize();
    swapped.Finalize();
    for (auto& output : outputs)
        output.Finalize();
    quad.Finalize();
    assert(GetRenderResourceDescriptorPoolStats().used == 3);
    retirement.Collect(0);
    assert(GetRenderResourceDescriptorPoolStats().used == 3);
    gpu.SubmitAndWait(1);
    void* mapped = nullptr;
    assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
    constexpr float expected[]{.25f, .75f, .5f};
    for (UINT i = 0; i != 3; ++i) {
        const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped) + i * 512);
        for (UINT x = 0; x != 4; ++x)
            assert(std::abs(row[x * 4] - expected[i]) < 1e-6f);
    }
    readback->Unmap(0, nullptr);
    retirement.Collect(1);
    assert(GetRenderResourceDescriptorPoolStats().used == 0);
    ShutdownRenderResourceDescriptorPool();
    std::puts("Capture depth GPU: main/secondary isolation, recreation, swap and delayed fence retirement PASS");
}
} // namespace

void CheckSceneCaptureStyles() {
    using namespace HIKARI;
    using namespace RENDER3D;
    using Microsoft::WRL::ComPtr;
    assert(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    TESTS::INTERNAL::GpuContractDevice gpu;
    GFX::Context graphics{};
    graphics.device = gpu.device.Get();
    graphics.deviceGeneration = 10;
    graphics.cmdList = gpu.commands.Get();
    graphics.queue = gpu.queue.Get();
    TIME::Reset();
    (void)TIME::BeginFrame();
    gpu.ResetCommands();
    constexpr UINT width = 64, height = 32, pixels = width * height;
    using Pixel = std::array<float, 4>;
    // 暗い背景（輝度 0.01）に象牙の箱（x 16..31, y 8..23）と赤い箱（x 40..47, y 8..15）。4 texel = 1 ゲーム画素。
    const Pixel background{.01f, .01f, .01f, 1}, ivory{.7f, .66f, .54f, 1}, red{.55f, .06f, .04f, 1};
    std::vector<Pixel> source(pixels, background);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            if (x >= 16 && x < 32 && y >= 8 && y < 24)
                source[y * width + x] = ivory;
            if (x >= 40 && x < 48 && y >= 8 && y < 16)
                source[y * width + x] = red;
        }
    const auto buffer = [&](UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> resource;
        const CD3DX12_HEAP_PROPERTIES heap(type);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                             IID_PPV_ARGS(&resource))));
        return resource;
    };
    auto upload = buffer(sizeof(Pixel) * pixels * 2, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped = nullptr;
    assert(SUCCEEDED(upload->Map(0, nullptr, &mapped)));
    std::memcpy(mapped, source.data(), sizeof(Pixel) * pixels);
    auto* scaled = static_cast<Pixel*>(mapped) + pixels;
    for (UINT i = 0; i < pixels; ++i) {
        scaled[i] = source[i];
        for (UINT channel = 0; channel != 3; ++channel)
            scaled[i][channel] *= .25f;
    }
    upload->Unmap(0, nullptr);
    std::array<RenderTarget2D, 2> inputs;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, 1, width * sizeof(Pixel)};
    for (UINT i = 0; i < inputs.size(); ++i) {
        auto& input = inputs[i];
        input.UpdateContext(graphics);
        assert(input.Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT));
        input.TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);
        footprint.Offset = i * sizeof(Pixel) * pixels;
        const CD3DX12_TEXTURE_COPY_LOCATION from(upload.Get(), footprint), to(input.GetResource(), 0);
        gpu.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        input.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    GameProjectSceneCaptureStyleDesc base;
    base.pixelSize = 4;
    base.lineWidth = 1;
    base.shadowStrength = .5f;
    base.shadowOffsetX = 2;
    base.shadowOffsetY = 2;
    // 0 Clean、1 Clean を前露出 0.25 の入力で、2 Pixel、3 Pixel 輪郭なし、4 Sketch、5 Clean 明るさ 2。
    std::vector<GameProjectSceneCaptureStyleDesc> cases(6, base);
    cases[0].style = cases[1].style = cases[5].style = GameProjectSceneCaptureStyle::Clean;
    cases[2].style = cases[3].style = GameProjectSceneCaptureStyle::Pixel;
    cases[3].lineWidth = 0;
    cases[4].style = GameProjectSceneCaptureStyle::Sketch;
    cases[5].brightness = 2;
    const UINT count = static_cast<UINT>(cases.size());
    std::vector<std::unique_ptr<RenderTarget2D>> outputs;
    auto readback =
        buffer(UINT64(count) * sizeof(Pixel) * pixels, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    for (UINT i = 0; i < count; ++i) {
        auto output = std::make_unique<RenderTarget2D>();
        output->UpdateContext(graphics);
        assert(output->Init(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT));
        COLOR::ExposureFrameState exposure{};
        exposure.valid = true;
        exposure.exposure = 1;
        exposure.preExposure = i == 1 ? .25f : 1.f;
        const COLOR::ScopedViewExposure scope(exposure);
        assert(DrawSceneCaptureStyle(graphics, inputs[i == 1 ? 1 : 0], *output, cases[i]));
        output->TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        footprint.Offset = UINT64(i) * sizeof(Pixel) * pixels;
        const CD3DX12_TEXTURE_COPY_LOCATION from(output->GetResource(), 0), to(readback.Get(), footprint);
        gpu.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        outputs.push_back(std::move(output));
    }
    gpu.SubmitAndWait(1);
    assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
    const auto* results = static_cast<const Pixel*>(mapped);
    const auto at = [&](UINT image, UINT x, UINT y) { return results[image * pixels + y * width + x]; };
    const auto matches = [](const Pixel& a, const Pixel& b, float tolerance) {
        return std::abs(a[0] - b[0]) < tolerance && std::abs(a[1] - b[1]) < tolerance &&
               std::abs(a[2] - b[2]) < tolerance;
    };
    for (UINT i = 0; i < count * pixels; ++i)
        for (float value : results[i])
            assert(std::isfinite(value));
    // Clean：そのまま、前露出は戻す、明るさは掛けるだけ。
    for (UINT p = 0; p < pixels; ++p) {
        assert(matches(results[p], source[p], 1e-5f));
        assert(matches(results[pixels + p], source[p], 1e-4f));
        const Pixel doubled{source[p][0] * 2, source[p][1] * 2, source[p][2] * 2, 1};
        assert(matches(results[5 * pixels + p], doubled, 1e-4f));
    }
    // Pixel：ゲーム画素の中は一色。物体はそのまま、隣の背景は輪郭、遠い背景は変わらない。
    // 落ち影は物体から右下へ (2, 2) ゲーム画素の帯で、輪郭の色へ shadowStrength だけ寄る。外周 1 ゲーム画素は市松。
    const auto linear = [](float value) { return std::pow((value + .055f) / 1.055f, 2.4f); };
    const Pixel line{linear(base.lineColor.x), linear(base.lineColor.y), linear(base.lineColor.z), 1};
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
            assert(matches(at(2, x, y), at(2, x & ~3u, y & ~3u), 1e-6f));
    assert(matches(at(2, 20, 12), ivory, 1e-5f) && matches(at(2, 41, 9), red, 1e-5f));
    assert(matches(at(2, 33, 12), line, 1e-5f) && matches(at(2, 13, 12), line, 1e-5f));
    assert(matches(at(3, 13, 12), background, 1e-5f));   // 輪郭なし：光の側（左）の隣は背景のまま
    const Pixel shaded{background[0] + (line[0] - background[0]) * .5f,
                       background[1] + (line[1] - background[1]) * .5f,
                       background[2] + (line[2] - background[2]) * .5f, 1};
    // 鮮やかな赤の箱（ゲーム画素 10..11, 2..3）の輪郭の外 2 ゲーム画素には赤の淡い光。距離 2 は全面、3 は市松。
    const auto glow = [&](const Pixel& base, float amount) {
        return Pixel{base[0] + (red[0] * .75f - base[0]) * amount, base[1] + (red[1] * .75f - base[1]) * amount,
                     base[2] + (red[2] * .75f - base[2]) * amount, 1};
    };
    assert(matches(at(2, 37, 26), shaded, 1e-5f));                // ゲーム画素 (9, 6)：本影
    assert(matches(at(2, 41, 25), glow(shaded, .22f), 1e-5f));    // (10, 6)：外周の市松の影に赤の光（距離 3）
    assert(matches(at(2, 53, 13), glow(background, .35f), 1e-5f)); // (13, 3)：赤から距離 2
    assert(matches(at(2, 57, 9), glow(background, .22f), 1e-5f));  // (14, 2)：距離 3、市松の光る側
    assert(matches(at(2, 41, 29), background, 1e-5f)); // (10, 7)：外周で市松の明るい側
    assert(matches(at(3, 33, 12), shaded, 1e-5f));     // 輪郭なしでは影の帯が物体の右に接する
    assert(matches(at(2, 2, 2), background, 1e-5f));
    // Sketch：遠い背景は紙のむら（sRGB で ±0.012）の範囲、物体の内側は紙に色を少し乗せた明るさ、赤は色鉛筆へ寄る。
    const Pixel paper{linear(base.paperColor.x), linear(base.paperColor.y), linear(base.paperColor.z), 1};
    assert(matches(at(4, 4, 28), paper, .005f));
    const auto inside = at(4, 22, 14);
    assert(inside[0] > paper[0] && inside[0] < ivory[0]);
    assert(at(4, 43, 11)[0] > at(4, 43, 11)[1] * 1.6f);
    std::ofstream evidence(std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "capture-style-gpu.rgba32f",
                           std::ios::binary);
    evidence.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(count * sizeof(Pixel) * pixels));
    readback->Unmap(0, nullptr);
    ResetSceneCaptureStylePass();
    std::printf("Capture styles GPU: %u production draws, clean/exposure/pixel/outline/shadow/sketch PASS\n", count);
    CheckCaptureDepthIsolation();
}
