#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundRunner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

#include <DirectXTex.h>
#include <Windows.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_ShaderCookCompiler.h"

namespace HIKARI::EDITOR::PLAYGROUND {
namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kMaximumSheetSide = 8192u;
constexpr uint32_t kMaximumImageSide = 4096u;
constexpr uint32_t kTimedDraws = 8u;
constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr const char* kStyleModelsSource = "Include/Playground/HIKARI_PlaygroundStyleModels.hlsl";

// HLSL の cbuffer PlaygroundGlobals と同じ並び。
struct Globals {
    float sheetSize[2];
    float tileSize[2];
    uint32_t columns;
    uint32_t tileCount;
    uint32_t scene;
    uint32_t hasImage;
    float ambientColor[3];
    float gap;
    float curveRange[2];
    float exposure;
    float padding;
    MATH::Vec4 tiles[kMaxTiles * kTileSlots];
};
static_assert(sizeof(Globals) == 64u + kMaxTiles * kTileSlots * 16u);
constexpr UINT64 kGlobalsBytes = (sizeof(Globals) + 255u) & ~UINT64{255u};

bool Check(HRESULT result, const char* what, std::string& error) {
    if (SUCCEEDED(result))
        return true;
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
    error = std::string(what) + " failed (" + code + ")";
    return false;
}

std::string Narrow(const wchar_t* text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? static_cast<size_t>(bytes - 1) : 0u, '\0');
    if (bytes > 1)
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

// エディタの描画とは独立した使い捨てのデバイス（資源は全てこの関数の中で作って捨てる。エンジンの
// 显存の記账はエンジンのデバイスのものなので、ここは対象外）。
struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 fenceValue = 0u;
    std::string adapter;
    bool software = false;

    ~Gpu() {
        if (event != nullptr)
            CloseHandle(event);
    }

    bool Create(Device kind, std::string& error) {
        ComPtr<IDXGIFactory6> factory;
        if (!Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1", error))
            return false;
        if (kind == Device::Gpu) {
            ComPtr<IDXGIAdapter1> candidate;
            for (UINT index = 0u; factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                      IID_PPV_ARGS(&candidate)) != DXGI_ERROR_NOT_FOUND;
                 ++index) {
                DXGI_ADAPTER_DESC1 desc{};
                candidate->GetDesc1(&desc);
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u &&
                    SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
                    adapter = Narrow(desc.Description);
                    break;
                }
            }
        }
        if (device == nullptr) {
            ComPtr<IDXGIAdapter> warp;
            if (!Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter", error) ||
                !Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
                       "D3D12CreateDevice (WARP)", error))
                return false;
            adapter = "WARP";
            software = true;
        }
        const D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_DIRECT};
        if (!Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue", error) ||
            !Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                   "CreateCommandAllocator", error) ||
            !Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&commands)),
                   "CreateCommandList", error) ||
            !Check(commands->Close(), "CommandList::Close", error) ||
            !Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence", error))
            return false;
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return event != nullptr || Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent", error);
    }

    bool Begin(std::string& error) {
        return Check(allocator->Reset(), "CommandAllocator::Reset", error) &&
               Check(commands->Reset(allocator.Get(), nullptr), "CommandList::Reset", error);
    }

    bool SubmitAndWait(std::string& error) {
        if (!Check(commands->Close(), "CommandList::Close", error))
            return false;
        ID3D12CommandList* lists[]{commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (!Check(queue->Signal(fence.Get(), ++fenceValue), "Queue::Signal", error) ||
            !Check(fence->SetEventOnCompletion(fenceValue, event), "Fence::SetEventOnCompletion", error))
            return false;
        if (WaitForSingleObject(event, 120000) != WAIT_OBJECT_0)
            return Check(device->GetDeviceRemovedReason() != S_OK ? device->GetDeviceRemovedReason() : E_ABORT,
                         "waiting for the GPU", error);
        return true;
    }

    bool Buffer(UINT64 bytes, D3D12_HEAP_TYPE heap, ComPtr<ID3D12Resource>& out, std::string& error) const {
        const CD3DX12_HEAP_PROPERTIES properties(heap);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
        const D3D12_RESOURCE_STATES state =
            heap == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
        return Check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                     IID_PPV_ARGS(&out)),
                     "CreateCommittedResource (buffer)", error);
    }
};

