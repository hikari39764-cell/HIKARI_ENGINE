#include "Core/HIKARI_MathConfig.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <vector>
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Color/HIKARI_AutomaticExposure.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_DynamicGiPlacement.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingChanges.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Vfx/Post/HIKARI_PostProcessingTypes.h"
#include <d3dcompiler.h>
#include <d3dx12.h>
#include "Internal/HIKARI_GpuContractDevice.h"
#include "Internal/HIKARI_ContractAssert.h"

namespace {
void VerifyRasterFaceConvention() {
    using namespace HIKARI::MATH;
    using Microsoft::WRL::ComPtr;
    HIKARI::TESTS::INTERNAL::GpuContractDevice gpu;
    const char* source = R"(
cbuffer Input : register(b0) { float4 clips[3]; uint reverseOrder; };
float4 VS(uint id : SV_VertexID) : SV_Position { return clips[reverseOrder != 0 && id != 0 ? 3-id : id]; }
float4 PS(bool front : SV_IsFrontFace) : SV_Target { return float4(front ? 1 : 0,1,0,1); }
)";
    ComPtr<ID3DBlob> vs, ps, errors, signature;
    assert(SUCCEEDED(
        D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &errors)));
    assert(SUCCEEDED(
        D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &errors)));
    CD3DX12_ROOT_PARAMETER parameter;
    parameter.InitAsConstants(16, 0);
    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(1, &parameter);
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    constexpr UINT size = 16;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto readHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, size, size, 1, 1, 1, 0,
                                                          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size * 256);
    ComPtr<ID3D12Resource> target, readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                         D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                         IID_PPV_ARGS(&target))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));
    D3D12_DESCRIPTOR_HEAP_DESC descriptorDesc{};
    descriptorDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    descriptorDesc.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&descriptorDesc, IID_PPV_ARGS(&descriptors))));
    const auto rtv = descriptors->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(target.Get(), nullptr, rtv);
    struct Constants {
        Vec4 clips[3];
        uint32_t reverse = 0;
        uint32_t padding[3]{};
    } constants;
    assert(kFrontFaceCounterClockwise);
    const auto viewProjection =
        Mat4::PerspectiveFovRH_ZO(1.0f, 1.0f, .1f, 100.0f) * Mat4::LookAtRH({}, {0, 0, -1}, {0, 1, 0});
    constants.clips[0] = viewProjection.TransformPoint({-1, -1, -3, 1});
    constants.clips[1] = viewProjection.TransformPoint({1, -1, -3, 1});
    constants.clips[2] = viewProjection.TransformPoint({0, 1, -3, 1});
    UINT64 fence = 0;
    for (bool counterClockwise : {false, true})
        for (bool reversed : {false, true})
            for (bool cull : {false, true}) {
                constants.reverse = reversed;
                D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
                desc.pRootSignature = root.Get();
                desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
                desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
                desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                desc.SampleMask = UINT_MAX;
                desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                desc.RasterizerState.FrontCounterClockwise = counterClockwise;
                desc.RasterizerState.CullMode = cull ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
                desc.DepthStencilState.DepthEnable = FALSE;
                desc.DepthStencilState.StencilEnable = FALSE;
                desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                desc.NumRenderTargets = 1;
                desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
                desc.SampleDesc.Count = 1;
                ComPtr<ID3D12PipelineState> pipeline;
                assert(SUCCEEDED(gpu.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))));
                gpu.ResetCommands();
                const float clear[4]{};
                gpu.commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
                gpu.commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                const D3D12_VIEWPORT viewport{0, 0, float(size), float(size), 0, 1};
                const D3D12_RECT scissor{0, 0, size, size};
                gpu.commands->RSSetViewports(1, &viewport);
                gpu.commands->RSSetScissorRects(1, &scissor);
                gpu.commands->SetPipelineState(pipeline.Get());
                gpu.commands->SetGraphicsRootSignature(root.Get());
                gpu.commands->SetGraphicsRoot32BitConstants(0, 16, &constants, 0);
                gpu.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                gpu.commands->DrawInstanced(3, 1, 0, 0);
                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                gpu.commands->ResourceBarrier(1, &barrier);
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                footprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, size, size, 1, 256};
                CD3DX12_TEXTURE_COPY_LOCATION destination(readback.Get(), footprint), origin(target.Get(), 0);
                gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &origin, nullptr);
                barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                               D3D12_RESOURCE_STATE_RENDER_TARGET);
                gpu.commands->ResourceBarrier(1, &barrier);
                gpu.SubmitAndWait(++fence);
                Vec4* pixels = nullptr;
                const D3D12_RANGE range{0, size * 256};
                assert(SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&pixels))));
                const auto pixel = pixels[(size / 2) * size + size / 2];
                const D3D12_RANGE written{};
                readback->Unmap(0, &written);
                const bool front = counterClockwise != reversed;
                std::printf("RH raster: frontCCW=%d reversed=%d cull=%d sample=(%.0f,%.0f)\n", int(counterClockwise),
                            int(reversed), int(cull), pixel.x, pixel.y);
                assert(pixel.y == ((!cull || front) ? 1.0f : 0.0f));
                if (pixel.y > 0)
                    assert(pixel.x == (front ? 1.0f : 0.0f));
            }
}

