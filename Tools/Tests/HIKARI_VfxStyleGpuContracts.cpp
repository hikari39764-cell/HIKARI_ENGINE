#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <d3dx12.h>
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Internal/HIKARI_GpuContractDevice.h"

void CheckPreservedVfxLayer();
void CheckSceneCaptureStyles();

// 最適化前の評価順と製品 shader を同じ入力で比較する。画像の品質や速度の測定ではない。
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using Microsoft::WRL::ComPtr;
    using namespace HIKARI;
    TESTS::INTERNAL::GpuContractDevice gpu;
    constexpr UINT width = 16, pixels = width * width;
    constexpr UINT kinds[] = {
#define HIKARI_EFFECT(name, id) id,
#include "Include/Vfx/HIKARI_MaterialEffectKinds.hlsli"
#undef HIKARI_EFFECT
    };
    constexpr UINT cases = UINT(std::size(kinds)) * 2;
    const auto sourcePath = std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT) / "HIKARI/Shaders/Post_VfxStylePS.hlsl";
    const std::string shader = "#define main OptimizedStyleMain\n#include \"" + sourcePath.generic_string() + R"hlsl("
#undef main
RWStructuredBuffer<float4> differences : register(u0);
float4 PreviousStyleMain(HikariPostEffectInput input) {
    float4 source=StyleRead(input.uv);
    uint kind=(uint)gUser[0].x;
    float4 filtered=StyleFilter(kind,input.uv,source);
    float4 styled=StyleSurface(kind,input.uv,filtered);
    if(StyleCoverage(source)<=0 && kind!=VfxBlur && kind!=VfxStreak && kind!=VfxRadialBlur &&
        kind!=VfxGlow && kind!=VfxOutline && kind!=VfxAlphaMorph && kind!=VfxInk && kind!=VfxChromatic && kind!=VfxPixelate) styled=0;
    float4 result=lerp(source,styled,StyleWeight(input.uv,source));
    result=float4(clamp(result.rgb,0,65504),saturate(result.a));
    return any(isnan(result))?source:result;
}
[numthreads(8,8,1)] void Compare(uint3 id:SV_DispatchThreadID) {
    HikariPostEffectInput input;
    input.position=float4(id.xy+.5,0,1); input.uv=(id.xy+.5)/16;
    float4 before=PreviousStyleMain(input), after=OptimizedStyleMain(input);
    differences[(uint)gCommonPadding.x*256+id.y*16+id.x]=(after-before)/max(abs(before),1);
}
)hlsl";
    ComPtr<ID3DBlob> code;
    assert(GFX::CompileShaderSourceSm6(shader.data(), shader.size(), sourcePath.c_str(), "Compare",
                                       GFX::ShaderStage::Compute, &code));
    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    std::array<CD3DX12_ROOT_PARAMETER, 3> roots;
    roots[0].InitAsConstantBufferView(0);
    roots[1].InitAsDescriptorTable(1, &range);
    roots[2].InitAsUnorderedAccessView(0);
    const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                              D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(roots.size()), roots.data(), 1, &sampler);
    ComPtr<ID3DBlob> signature, error;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = root.Get();
    pipelineDesc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline))));
    const auto buffer = [&](UINT64 size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> resource;
        const CD3DX12_HEAP_PROPERTIES heap(heapType);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, heapType == D3D12_HEAP_TYPE_DEFAULT
                                                                  ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                                  : D3D12_RESOURCE_FLAG_NONE);
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                             IID_PPV_ARGS(&resource))));
        return resource;
    };
    auto constants = buffer(cases * 512, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto upload = buffer(pixels * 16, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto output = buffer(cases * pixels * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto readback = buffer(cases * pixels * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    void* mapped = nullptr;
    assert(SUCCEEDED(constants->Map(0, nullptr, &mapped)));
    std::memset(mapped, 0, cases * 512);
    for (UINT i = 0; i < cases; ++i) {
        float* p = static_cast<float*>(mapped) + i * 128;
        p[0] = .37f;
        p[3] = 1;
        p[4] = p[5] = float(width);
        p[6] = float(i);
        p[8] = float(kinds[i / 2]);
        p[9] = i % 2 ? .63f : 1;
        p[10] = i % 2 ? 1.f : 0.f;
        // フィルタ半径・階調・HDR 色・部分的なマスクを含む入力。
        const std::array<float, 20> parameters{.15f, .3f, .7f, .8f, .23f, .09f, .4f, .6f, .3f, .05f,
                                               .1f,  1,   4,   1,   .2f,  1,    .5f, .5f, .2f, .15f};
        std::memcpy(p + 12, parameters.data(), sizeof(parameters));
    }
    constants->Unmap(0, nullptr);
    assert(SUCCEEDED(upload->Map(0, nullptr, &mapped)));
    std::memset(mapped, 0, pixels * 16);
    for (UINT y = 4; y < 12; ++y)
        for (UINT x = 4; x < 12; ++x) {
            float* p = static_cast<float*>(mapped) + (y * width + x) * 4;
            p[0] = 4;
            p[1] = .2f;
            p[2] = 1;
            p[3] = x < 8 ? 0.f : .5f;
        }
    upload->Unmap(0, nullptr);
    ComPtr<ID3D12Resource> texture;
    const auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, width, 1, 1);
    const CD3DX12_HEAP_PROPERTIES textureHeap(D3D12_HEAP_TYPE_DEFAULT);
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&textureHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&texture))));
    const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1,
                                              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
    gpu.device->CreateShaderResourceView(texture.Get(), nullptr, heap->GetCPUDescriptorHandleForHeapStart());
    gpu.ResetCommands();
    auto* commands = gpu.commands.Get();
    const CD3DX12_TEXTURE_COPY_LOCATION destination(texture.Get(), 0);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, width, width, 1, width * 16};
    const CD3DX12_TEXTURE_COPY_LOCATION from(upload.Get(), footprint);
    commands->CopyTextureRegion(&destination, 0, 0, 0, &from, nullptr);
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->ResourceBarrier(1, &transition);
    ID3D12DescriptorHeap* heaps[]{heap.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(root.Get());
    commands->SetPipelineState(pipeline.Get());
    commands->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
    commands->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
    for (UINT i = 0; i < cases; ++i) {
        commands->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress() + i * 512);
        commands->Dispatch(2, 2, 1);
    }
    transition = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                      D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->ResourceBarrier(1, &transition);
    commands->CopyResource(readback.Get(), output.Get());
    gpu.SubmitAndWait(1);
    assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
    for (UINT i = 0; i < cases * pixels * 4; ++i) {
        const float difference = static_cast<const float*>(mapped)[i];
        assert(std::isfinite(difference) && std::abs(difference) <= 1e-4f);
    }
    readback->Unmap(0, nullptr);
    std::printf("VFX style GPU equivalence: %u effects, empty/HDR/additive pixels, full/partial mask PASS\n",
                UINT(std::size(kinds)));
    CheckPreservedVfxLayer();
    CheckSceneCaptureStyles();
}