bool CompileProgram(const Spec& spec, const std::filesystem::path& source,
                    const std::filesystem::path& engineShaderRoot, GFX::ShaderCookOutput& out) {
    GFX::ShaderCookRequest request;
    request.projectShaderRoot = spec.directory;
    request.engineSdkIncludeRoot = engineShaderRoot;
    request.primarySourceRoot =
        source.empty() ? GFX::ShaderCookIncludeRoot::EngineSdk : GFX::ShaderCookIncludeRoot::Project;
    request.sourcePath = source.empty() ? std::filesystem::path(kStyleModelsSource) : source;
    request.entries = {{"PS", GFX::ShaderStage::Pixel, "PlaygroundPS", "ps_6_0"},
                       {"VS", GFX::ShaderStage::Vertex, "PlaygroundVS", "vs_6_0"}};
    request.includeRoots = {GFX::ShaderCookIncludeRoot::Project, GFX::ShaderCookIncludeRoot::EngineSdk};
    for (size_t index = 0u; index < spec.parameterNames.size(); ++index)
        request.defines.push_back(
            {"HIKARI_PLAY_PARAM_" + spec.parameterNames[index], std::to_string(spec.parameterBase + index)});
    request.engineInterface.id = "HIKARI.ShaderPlayground";
    request.engineInterface.version = 1u;
    if (!HASH::ComputeSha256("HIKARI.ShaderPlayground.v1", request.engineInterface.hash)) {
        out.diagnostics = "could not hash the playground interface";
        return false;
    }
    request.rootLayout = request.engineInterface;
    request.requiredCaps = {60u, 10u, 0u};
    return GFX::CompileShaderProgramForCook(request, out);
}

// 入力画像を sRGB の RGBA8 へ。無ければ 1x1 の黒（HLSL は gPlayHasImage が 0 なら試験図を使う）。
bool LoadImage(const Spec& spec, DirectX::ScratchImage& out, std::string& error) {
    if (spec.image.empty()) {
        return Check(out.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1u, 1u, 1u, 1u), "ScratchImage::Initialize2D",
                     error);
    }
    DirectX::ScratchImage loaded;
    const std::filesystem::path path = spec.directory / spec.image;
    if (!Check(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, loaded),
               ("loading " + TEXT::PathToUtf8(spec.image)).c_str(), error))
        return false;
    const DirectX::TexMetadata& meta = loaded.GetMetadata();
    if (meta.width > kMaximumImageSide || meta.height > kMaximumImageSide)
        return Check(E_INVALIDARG, "the input image must be at most 4096 pixels per side; image", error);
    if (meta.format == DXGI_FORMAT_R8G8B8A8_UNORM || meta.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        out = std::move(loaded);
    } else if (!Check(DirectX::Convert(*loaded.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM,
                                       DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, out),
                      "converting the input image", error)) {
        return false;
    }
    // PNG と JPEG の値は sRGB として読む（線形への変換はサンプラーが行う）。
    out.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    return true;
}