void VerifyViewOrientation() {
    using namespace HIKARI::MATH;
    const Mat4 identityView = Mat4::LookAtRH({}, {0, 0, -1}, {0, 1, 0});
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            assert(std::abs(identityView.m[column][row] - (column == row ? 1.0f : 0.0f)) < 1e-6f);
    const auto projection = Mat4::PerspectiveFovRH_ZO(1.0f, 1.0f, 0.1f, 100.0f);
    // 読める文字の右側は screen の右へ写る。中心の depth だけでは鏡像を検出できない。
    const auto left = (projection * identityView).TransformPoint({-1, 0, -3, 1});
    const auto right = (projection * identityView).TransformPoint({1, 0, -3, 1});
    assert(left.x / left.w < right.x / right.w);
    for (Vec3 direction : {Vec3{0, 0, -1}, Vec3{0, 0, 1}, Vec3{1, 2, 3}, Vec3{0, 1, 0}, Vec3{}}) {
        const auto basis = BuildViewBasisRH(direction, {0, 1, 0});
        assert(std::abs(Dot(Cross(basis.right, basis.up), basis.forward * -1.0f) - 1.0f) < 1e-5f);
        const Vec3 eye{2, 3, 4};
        const auto view = Mat4::LookAtRH(eye, eye + direction, {0, 1, 0});
        const auto origin = view.TransformPoint({eye.x, eye.y, eye.z, 1});
        assert(std::abs(origin.x) + std::abs(origin.y) + std::abs(origin.z) < 1e-5f);
    }
}

} // namespace

int main() {
    {
        using namespace HIKARI::RENDER3D;
        RAYTRACING::RuntimeRayTracingChanges changes;
        std::array<GPUDRIVEN::GpuSceneSurfaceRecord, 1> records{};
        auto& object = records[0];
        object.objectId.value = 7;
        object.valid = object.visible = true;
        object.worldBounds = {{-1, -1, -1}, {1, 1, 1}};
        changes.Update(records);
        assert(changes.minimum.w == 1);
        changes.Update(records);
        assert(changes.minimum.w == 0);
        object.clusterWorldMatrix.m[3][0] = 5;
        object.worldBounds = {{4, -1, -1}, {6, 1, 1}};
        changes.Update(records);
        assert(changes.minimum.x == -1 && changes.maximum.x == 6);
        changes.Update(records);
        assert(changes.minimum.w == 0);
        changes.Update({});
        assert(changes.minimum.x == 4 && changes.maximum.x == 6 && changes.minimum.w == 1);
        changes.Update({});
        assert(changes.minimum.w == 0);
    }
    {
        using namespace HIKARI::RENDER3D::DYNAMICGI::DETAIL;
        // 壁頂の上から室内を見る。真下の 3m の壁が、見えている 0m の床を近景から押し出してはならない。
        const std::array<float, kFocusValueCount> wallTop{0.0f, 1.0f, 0.2f, 2.5f, 0.0f, 3.0f,
                                                          5.0f, 4.0f, 6.0f, 3.0f, 5.0f, 0.0f};
        for (uint32_t count : {6u, 8u, 12u}) {
            const auto placed = PlaceNearField(wallTop, 3.368f, count, 1.5f);
            assert(placed.originY <= -1.5f);
            assert(placed.originY + float(count - 1) * 1.5f >= 4.5f);
            // 以前の coverage が悪い時は、格子移動のヒステリシスより修正を優先する。
            const auto recovered = PlaceNearField(wallTop, 3.368f, count, 1.5f, NearFieldPlacement{0.75f, 0.5f});
            assert(recovered.originY <= -1.5f);
            assert(PlaceNearField(wallTop, 3.368f, count, 1.5f, placed).originY == placed.originY);
        }
        auto ground = wallTop;
        ground[5] = 0.0f;
        const auto low = PlaceNearField(ground, 1.4f, 6u, 1.5f);
        assert(low.originY <= -1.5f && low.originY + 7.5f >= 4.0f);
        auto valley = wallTop;
        for (size_t i = 0; i < 5; ++i)
            valley[i] -= 20.0f;
        valley[5] = 0.0f;
        const auto down = PlaceNearField(valley, 1.4f, 12u, 1.5f);
        assert(down.originY <= -21.5f && down.originY + 16.5f >= -16.0f);
        std::array<float, kFocusValueCount> misses;
        misses.fill(kFocusMiss);
        const auto sky = PlaceNearField(misses, 3.0f, 8u, 1.5f);
        assert(std::isfinite(sky.originY) && sky.reach == 0.0f);
    }
    {
        HIKARI::POST::CommonParams parameters;
        parameters.time = 7.0f;
        HIKARI::ToneMappingSettings settings;
        settings.curve = HIKARI::ToneCurve::None;
        settings.gamma = 0.0f;
        settings.contrast = 1.3f;
        settings.saturation = 0.4f;
        HIKARI::POST::WriteToneMappingParameters(parameters, settings, 2.0f, 0.2f, 0.8f);
        assert(parameters.user[0].x == 0 && parameters.user[0].y == 2 && parameters.user[0].z == 0.01f);
        assert(parameters.user[1].w == 1.3f && parameters.user[2].x == 0.4f);
        assert(parameters.user[2].y == 0.2f && parameters.user[2].z == 0.8f);
        HIKARI::POST::WriteToneMappingParameters(parameters, settings, 1.0f);
        assert(parameters.user[2].y == 0 && parameters.user[2].z == 0); // 別ビューへ warp を持ち越さない。
        HIKARI::POST::WriteFxaaParameters(parameters, 0, -1, {0.2f, 0.05f, 0.5f});
        assert(parameters.resolutionX == 1 && parameters.resolutionY == 1 && parameters.user[0].z == 1);
        assert(parameters.user[1].y == 0.2f && parameters.user[1].z == 0.05f && parameters.user[1].w == 0.5f);
        assert(parameters.time == 7.0f);
    }
    {
        namespace COLOR = HIKARI::RENDER3D::COLOR;
        // P はこのフレームの E。残差は 1 で、履歴は前フレームの P との比で換算する。
        const COLOR::ExposureFrameState manual = COLOR::BuildManualExposureFrame(0.25f, 0.5f);
        assert(manual.valid && manual.exposure == 0.25f && manual.preExposure == 0.25f);
        assert(COLOR::ResidualExposure(manual) == 1.0f && COLOR::PreExposureRatio(manual) == 0.5f);
        const COLOR::ExposureFrameState first = COLOR::BuildManualExposureFrame(2.0f, 0.0f, 1.5f);
        assert(first.exposure == 3.0f && first.previousPreExposure == 3.0f && COLOR::PreExposureRatio(first) == 1.0f);
        const HIKARI::MATH::Vec4 view = COLOR::MakeViewExposureConstants(manual);
        assert(view.x == 0.25f && view.y == 4.0f && view.z == 1.0f && view.w == 0.5f);
        // 視点ごとの補正は残差だけに入り、表示参照の色は P/E でその分だけ控えめに書く。
        COLOR::ExposureFrameState adjusted = manual;
        adjusted.exposure *= 2.0f;
        assert(COLOR::ResidualExposure(adjusted) == 2.0f && COLOR::MakeViewExposureConstants(adjusted).z == 0.5f);
        // ビューの区間外は絶対値（探針の捕獲や焼き付け）。
        assert(COLOR::GetActiveViewExposure().x == 1.0f);
        {
            const COLOR::ScopedViewExposure scope(manual);
            assert(COLOR::GetActiveViewExposure().x == 0.25f && COLOR::GetActiveViewExposure().w == 0.5f);
        }
        assert(COLOR::GetActiveViewExposure().x == 1.0f);
        // 自動露出は対数領域で目標へ近づき、測光が無効な間は前の値を保つ（入射照度が分かれば、その値へすぐ合わせる）。
        // 露出を固定・解除しても、描画済み HDR の前フレーム P と順応の開始値を失わない。
        COLOR::AutomaticExposureMeter meter;
        COLOR::ExposureSettings manualSettings{};
        manualSettings.mode = COLOR::ExposureMode::Manual;
        manualSettings.manualEv100 = 12.0f;
        const auto beforeLock = meter.BeginFrame(manualSettings, {}, 1.0f / 60.0f);
        manualSettings.manualEv100 = 15.0f;
        const auto afterLock = meter.BeginFrame(manualSettings, {}, 1.0f / 60.0f);
        assert(std::abs(COLOR::PreExposureRatio(afterLock) - 0.125f) < 1e-5f);
        manualSettings.mode = COLOR::ExposureMode::Automatic;
        const auto unlock = meter.BeginFrame(manualSettings, COLOR::ExposureIllumination{5.0f, 0.0f}, 1.0f / 60.0f);
        assert(unlock.mode == COLOR::ExposureMode::Automatic && !unlock.meteringValid);
        assert(unlock.exposure == afterLock.exposure && COLOR::PreExposureRatio(unlock) == 1.0f);
        manualSettings.mode = COLOR::ExposureMode::Manual;
        manualSettings.manualEv100 = 12.0f;
        const auto relock = meter.BeginFrame(manualSettings, {}, 1.0f / 60.0f);
        assert(relock.exposure == beforeLock.exposure && std::abs(COLOR::PreExposureRatio(relock) - 8.0f) < 1e-5f);

        COLOR::ExposureSettings automatic{};
        automatic.mode = COLOR::ExposureMode::Automatic;
        automatic.speedUp = automatic.speedDown = 20.0f;
        // 暗所の順応は閾値より暗い場面だけを暗く残す。星明かり（EV100 約 -7）は閾値との差の 35% を残す。
        const COLOR::ExposureIllumination unknown{};
        const COLOR::ExposureFrameState night =
            COLOR::BuildAutomaticExposureFrame(automatic, unknown, 0.001f, 0.001f, 1.0f, 1.0f, 0.25f, true);
        const float nightEv100 = std::log2(0.008f) + 0.35f * (5.0f - std::log2(0.008f));
        assert(std::abs(night.targetExposure / COLOR::ExposureFromEv100(nightEv100) - 1.0f) < 1e-3f);
        automatic.nightAdaptationStrength = 0.0f;
        // 明部が中間灰の 6 段上なら、3.5 段を超えた 2.5 段の半分だけ露出を下げる。
        const COLOR::ExposureFrameState protectedHighlights =
            COLOR::BuildAutomaticExposureFrame(automatic, unknown, 0.045f, 0.045f * 64.0f, 1.0f, 1.0f, 0.25f, true);
        assert(std::abs(std::log2(protectedHighlights.targetExposure) - (2.0f - 1.25f)) < 1e-3f);
        const COLOR::ExposureFrameState adapted =
            COLOR::BuildAutomaticExposureFrame(automatic, unknown, 0.045f, 0.045f, 1.0f, 1.0f, 0.25f, true);
        assert(std::abs(adapted.targetExposure - 4.0f) < 1e-4f && std::abs(adapted.exposure - 4.0f) < 0.05f);
        assert(adapted.preExposure == adapted.exposure && COLOR::PreExposureRatio(adapted) == adapted.exposure);
        const COLOR::ExposureFrameState held =
            COLOR::BuildAutomaticExposureFrame(automatic, unknown, 0.045f, 0.045f, 1.0f, 1.0f, 0.25f, false);
        assert(held.exposure == 1.0f && !held.meteringValid);
        const COLOR::ExposureFrameState incident = COLOR::BuildAutomaticExposureFrame(
            automatic, COLOR::ExposureIllumination{15.5f, 1.0f}, 0.045f, 0.045f, 1.0f, 1.0f, 0.25f, false);
        assert(std::abs(COLOR::Ev100FromExposure(incident.exposure) - 15.5f) < 1e-3f && !incident.meteringValid);
        // 直射日光の下で空の多い画（測光 EV100 16.8、入射 15.5）は 1 段まで明るく戻す。
        const float skyLuminance = std::exp2(16.8f) / 8.0f;
        const COLOR::ExposureFrameState sunlit = COLOR::BuildAutomaticExposureFrame(
            automatic, COLOR::ExposureIllumination{15.5f, 1.0f}, skyLuminance, skyLuminance, 1.0f, 1.0f, 0.25f, true);
        assert(std::abs(COLOR::Ev100FromExposure(sunlit.targetExposure) - 15.8f) < 1e-3f);
        // 暴風雨（入射 EV100 11）は昼との差 4 段の 3 割を残し、1.2 段暗い。
        const float stormLuminance = std::exp2(12.0f) / 8.0f;
        const COLOR::ExposureFrameState storm =
            COLOR::BuildAutomaticExposureFrame(automatic, COLOR::ExposureIllumination{11.0f, 0.0f}, stormLuminance,
                                               stormLuminance, 1.0f, 1.0f, 0.25f, true);
        assert(std::abs(COLOR::Ev100FromExposure(storm.targetExposure) - 13.2f) < 1e-3f);
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    VerifyViewOrientation();
    VerifyRasterFaceConvention();
    using Microsoft::WRL::ComPtr;
    HIKARI::TESTS::INTERNAL::GpuContractDevice gpu;
    ComPtr<ID3DBlob> errors, signature;
    const auto source =
        std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT) / "Tools/Tests/Shaders/HIKARI_RenderMathContracts.hlsl";
    // 雲の位相関数は雲の定数（VolumetricCloudCB、b0）の銀の縁の倍率 look.y を読む。全部 1 にした定数を渡し、
    // 倍率 1 の時に全球で 1 に正規化されることを確かめる。
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 0;
    const D3D12_DESCRIPTOR_RANGE atlasRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 4, 0, 0};
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable = {1, &atlasRange};
    const CD3DX12_STATIC_SAMPLER_DESC sampler(1, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                                              D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                              D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    const D3D12_ROOT_SIGNATURE_DESC rootDesc{3, parameters, 1, &sampler};
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    // 積分と GI の問い合わせは別 kernel。製品と同じ DXC / SM6 と descriptor index を使う。
    const std::array<const char*, 2> entries{"Main", "DynamicGiMain"};
    std::array<ComPtr<ID3D12PipelineState>, 2> pipelines;
    for (size_t index = 0; index < entries.size(); ++index) {
        ComPtr<ID3DBlob> shader;
        assert(HIKARI::GFX::CompileShaderFileSm6(source.c_str(), entries[index], L"cs_6_0", &shader));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.pRootSignature = root.Get();
        pipelineDesc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
        assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelines[index]))));
    }
    constexpr size_t count = 21 * 21;
    constexpr size_t bytes =
        (count + 21 + 4 + 21 + count + 21 * 4 + 21 * 3 + 21 * 4 + 21 + 21 + 21 * 3 + 21 + 18 + 13 + 101 + 21) *
        sizeof(std::array<float, 4>);
    ComPtr<ID3D12Resource> output, readback, cloudConstants;
    const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto cloudDesc = CD3DX12_RESOURCE_DESC::Buffer(512);
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cloudDesc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(&cloudConstants))));
    void* cloudMapped = nullptr;
    assert(SUCCEEDED(cloudConstants->Map(0, nullptr, &cloudMapped)));
    std::fill_n(static_cast<float*>(cloudMapped), 512 / sizeof(float), 1.0f);
    cloudConstants->Unmap(0, nullptr);
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto readHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto outputDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                         IID_PPV_ARGS(&output))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(
        &readHeap, D3D12_HEAP_FLAG_NONE, &readDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))));
    gpu.ResetCommands();
    // 4x2x2 probe。最後の x 列は未初期化、他は全方向で一定の放射輝度と既知の可視性。
    constexpr UINT atlasWidth = 4 * 2 * 30, atlasHeight = 2 * 18;
    const D3D12_DESCRIPTOR_HEAP_DESC atlasHeapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3,
                                                   D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ComPtr<ID3D12DescriptorHeap> atlasHeap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&atlasHeapDesc, IID_PPV_ARGS(&atlasHeap))));
    std::array<ComPtr<ID3D12Resource>, 3> atlases, uploads;
    const UINT descriptorSize = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT variant = 0; variant < 3; ++variant) {
        // 変種 1 は全 probe を +x へ 0.25 動かした状態、変種 2 は全 probe の +x 側 0.5 m に円盤を置く。
        std::vector<std::array<float, 4>> atlasTexels(atlasWidth * atlasHeight);
        for (UINT y = 0; y < atlasHeight; ++y)
            for (UINT x = 0; x < atlasWidth; ++x) {
                const UINT probeX = (x / 30) % 4;
                const float stored = std::pow(float((probeX + 1) * (probeX + 1)), .2f);
                const UINT px = x % 30, py = y % 18;
                std::array<float, 4> texel{};
                if (px < 10 && py < 10)
                    texel = {stored, stored, stored, .75f};
                else if (px >= 10 && px < 28)
                    texel = {1.0f, 1.0f, probeX == 3 ? 0.0f : 1.0f, 0.0f};
                else if (px < 8 && variant == 2)
                    texel = {0.0f, 0.5f, -0.05f, 0.05f};
                else if (px == 28 && py == 0)
                    texel = {variant == 1 ? 0.25f : 0.0f, 0.0f, 0.0f, probeX == 3 ? 0.0f : 1.0f};
                atlasTexels[y * atlasWidth + x] = texel;
            }
        const auto atlasDesc =
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, atlasWidth, atlasHeight, 1, 1);
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &atlasDesc,
                                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&atlases[variant]))));
        const auto atlasUploadDesc =
            CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(atlases[variant].Get(), 0, 1));
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &atlasUploadDesc,
                                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                             IID_PPV_ARGS(&uploads[variant]))));
        const D3D12_SUBRESOURCE_DATA atlasData{atlasTexels.data(), atlasWidth * sizeof(atlasTexels[0]),
                                               atlasWidth * atlasHeight * sizeof(atlasTexels[0])};
        UpdateSubresources(gpu.commands.Get(), atlases[variant].Get(), uploads[variant].Get(), 0, 0, 1, &atlasData);
        const auto atlasBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            atlases[variant].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.commands->ResourceBarrier(1, &atlasBarrier);
        const CD3DX12_CPU_DESCRIPTOR_HANDLE handle(atlasHeap->GetCPUDescriptorHandleForHeapStart(), variant,
                                                   descriptorSize);
        gpu.device->CreateShaderResourceView(atlases[variant].Get(), nullptr, handle);
    }
    ID3D12DescriptorHeap* atlasHeaps[]{atlasHeap.Get()};
    for (size_t index = 0; index < pipelines.size(); ++index) {
        if (index != 0)
            gpu.ResetCommands();
        gpu.commands->SetPipelineState(pipelines[index].Get());
        gpu.commands->SetComputeRootSignature(root.Get());
        gpu.commands->SetDescriptorHeaps(1, atlasHeaps);
        gpu.commands->SetComputeRootDescriptorTable(2, atlasHeap->GetGPUDescriptorHandleForHeapStart());
        gpu.commands->SetComputeRootUnorderedAccessView(0, output->GetGPUVirtualAddress());
        gpu.commands->SetComputeRootConstantBufferView(1, cloudConstants->GetGPUVirtualAddress());
        gpu.commands->Dispatch(index == 0 ? 21 : 101, index == 0 ? 21 : 1, 1);
        gpu.SubmitAndWait(index + 1);
    }
    gpu.ResetCommands();
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                              D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.commands->ResourceBarrier(1, &barrier);
    gpu.commands->CopyResource(readback.Get(), output.Get());
    gpu.SubmitAndWait(3);
    const D3D12_RANGE range{0, bytes};
    void* mapped = nullptr;
    assert(SUCCEEDED(readback->Map(0, &range, &mapped)));
    const auto* values = static_cast<const std::array<float, 4>*>(mapped);
    float maximumWhiteResponse = 0;
    for (size_t i = 0; i < 21; ++i) {
        const auto& phase = values[1201 + i * 3];
        assert(std::abs(phase[0] - 1) < .001f);
        assert(i > 10 ? phase[1] > phase[2] : i < 10 ? phase[1] < phase[2] : phase[1] == phase[2]);
        const auto& scattering = values[1202 + i * 3];
        assert(scattering[0] == 0 && std::abs(scattering[3] - .125f) < 1e-6f);
        assert(std::abs((scattering[2] - .125f) - 2 * (scattering[1] - .125f)) < 1e-5f);
        const auto& cloud = values[1203 + i * 3];
        assert(std::abs(cloud[0] - 1) < .002f);
        assert(cloud[1] > cloud[2] && cloud[2] > cloud[3] && cloud[3] >= 0 && cloud[3] < 0.01f * cloud[1]);
    }
    for (size_t i = 0; i < 21; ++i) {
        const auto& integral = values[1264 + i];
        const double tau = integral[3];
        const double expected = 3.0 * (-std::expm1(-tau)) / tau;
        assert(std::abs(integral[0] - expected) < std::max(1e-8, expected * 0.0001));
        assert(integral[1] == 3.0f);
        assert(std::abs(integral[2] - 3.0 * (-std::expm1(-2 * tau)) / (2 * tau)) < std::max(1e-8, expected * 0.0001));
    }
    for (size_t i = 0; i < count; ++i) {
        for (float value : values[i])
            assert(std::isfinite(value) && value >= 0.0f);
        assert(values[i][3] <= 1.01f);
        assert(values[i][2] <= values[i][3]);
        std::fflush(stdout);
        maximumWhiteResponse = (std::max)(maximumWhiteResponse, values[i][3]);
        std::fflush(stdout);
    }
    assert(values[2 * 21][2] > values[2 * 21 + 20][2]);
    assert(std::abs(values[20][2] - 0.04f) < 0.006f);
    assert(values[20 * 21 + 20][3] < 0.5f);
    for (size_t i = 0; i < 21; ++i) {
        const auto& sample = values[count + i];
        const double alpha = static_cast<double>(sample[2]) * sample[2];
        const double expectedPeak = 1.0 / (3.141592653589793 * alpha * alpha);
        if (std::abs(sample[0] / expectedPeak - 1.0) >= 0.002)
            std::fprintf(stderr, "GGX peak mismatch roughness=%.4f actual=%.4f expected=%.4f\n", sample[2], sample[0],
                         expectedPeak);
        assert(std::abs(sample[0] / expectedPeak - 1.0) < 0.002);
        assert(std::isfinite(sample[1]) && sample[1] > 0.0f && sample[1] <= sample[0]);
    }
    const D3D12_RANGE written{0, 0};
    for (size_t i = 0; i < 2; ++i) {
        const auto& sample = values[462 + i];
        assert(std::abs(sample[0] - 0.2f) < 0.0001f);
        assert(std::abs(sample[1] - 0.8f) < 0.0001f);
        assert(std::abs(sample[2] - 0.9f) < 0.0001f);
        assert(std::abs(sample[3] - 0.3f) < 0.0001f);
    }
    assert(std::abs(values[464][0] - 0.72f) < 0.0001f);
    assert(std::abs(values[465][0] - 0.86f) < 0.0001f);
    assert(std::abs(values[465][1] - (0.8f * 0.75f * 0.45f)) < 0.0001f);
    for (size_t i = 0; i < 21; ++i) {
        // 逆二乗を range 20 の手前で滑らかに 0 へ落とす（発光体半径 0 は 1 cm で頭打ち）。
        const double distance = std::abs(10.0 - static_cast<double>(i));
        const double window = 1.0 - std::pow(distance / 20.0, 4.0);
        const double expected = window * window / (std::max)(distance * distance, 1e-4);
        const auto& sample = values[466 + i];
        assert(std::abs(sample[0] - expected) <= 1e-4 * (std::max)(1.0, expected));
        assert(std::abs(sample[1] - sample[0] * 0.5f) < 0.0001f);
        assert(std::abs(sample[2] - sample[0] * 2.0f) < 0.0001f);
        const float cone = std::clamp((static_cast<float>(i) / 20.0f - 0.6f) / 0.2f, 0.0f, 1.0f);
        assert(std::abs(sample[3] - cone * cone * cone) < 0.0001f);
    }
    for (size_t row = 0; row < 21; ++row) {
        const double authored = (std::max)(0.04, static_cast<double>(row) / 20.0);
        float previous = 0.0f;
        for (size_t column = 0; column < 21; ++column) {
            const auto& sample = values[487 + row * 21 + column];
            for (float value : sample)
                assert(std::isfinite(value) && value >= 0.03999f && value <= 1.0f);
            assert(std::abs(sample[0] - sample[1]) < 0.00001f);
            assert(std::abs(sample[0] - sample[2]) < 0.00001f);
            assert(std::abs(sample[3] - authored) < 0.00001f);
            assert(sample[0] >= previous && sample[0] >= sample[3] - 0.00001f);
            const double slope = static_cast<double>(column) / 20.0;
            const double expectedAlphaSquared =
                (std::min)(1.0, std::pow(authored, 4.0) + (std::min)(0.18, 0.3 * 2.4125 * slope * slope));
            assert(std::abs(std::pow(sample[0], 4.0) - expectedAlphaSquared) < 0.00001);
            previous = sample[0];
        }
    }
    for (size_t i = 0; i < 21; ++i) {
        using Vector = std::array<double, 3>;
        const auto normalize = [](Vector value) {
            const double length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
            for (double& component : value)
                component /= length;
            return value;
        };
        const double sx = (0.25 + 0.1 * i) * ((i & 1) ? -1.0 : 1.0);
        const double sign = (i % 3 == 0 ? -1.0 : 1.0) * ((i & 1) ? -1.0 : 1.0);
        const Vector normal = normalize({1.0 / sx, (1.0 - 0.25 / sx) / 1.75, 1.0 / 0.7});
        const Vector tangent = normalize({sx - 0.25, -1.75, 0});
        const Vector bitangent{sign * (normal[1] * tangent[2] - normal[2] * tangent[1]),
                               sign * (normal[2] * tangent[0] - normal[0] * tangent[2]),
                               sign * (normal[0] * tangent[1] - normal[1] * tangent[0])};
        for (size_t axis = 0; axis < 3; ++axis) {
            const double expected = 0.3 * tangent[axis] + 0.4 * bitangent[axis] + std::sqrt(0.75) * normal[axis];
            assert(std::abs(values[928 + i * 4][axis] - tangent[axis]) < 0.00001);
            assert(std::abs(values[929 + i * 4][axis] - expected) < 0.00001);
            assert(std::abs(values[930 + i * 4][axis] - normal[axis]) < 0.00001);
            assert(std::abs(values[931 + i * 4][axis] - normal[axis]) < 0.00001);
        }
        assert(values[928 + i * 4][3] == sign);
    }
    for (size_t i = 0; i < 21; ++i) {
        const auto& point = values[1012 + i * 3];
        const auto& spot = values[1013 + i * 3];
        const auto& rangeSample = values[1014 + i * 3];
        for (float value : point)
            assert(std::isfinite(value) && value >= 0.0f);
        assert(point[0] == point[3]);
        assert(point[1] == point[3] * 0.5f && point[2] == point[3] * 2.0f);
        if (i > 0) {
            assert(point == spot); // 同じ向きの Point と Spot は中心で一致する。
            assert(point[3] <= values[1012 + (i - 1) * 3][3]);
        }
        if (i >= 10)
            assert(point[3] == 0.0f);
        assert(rangeSample[2] == 0.0f);
        // 逆二乗を range の手前で滑らかに 0 へ落とし、発光体半径より内側は頭打ちにする。
        const double window = (std::max)(0.0, 1.0 - std::pow(double(i) / 10.0, 4.0));
        const double clamped = window * window / (std::max)(double(i * i), 0.25);
        assert(std::abs(rangeSample[3] - clamped) <= 1e-5 * (std::max)(1.0, clamped));
        if (i > 0 && i <= 10) {
            // 遠い範囲端を動かしても、近傍の強度と逆二乗則は保たれる。
            assert(std::abs(rangeSample[0] * double(i * i) - 1.0) < 0.00021);
            assert(std::abs(rangeSample[0] / rangeSample[1] - 1.0f) < 0.00021f);
        }
    }
    assert(std::abs(values[1017][0] / values[1020][0] - 4.0f) < 0.0001f);
    for (size_t i = 0; i < 21; ++i) {
        const auto& scaled = values[1076 + i * 4];
        const auto& rotated = values[1077 + i * 4];
        assert(std::abs(scaled[0] - 0.05f * i) < 0.00001f);
        assert(std::abs(scaled[1] - 0.4f * i) < 0.00001f);
        assert(std::abs(scaled[2] - 0.075f * i) < 0.00001f);
        const double elevation = (double(i) - 10) / 10;
        assert(std::abs(rotated[0]) < 0.00001f && std::abs(rotated[1] - elevation) < 0.00001);
        assert(std::abs(rotated[2] - std::sqrt((std::max)(0.0, 1 - elevation * elevation))) < 0.00001);
    }
    for (size_t i = 0; i < 21; ++i) {
        const auto& finiteSource = values[1180 + i];
        const double distance = double(i) * 0.1;
        const double angle = std::asin((std::min)(1.0, 0.5 / (std::max)(distance, 0.0001)));
        assert(std::abs(finiteSource[0] - std::pow(std::sin(angle * 0.5), 2.0)) < 1e-6);
        assert(finiteSource[3] == 0.0f);
        assert(std::isfinite(finiteSource[1]) && finiteSource[1] <= 4.0f);
        const double window = 1.0 - std::pow(distance / 10.0, 4.0);
        assert(std::abs(finiteSource[1] - window * window / (std::max)(distance * distance, 0.25)) < 1e-6);
        assert(std::abs(finiteSource[1] / finiteSource[2] - 1) < 0.004);
    }
    constexpr float packedSpriteUvExpected[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.3f, 0.2f, 0.2f, 0.7f},
    };
    for (size_t index = 0u; index < 4u; ++index) {
        for (size_t channel = 0u; channel < 4u; ++channel) {
            assert(std::abs(values[1285u + index][channel] - packedSpriteUvExpected[index][channel]) < 0.00001f);
        }
    }
    constexpr std::array<std::array<float, 2>, 4> flippedPackedSpriteUvExpected{{
        {1.0f, 1.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
        {0.3f, 0.8f},
    }};
    for (size_t index = 0u; index < flippedPackedSpriteUvExpected.size(); ++index) {
        assert(std::abs(values[1308u + index][0] - flippedPackedSpriteUvExpected[index][0]) < 0.00001f);
        assert(std::abs(values[1308u + index][1] - flippedPackedSpriteUvExpected[index][1]) < 0.00001f);
    }
    const auto& rightLitNormal = values[1289u];
    const auto& upperLitNormal = values[1290u];
    assert(rightLitNormal[0] > 0.59f && rightLitNormal[1] < 0.01f && rightLitNormal[2] > 0.79f);
    assert(upperLitNormal[0] < 0.01f && upperLitNormal[1] > 0.59f && upperLitNormal[2] > 0.79f);
    for (size_t channel = 0u; channel < 3u; ++channel) {
        assert(std::abs(values[1289u][channel] - values[1291u][channel]) < 0.00001f);
        assert(std::abs(values[1290u][channel] - values[1292u][channel]) < 0.00001f);
        assert(std::abs(values[1293u][channel] + values[1289u][channel]) < 0.00001f);
    }
    assert(values[1312u][0] < -0.59f && values[1312u][1] < 0.01f && values[1312u][2] > 0.79f);
    assert(values[1313u][0] > 0.59f && values[1313u][1] < 0.01f && values[1313u][2] < -0.79f);
    constexpr std::array<float, 3> orthographicForward{0.36f, 0.48f, -0.8f};
    assert(values[1294u][0] == 1.0f && values[1301u][0] == 1.0f);
    for (size_t axis = 0u; axis < orthographicForward.size(); ++axis) {
        assert(std::abs(values[1294u][axis + 1u] - orthographicForward[axis]) < 0.00002f);
        assert(std::abs(values[1295u][axis] - orthographicForward[axis]) < 0.00002f);
        assert(std::abs(values[1296u][axis] - orthographicForward[axis]) < 0.00002f);
        assert(std::abs(values[1301u][axis + 1u] - orthographicForward[axis]) < 0.00002f);
    }
    assert(std::abs(values[1295u][3] - 7.0f) < 0.0001f && std::abs(values[1296u][3] - 7.0f) < 0.0001f);
    assert(std::abs(values[1297u][3]) < 0.0001f && std::abs(values[1298u][3]) < 0.0001f);
    assert(std::abs(values[1299u][3] - 7.0f) < 0.0001f && std::abs(values[1300u][3] - 7.0f) < 0.0001f);
    double lateralOriginDelta = 0.0;
    for (size_t axis = 0u; axis < 3u; ++axis) {
        lateralOriginDelta += std::pow(double(values[1297u][axis] - values[1298u][axis]), 2.0);
    }
    assert(lateralOriginDelta > 1.0);
    assert(std::abs(values[1302u][0] - values[1302u][1]) < 0.0001f);
    assert(std::abs(values[1302u][0] - values[1302u][2]) > 0.01f);
    const auto& receiverPlaneGradient = values[1303u];
    assert(std::isfinite(receiverPlaneGradient[0]) && std::isfinite(receiverPlaneGradient[1]));
    assert(std::abs(receiverPlaneGradient[2]) < 0.00001f && std::abs(receiverPlaneGradient[3]) < 0.00001f);
    const auto& receiverTap = values[1304u];
    assert(receiverTap[0] >= 0.0f && receiverTap[0] < 1.0f && receiverTap[1] >= 0.0f && receiverTap[1] < 1.0f);
    assert(std::abs(receiverTap[0] - 0.5f) > 0.1f && std::abs(receiverTap[1] - 0.5f) > 0.1f);
    assert(std::abs(receiverTap[2] - 1.0f) < 0.0001f && std::abs(receiverTap[3] + 1.0f) < 0.0001f);
    const auto expectedFootprintBias = [](float gradient, float phase) {
        return ((std::max)(gradient, 0.0f) * phase + (std::max)(-gradient, 0.0f) * (1.0f - phase)) / 64.0f;
    };
    const float receiverFootprintBias = expectedFootprintBias(receiverPlaneGradient[0], receiverTap[0]) +
                                        expectedFootprintBias(receiverPlaneGradient[1], receiverTap[1]);
    assert(values[1305u][3] == 1.0f && receiverFootprintBias > 0.0f);
    assert(std::abs(values[1305u][2] - receiverFootprintBias) < 0.000001f);
    const float receiverTapDepth = values[1305u][1] + receiverPlaneGradient[0] * receiverTap[2] / 64.0f +
                                   receiverPlaneGradient[1] * receiverTap[3] / 64.0f - receiverFootprintBias;
    assert(std::abs(values[1305u][0] - receiverTapDepth) < 0.000001f);
    assert(receiverFootprintBias <=
           (std::abs(receiverPlaneGradient[0]) + std::abs(receiverPlaneGradient[1])) / 64.0f + 0.000001f);
    const auto& edgeReceiverTap = values[1306u];
    assert(std::abs(edgeReceiverTap[0] - 0.244f) < 0.001f && std::abs(edgeReceiverTap[1] + 0.244f) < 0.001f);
    assert(edgeReceiverTap[2] == 0.0f && edgeReceiverTap[3] == 0.0f);
    const float edgeFootprintBias = expectedFootprintBias(receiverPlaneGradient[0], edgeReceiverTap[2]) +
                                    expectedFootprintBias(receiverPlaneGradient[1], edgeReceiverTap[3]);
    const float edgeTapDepth = values[1307u][1] + receiverPlaneGradient[0] * edgeReceiverTap[0] / 64.0f +
                               receiverPlaneGradient[1] * edgeReceiverTap[1] / 64.0f - edgeFootprintBias;
    assert(values[1307u][3] == 1.0f);
    assert(edgeFootprintBias >= 0.0f);
    assert(std::abs(values[1307u][2] - edgeFootprintBias) < 0.000001f);
    assert(std::abs(values[1307u][0] - edgeTapDepth) < 0.000001f);
    assert(std::abs(values[1314][0] - 0.4f) < 1e-5f);
    assert(std::abs(values[1314][1] - 0.2f) < 1e-5f);
    assert(values[1314][2] == 0 && values[1314][3] == 0);
    assert(values[1315][0] >= 0 && values[1315][0] <= 0.4f);
    assert(values[1315][1] >= 0 && values[1315][1] <= 0.4f);
    assert(std::abs(values[1315][0] - values[1315][1]) > 1e-6f);
    assert(std::abs(values[1315][2] - std::exp(-2.0f)) < 1e-5f);
    assert(std::abs(values[1315][3] + values[1315][2] - 1.0f) < 1e-5f);
    for (size_t i = 0; i < 21; ++i) {
        const float x = .8f + float(i) * .02f;
        const float expected = (x + 1) * (x + 1);
        assert(std::abs(values[1316 + i][0] - expected) < .0001f);
        assert(values[1316 + i][1] == 1 && values[1316 + i][2] == 1);
        assert(std::abs(values[1316 + i][3] - .75f) < 1e-5f);
        assert(std::abs(values[1337 + i][0] - values[1395 + i][0]) < 1e-5f);
        assert(values[1337 + i][1] == 1 && values[1395 + i][1] == 1);
        // 上位の層（強度 4）が全面を覆うので、下位の層（強度 1）は混ざらない。
        assert(std::abs(values[1358 + i][0] - expected * 4.0f) < .0005f);
        assert(values[1358 + i][3] == 1);
    }
    // 強度 0 は有効な黒。影響の外は支持なし（下位の層か空へ）。
    assert(values[1379][0] == 0 && values[1379][3] == 1);
    assert(values[1380][0] == 0 && values[1380][3] == 0);
    assert(values[1381][0] == 0 && values[1381][3] == 0);
    assert(std::abs(values[1382][0] - 4) < 1e-5f && values[1382][3] == 1);
    assert(std::abs(values[1383][0] - 1.05f * 1.05f * .5f) < .0001f);
    assert(std::abs(values[1383][3] - .5f) < 1e-5f);
    // 面の裏側の probe も小さな重みで混ざる（RTXGI の wrap shading、0 にはしない）。表側の probe が主になる。
    // 表側の probe が初期化前なら、支持は裏側の probe の補間重み（法線方向の偏移後 0.0125）だけになる。
    assert(values[1384][0] > 8.8f && values[1384][0] < 9.0f && values[1384][3] == 1);
    assert(std::abs(values[1385][0] - 9) < .0001f && std::abs(values[1385][3] - .0125f) < 1e-4f);
    // 再配置：裏側の probe（値 1）は少し混ざり、表側の probe（値 4）が主になる。格子点から +x へ動いた probe は
    // 受光面に近づいて面の向きの重みが増え、値は 1 の側へ寄る。
    assert(values[1386][0] > 2.5f && values[1386][0] < 4.0f && values[1386][3] == 1);
    assert(values[1387][0] > 1.0f && values[1387][0] < values[1386][0] && values[1387][3] == 1);
    assert(values[1388][0] == 0 && values[1388][3] == 0);
    // 円盤：probe から受光点への線分が形状を貫けばその probe は外れ、残りの probe（裏側でも）が支える。
    // 円盤の手前と probe の上は表側の probe が主になる。
    assert(std::abs(values[1389][0] - 9) < .0001f && values[1389][3] == 1);
    assert(std::abs(values[1390][0] - 4) < .0001f && values[1390][3] == 1);
    assert(values[1391][0] > 4.0f && values[1391][0] < 4.5f && values[1391][3] == 1);
    assert(values[1392][0] == 0 && values[1392][1] == 1 && values[1392][2] == 1 && values[1392][3] == 1);
    assert(values[1393][0] == 1 && values[1393][1] == 0);
    assert(values[1393][2] > 0 && values[1393][2] == values[1393][3]);
    // 上位の層が全面を覆えば、下位の層は混ぜない（収束途中かどうかでは受け渡さない）：上位の層だけの値（1416）と同じ。
    assert(std::abs(values[1394][0] - values[1416][0]) < 1e-5f && values[1394][3] == 1 && values[1416][1] == 1);
    // GPU の実際の点光源評価と比較し、光錐を跨ぐ区間や零長区間も照明を落とさない。
    for (size_t i = 0u; i < 21u; ++i) {
        const auto& segment = values[1417u + i];
        assert(segment[0] == 0.0f && segment[1] > 0.0f && segment[2] > 0.0f && segment[3] == 1.0f);
    }
    std::printf("Local-light segment rejection: illuminated samples and boundary cases preserved.\n");
    readback->Unmap(0, &written);
    std::printf("PBR GPU reference grid: %zu samples, maximum unit-reflector response %.6f\n", count,
                maximumWhiteResponse);
    return 0;
}