Globals MakeGlobals(const Spec& spec, uint32_t width, uint32_t height, bool hasImage) {
    Globals globals{};
    globals.sheetSize[0] = static_cast<float>(width);
    globals.sheetSize[1] = static_cast<float>(height);
    globals.tileSize[0] = globals.tileSize[1] = static_cast<float>(spec.tileSize);
    globals.columns = spec.columns;
    globals.tileCount = static_cast<uint32_t>(spec.tiles.size());
    globals.scene = static_cast<uint32_t>(spec.scene);
    globals.hasImage = hasImage ? 1u : 0u;
    globals.ambientColor[0] = spec.ambientColor.x;
    globals.ambientColor[1] = spec.ambientColor.y;
    globals.ambientColor[2] = spec.ambientColor.z;
    globals.gap = static_cast<float>(spec.gap);
    globals.curveRange[0] = spec.curveRange.x;
    globals.curveRange[1] = spec.curveRange.y;
    globals.exposure = spec.exposure;
    for (size_t tile = 0u; tile < spec.tiles.size(); ++tile)
        std::copy(spec.tiles[tile].slots.begin(), spec.tiles[tile].slots.end(), globals.tiles + tile * kTileSlots);
    return globals;
}

} // namespace

bool RenderSheet(const Spec& spec, const std::filesystem::path& engineShaderRoot, SheetRender& outSheet,
                 std::string& outError) {
    SheetRender sheet;
    const uint32_t tileCount = static_cast<uint32_t>(spec.tiles.size());
    sheet.rows = (tileCount + spec.columns - 1u) / spec.columns;
    sheet.width = spec.columns * spec.tileSize + (spec.columns - 1u) * spec.gap;
    sheet.height = sheet.rows * spec.tileSize + (sheet.rows - 1u) * spec.gap;
    if (tileCount == 0u || sheet.width > kMaximumSheetSide || sheet.height > kMaximumSheetSide)
        return Check(E_INVALIDARG, "the tile sheet must be 1 to 8192 pixels per side; sheet layout", outError);

    std::vector<std::pair<std::string, std::filesystem::path>> sources{{"a", spec.source}};
    if (!spec.compare.empty())
        sources.emplace_back("b", spec.compare);
    std::vector<GFX::ShaderCookOutput> compiled(sources.size());
    bool anyCompiled = false;
    for (size_t index = 0u; index < sources.size(); ++index) {
        ProgramRender program;
        program.id = sources[index].first;
        program.source = sources[index].second.empty() ? std::string("builtin:renderStyleShading")
                                                       : TEXT::PathToUtf8(sources[index].second);
        const auto started = std::chrono::steady_clock::now();
        program.compiled = CompileProgram(spec, sources[index].second, engineShaderRoot, compiled[index]);
        program.compileMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        for (const GFX::ShaderCookEntryOutput& entry : compiled[index].entries)
            program.dxilBytes += entry.dxil.size();
        program.diagnostics = compiled[index].diagnostics;
        anyCompiled = anyCompiled || program.compiled;
        sheet.programs.push_back(std::move(program));
    }
    if (!anyCompiled) {
        outSheet = std::move(sheet);
        outError.clear();
        return true;
    }

    Gpu gpu;
    DirectX::ScratchImage image;
    if (!gpu.Create(spec.device, outError) || !LoadImage(spec, image, outError))
        return false;
    sheet.adapter = gpu.adapter;
    sheet.software = gpu.software;
    ID3D12Device* device = gpu.device.Get();

    CD3DX12_DESCRIPTOR_RANGE imageRange;
    imageRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    std::array<CD3DX12_ROOT_PARAMETER, 2> parameters;
    parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    parameters[1].InitAsDescriptorTable(1, &imageRange, D3D12_SHADER_VISIBILITY_PIXEL);
    const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                              D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    const D3D12_ROOT_SIGNATURE_DESC signatureDesc{static_cast<UINT>(parameters.size()), parameters.data(), 1, &sampler};
    ComPtr<ID3DBlob> signature, signatureErrors;
    ComPtr<ID3D12RootSignature> root;
    if (!Check(D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &signatureErrors),
               "D3D12SerializeRootSignature", outError) ||
        !Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&root)),
               "CreateRootSignature", outError))
        return false;

    // 描画先 2 枚（表示と生の値）と読み戻し。
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC targetDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        kTargetFormat, sheet.width, sheet.height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    const D3D12_CLEAR_VALUE clear{kTargetFormat, {0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<ComPtr<ID3D12Resource>, 2> targets, readbacks;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readbackBytes = 0u;
    device->GetCopyableFootprints(&targetDesc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackBytes);
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2u};
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    if (!Check(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap (RTV)",
               outError))
        return false;
    const UINT rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvs{};
    for (size_t index = 0u; index < targets.size(); ++index) {
        if (!Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &targetDesc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                   IID_PPV_ARGS(&targets[index])),
                   "CreateCommittedResource (target)", outError) ||
            !gpu.Buffer(readbackBytes, D3D12_HEAP_TYPE_READBACK, readbacks[index], outError))
            return false;
        rtvs[index] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                                    static_cast<INT>(index), rtvStride);
        device->CreateRenderTargetView(targets[index].Get(), nullptr, rtvs[index]);
    }

    // 定数と入力画像。
    ComPtr<ID3D12Resource> constants, texture, textureUpload;
    const Globals globals = MakeGlobals(spec, sheet.width, sheet.height, !spec.image.empty());
    void* mapped = nullptr;
    if (!gpu.Buffer(kGlobalsBytes, D3D12_HEAP_TYPE_UPLOAD, constants, outError) ||
        !Check(constants->Map(0, nullptr, &mapped), "Map (constants)", outError))
        return false;
    std::memcpy(mapped, &globals, sizeof(globals));
    constants->Unmap(0, nullptr);
    const DirectX::Image& source = *image.GetImage(0, 0, 0);
    const CD3DX12_RESOURCE_DESC textureDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(source.format, source.width, static_cast<UINT>(source.height), 1, 1);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint{};
    UINT64 uploadBytes = 0u;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, &uploadBytes);
    if (!Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)),
               "CreateCommittedResource (image)", outError) ||
        !gpu.Buffer(uploadBytes, D3D12_HEAP_TYPE_UPLOAD, textureUpload, outError) ||
        !Check(textureUpload->Map(0, nullptr, &mapped), "Map (image)", outError))
        return false;
    for (size_t row = 0u; row < source.height; ++row)
        std::memcpy(static_cast<std::byte*>(mapped) + textureFootprint.Offset +
                        row * textureFootprint.Footprint.RowPitch,
                    source.pixels + row * source.rowPitch, source.width * 4u);
    textureUpload->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1u,
                                           D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    if (!Check(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)), "CreateDescriptorHeap (SRV)",
               outError))
        return false;
    device->CreateShaderResourceView(texture.Get(), nullptr, srvHeap->GetCPUDescriptorHandleForHeapStart());

    // 時間はハードウェアのときだけ測る（WARP の時間は着色器の重さを表さない）。
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> queryReadback;
    UINT64 timestampFrequency = 0u;
    if (!gpu.software) {
        const D3D12_QUERY_HEAP_DESC queryDesc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2u, 0u};
        if (!Check(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queries)), "CreateQueryHeap", outError) ||
            !gpu.Buffer(2u * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, queryReadback, outError) ||
            !Check(gpu.queue->GetTimestampFrequency(&timestampFrequency), "GetTimestampFrequency", outError))
            return false;
    }

    if (!gpu.Begin(outError))
        return false;
    const CD3DX12_TEXTURE_COPY_LOCATION textureDestination(texture.Get(), 0);
    const CD3DX12_TEXTURE_COPY_LOCATION textureSource(textureUpload.Get(), textureFootprint);
    gpu.commands->CopyTextureRegion(&textureDestination, 0, 0, 0, &textureSource, nullptr);
    const CD3DX12_RESOURCE_BARRIER textureReady = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.commands->ResourceBarrier(1, &textureReady);
    if (!gpu.SubmitAndWait(outError))
        return false;

    const size_t floats = static_cast<size_t>(sheet.width) * sheet.height * 4u;
    for (size_t index = 0u; index < sheet.programs.size(); ++index) {
        ProgramRender& program = sheet.programs[index];
        if (!program.compiled)
            continue;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        for (const GFX::ShaderCookEntryOutput& entry : compiled[index].entries) {
            const D3D12_SHADER_BYTECODE bytecode{entry.dxil.data(), entry.dxil.size()};
            if (entry.id == "VS")
                desc.VS = bytecode;
            else
                desc.PS = bytecode;
        }
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 2;
        desc.RTVFormats[0] = desc.RTVFormats[1] = kTargetFormat;
        desc.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pipeline;
        const HRESULT pipelineResult = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
        if (FAILED(pipelineResult)) {
            program.compiled = false;
            Check(pipelineResult, "CreateGraphicsPipelineState", program.diagnostics);
            continue;
        }

        if (!gpu.Begin(outError))
            return false;
        ID3D12GraphicsCommandList* commands = gpu.commands.Get();
        commands->SetGraphicsRootSignature(root.Get());
        commands->SetPipelineState(pipeline.Get());
        ID3D12DescriptorHeap* heaps[]{srvHeap.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
        commands->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(sheet.width), static_cast<float>(sheet.height),
                                      0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(sheet.width), static_cast<LONG>(sheet.height)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->OMSetRenderTargets(2, rtvs.data(), FALSE, nullptr);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (const D3D12_CPU_DESCRIPTOR_HANDLE rtv : rtvs)
            commands->ClearRenderTargetView(rtv, clear.Color, 0, nullptr);
        commands->DrawInstanced(3, 1, 0, 0);
        if (queries != nullptr) {
            commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            for (uint32_t draw = 0u; draw < kTimedDraws; ++draw)
                commands->DrawInstanced(3, 1, 0, 0);
            commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, queryReadback.Get(), 0);
        }
        for (size_t target = 0u; target < targets.size(); ++target) {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                targets[target].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->ResourceBarrier(1, &barrier);
            const CD3DX12_TEXTURE_COPY_LOCATION destination(readbacks[target].Get(), footprint);
            const CD3DX12_TEXTURE_COPY_LOCATION copySource(targets[target].Get(), 0);
            commands->CopyTextureRegion(&destination, 0, 0, 0, &copySource, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commands->ResourceBarrier(1, &barrier);
        }
        if (!gpu.SubmitAndWait(outError))
            return false;

        std::array<std::vector<float>*, 2> outputs{&program.display, &program.raw};
        for (size_t target = 0u; target < targets.size(); ++target) {
            const D3D12_RANGE range{0, static_cast<SIZE_T>(readbackBytes)};
            if (!Check(readbacks[target]->Map(0, &range, &mapped), "Map (readback)", outError))
                return false;
            outputs[target]->resize(floats);
            for (uint32_t row = 0u; row < sheet.height; ++row)
                std::memcpy(outputs[target]->data() + static_cast<size_t>(row) * sheet.width * 4u,
                            static_cast<const std::byte*>(mapped) + footprint.Offset +
                                static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                            static_cast<size_t>(sheet.width) * 4u * sizeof(float));
            const D3D12_RANGE written{0, 0};
            readbacks[target]->Unmap(0, &written);
        }
        if (queries != nullptr && timestampFrequency > 0u && SUCCEEDED(queryReadback->Map(0, nullptr, &mapped))) {
            UINT64 stamps[2]{};
            std::memcpy(stamps, mapped, sizeof(stamps));
            const D3D12_RANGE written{0, 0};
            queryReadback->Unmap(0, &written);
            if (stamps[1] > stamps[0])
                program.gpuMilliseconds = static_cast<double>(stamps[1] - stamps[0]) * 1000.0 /
                                          static_cast<double>(timestampFrequency) / kTimedDraws;
        }
    }
    outSheet = std::move(sheet);
    outError.clear();
    return true;
}

} // namespace HIKARI::EDITOR::PLAYGROUND
