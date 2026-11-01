#include "Render3D/MotionBlur/HIKARI_CameraMotionBlurStage.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "RenderContracts/Shader/HIKARI_CameraMotionBlurShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::MOTIONBLUR {
namespace {
using Microsoft::WRL::ComPtr;
// shader の HIKARI_MOTION_BLUR_TILE と同じ。近傍 3x3 のタイルで拾えるのはタイル一枚分の半径までなので、
// ブラーの長さはタイル二枚分で切る。
constexpr uint32_t kTilePixels = 32u;
constexpr DXGI_FORMAT kTileFormat = DXGI_FORMAT_R16G16_FLOAT;
constexpr D3D12_RESOURCE_STATES kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
enum Root : uint32_t {
    kConstants = 0u,
    kSceneColor,
    kMotion,
    kDepth,
    kTileMax,
    kNeighborMax,
    kTileMaxOut,
    kNeighborMaxOut,
    kOutput,
    kRootCount,
};

struct Constants {
    float sizes[4];
    float blur[4];
    float depthTiles[4];
};
static_assert(sizeof(Constants) == 48u);

struct TileTexture {
    ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    RenderResourceView uav{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

struct StageState {
    ID3D12Device* device = nullptr;
    uint64_t deviceGeneration = 0u;
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> tileMaxPipeline{};
    ComPtr<ID3D12PipelineState> neighborMaxPipeline{};
    ComPtr<ID3D12PipelineState> gatherPipeline{};
    bool pipelineFailed = false;
    TileTexture tileMax{};
    TileTexture neighborMax{};
    uint32_t tilesX = 0u;
    uint32_t tilesY = 0u;
    RenderTarget2D output{};
    RenderResourceView outputUav{};
};

StageState& State() {
    static StageState state{};
    return state;
}

void ReleaseTile(TileTexture& tile, const GFX::Context& context) {
    RetireRenderResourceDescriptor(tile.srv, context, "CameraMotionBlur.TileSrv");
    RetireRenderResourceDescriptor(tile.uav, context, "CameraMotionBlur.TileUav");
    if (tile.resource) {
        GFX::RetireD3D12ObjectForFrame(tile.resource, context, "CameraMotionBlur.Tile");
    }
    tile.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void ReleaseTargets(StageState& state, const GFX::Context& context) {
    ReleaseTile(state.tileMax, context);
    ReleaseTile(state.neighborMax, context);
    state.tilesX = state.tilesY = 0u;
    RetireRenderResourceDescriptor(state.outputUav, context, "CameraMotionBlur.OutputUav");
    state.output.Finalize();
}

void ReleaseAll(StageState& state, const GFX::Context& context) {
    ReleaseTargets(state, context);
    for (ComPtr<ID3D12PipelineState>* pipeline :
         {std::addressof(state.tileMaxPipeline), std::addressof(state.neighborMaxPipeline),
          std::addressof(state.gatherPipeline)}) {
        if (*pipeline) {
            GFX::RetireD3D12ObjectForFrame(*pipeline, context, "CameraMotionBlur.Pipeline");
        }
    }
    if (state.rootSignature) {
        GFX::RetireD3D12ObjectForFrame(state.rootSignature, context, "CameraMotionBlur.RootSignature");
    }
    state.pipelineFailed = false;
}

bool CreateRootSignature(ID3D12Device* device, ID3D12RootSignature** output) {
    std::array<D3D12_DESCRIPTOR_RANGE, kRootCount - 1u> ranges{};
    std::array<D3D12_ROOT_PARAMETER, kRootCount> parameters{};
    parameters[kConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[kConstants].Constants = {0u, 0u, sizeof(Constants) / 4u};
    for (uint32_t index = 1u; index < kRootCount; ++index) {
        D3D12_DESCRIPTOR_RANGE& range = ranges[index - 1u];
        const bool srv = index <= kNeighborMax;
        range.RangeType = srv ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1u;
        range.BaseShaderRegister = srv ? index - kSceneColor : index - kTileMaxOut;
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index].DescriptorTable = {1u, &range};
    }
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1u;
    description.pStaticSamplers = &sampler;
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, description, output),
                           "CameraMotionBlur::CreateRootSignature");
}

bool EnsurePipelines(StageState& state, ID3D12Device* device) {
    if (state.gatherPipeline) {
        return true;
    }
    if (state.pipelineFailed) {
        return false;
    }
    std::string diagnostic;
    if (!CreateRootSignature(device, state.rootSignature.ReleaseAndGetAddressOf())) {
        diagnostic = "root signature creation failed";
    }
    const GFX::ShaderDeviceCapabilities deviceCaps = GFX::QueryShaderDeviceCapabilities(device);
    const ASSETS::SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion,
                                                               deviceCaps.featureMask};
    const std::array<std::pair<const char*, ComPtr<ID3D12PipelineState>*>, 3u> entries{{
        {"TileMaxCS", std::addressof(state.tileMaxPipeline)},
        {"NeighborMaxCS", std::addressof(state.neighborMaxPipeline)},
        {"GatherCS", std::addressof(state.gatherPipeline)},
    }};
    for (const auto& [entry, pipeline] : entries) {
        if (!diagnostic.empty()) {
            break;
        }
        auto resolved = ASSETS::SHADER::ResolveCameraMotionBlurShaderEntry(entry, caps);
        if (!resolved) {
            diagnostic = std::string(entry) + ": " + resolved.diagnostic;
            break;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = state.rootSignature.Get();
        description.CS = {resolved.lease.GetDxil().data(), resolved.lease.GetDxil().size()};
        if (!HIKARI_DX_CHECK(
                device->CreateComputePipelineState(&description, IID_PPV_ARGS(pipeline->ReleaseAndGetAddressOf())),
                "CameraMotionBlur::CreatePipeline")) {
            diagnostic = std::string(entry) + ": pipeline creation failed";
        }
    }
    if (!diagnostic.empty()) {
        HIKARI_LOG_WARN("[CameraMotionBlur] Shader pipeline unavailable: " + diagnostic);
        state.pipelineFailed = true;
        state.gatherPipeline.Reset();
        return false;
    }
    return true;
}

bool CreateTile(ID3D12Device* device, TileTexture& tile, uint32_t width, uint32_t height) {
    const auto description = CD3DX12_RESOURCE_DESC::Tex2D(kTileFormat, width, height, 1u, 1u, 1u, 0u,
                                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    if (!HIKARI_DX_CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                         IID_PPV_ARGS(tile.resource.ReleaseAndGetAddressOf())),
                         "CameraMotionBlur::CreateTile")) {
        return false;
    }
    tile.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    tile.srv = AllocateTexture2DSrvDescriptor(tile.resource.Get(), kTileFormat);
    tile.uav = AllocateTexture2DUavDescriptor(tile.resource.Get(), kTileFormat);
    return tile.srv.IsValid() && tile.uav.IsValid();
}

bool EnsureTargets(StageState& state, const GFX::Context& context, const RenderTarget2D& sceneColor) {
    const uint32_t width = static_cast<uint32_t>(sceneColor.GetWidth());
    const uint32_t height = static_cast<uint32_t>(sceneColor.GetHeight());
    const uint32_t tilesX = (width + kTilePixels - 1u) / kTilePixels;
    const uint32_t tilesY = (height + kTilePixels - 1u) / kTilePixels;
    // 状態遷移はこの目標が覚えている命令列へ記録されるので、毎フレーム今の文脈を渡す。
    state.output.UpdateContext(context);
    if (state.output.GetResource() != nullptr && state.output.GetWidth() == sceneColor.GetWidth() &&
        state.output.GetHeight() == sceneColor.GetHeight() && state.output.GetFormat() == sceneColor.GetFormat() &&
        state.tilesX == tilesX && state.tilesY == tilesY) {
        return true;
    }
    ReleaseTargets(state, context);
    state.output.SetDebugName("CameraMotionBlur.Output");
    if (!state.output.Init(sceneColor.GetWidth(), sceneColor.GetHeight(), sceneColor.GetFormat(), false,
                           {0.0f, 0.0f, 0.0f, 0.0f}, false, true)) {
        return false;
    }
    state.outputUav = AllocateTexture2DUavDescriptor(state.output.GetResource(), sceneColor.GetFormat());
    if (!state.outputUav.IsValid() || !CreateTile(context.device, state.tileMax, tilesX, tilesY) ||
        !CreateTile(context.device, state.neighborMax, tilesX, tilesY)) {
        ReleaseTargets(state, context);
        return false;
    }
    state.tilesX = tilesX;
    state.tilesY = tilesY;
    return true;
}

void TransitionTile(ID3D12GraphicsCommandList* commandList, TileTexture& tile, D3D12_RESOURCE_STATES next) {
    if (tile.state == next) {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(tile.resource.Get());
        commandList->ResourceBarrier(1u, &barrier);
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tile.resource.Get(), tile.state, next);
    commandList->ResourceBarrier(1u, &barrier);
    tile.state = next;
}

uint32_t ResolveSampleCount(MotionBlurQuality quality) noexcept {
    switch (quality) {
    case MotionBlurQuality::Low: return 8u;
    case MotionBlurQuality::Medium: return 12u;
    case MotionBlurQuality::High: return 16u;
    case MotionBlurQuality::Off: break;
    }
    return 0u;
}

bool IsCameraMotionBlurActive(const CAMERA::MotionBlurSettings& settings, MotionBlurQuality quality) noexcept {
    return settings.enabled && settings.shutterAngleDegrees > 0.0f && settings.maxBlur1080pPixels > 0.0f &&
           ResolveSampleCount(quality) > 0u;
}

// 色と深度の SRV をフレームごとに作り、フレームの終わりに返す（運動ベクトルは時域資源の SRV をそのまま使う）。
class FrameInputs {
public:
    FrameInputs(const GFX::Context& context, const CameraMotionBlurInputs& inputs)
        : context_(context), views_(AllocateRenderResourceDescriptorRange(2u)) {
        if (views_.size() != 2u) {
            Release();
            return;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1u;
        view.Format = inputs.sceneColor->GetFormat();
        context.device->CreateShaderResourceView(inputs.sceneColor->GetResource(), &view, views_[0].cpu);
        view.Format = DXGI_FORMAT_R32_FLOAT;
        context.device->CreateShaderResourceView(inputs.sceneDepthOwner->GetDepthResource(), &view, views_[1].cpu);
    }
    ~FrameInputs() { Release(); }
    FrameInputs(const FrameInputs&) = delete;
    FrameInputs& operator=(const FrameInputs&) = delete;

    explicit operator bool() const noexcept { return views_.size() == 2u; }
    D3D12_GPU_DESCRIPTOR_HANDLE operator[](size_t index) const noexcept { return views_[index].gpu; }

private:
    void Release() noexcept {
        for (RenderResourceView& view : views_) {
            RetireRenderResourceDescriptor(view, context_, "CameraMotionBlur.FrameInputs");
        }
        views_.clear();
    }

    GFX::Context context_{};
    std::vector<RenderResourceView> views_{};
};

} // namespace

RenderTarget2D* ExecuteCameraMotionBlur(const GFX::Context& context, const CameraMotionBlurInputs& inputs) {
    RenderTarget2D* source = inputs.sceneColor;
    if (source == nullptr || !IsCameraMotionBlurActive(inputs.settings, inputs.quality) ||
        !inputs.motionVectors.IsSampleable() || inputs.motionVectors.resource == nullptr ||
        inputs.sceneDepthOwner == nullptr || inputs.sceneDepthOwner->GetDepthResource() == nullptr ||
        inputs.temporalFrame == nullptr || !inputs.temporalFrame->camera.valid || source->GetResource() == nullptr ||
        context.device == nullptr || context.cmdList == nullptr || context.srvHeap == nullptr) {
        return source;
    }
    StageState& state = State();
    if (state.device != context.device || state.deviceGeneration != context.deviceGeneration) {
        ReleaseAll(state, context);
        state.device = context.device;
        state.deviceGeneration = context.deviceGeneration;
    }
    if (!EnsurePipelines(state, context.device) || !EnsureTargets(state, context, *source)) {
        return source;
    }
    const FrameInputs frameInputs(context, inputs);
    if (!frameInputs || !inputs.sceneDepthOwner->BeginDepthShaderRead()) {
        return source;
    }

    const TEMPORAL::TemporalFrameState& frame = *inputs.temporalFrame;
    const float outputHeight = static_cast<float>(source->GetHeight());
    const float maxLength =
        (std::min)(inputs.settings.maxBlur1080pPixels * outputHeight / 1080.0f, static_cast<float>(kTilePixels * 2u));
    Constants constants{};
    constants.sizes[0] = static_cast<float>(source->GetWidth());
    constants.sizes[1] = outputHeight;
    constants.sizes[2] = static_cast<float>(inputs.motionVectors.width);
    constants.sizes[3] = static_cast<float>(inputs.motionVectors.height);
    constants.blur[0] = inputs.settings.shutterAngleDegrees / 360.0f;
    constants.blur[1] = maxLength;
    constants.blur[2] = static_cast<float>(ResolveSampleCount(inputs.quality));
    constants.blur[3] = inputs.orthographic ? 1.0f : 0.0f;
    constants.depthTiles[0] = frame.camera.nearZ;
    constants.depthTiles[1] = frame.camera.farZ;
    constants.depthTiles[2] = static_cast<float>(state.tilesX);
    constants.depthTiles[3] = static_cast<float>(state.tilesY);

    ID3D12GraphicsCommandList* commandList = context.cmdList;
    GFX::PIX::ScopedGpuEvent event(commandList, GFX::PIX::kColorPost, "CameraMotionBlur");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commandList, GFX::GPU_PROFILE::Pass::MotionBlur);
    const D3D12_RESOURCE_STATES sourceState = source->GetColorState();
    const D3D12_RESOURCE_STATES motionState = inputs.motionVectors.state;
    // 読み取りの状態なら計算 shader の読み取りを足し、書き込みの状態なら読み取りへ移す。
    const D3D12_RESOURCE_STATES motionRead =
        (motionState & D3D12_RESOURCE_STATE_GENERIC_READ) == motionState ? motionState | kShaderRead : kShaderRead;
    source->TransitionColor(kShaderRead);
    if (motionRead != motionState) {
        const auto barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(inputs.motionVectors.resource, motionState, motionRead);
        commandList->ResourceBarrier(1u, &barrier);
    }
    state.output.TransitionColor(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heap = context.srvHeap;
    commandList->SetDescriptorHeaps(1u, &heap);
    commandList->SetComputeRootSignature(state.rootSignature.Get());
    commandList->SetComputeRoot32BitConstants(kConstants, sizeof(Constants) / 4u, &constants, 0u);
    commandList->SetComputeRootDescriptorTable(kSceneColor, frameInputs[0]);
    commandList->SetComputeRootDescriptorTable(kMotion, inputs.motionVectors.srv);
    commandList->SetComputeRootDescriptorTable(kDepth, frameInputs[1]);
    commandList->SetComputeRootDescriptorTable(kTileMax, state.tileMax.srv.gpu);
    commandList->SetComputeRootDescriptorTable(kNeighborMax, state.neighborMax.srv.gpu);
    commandList->SetComputeRootDescriptorTable(kTileMaxOut, state.tileMax.uav.gpu);
    commandList->SetComputeRootDescriptorTable(kNeighborMaxOut, state.neighborMax.uav.gpu);
    commandList->SetComputeRootDescriptorTable(kOutput, state.outputUav.gpu);

    TransitionTile(commandList, state.tileMax, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(state.tileMaxPipeline.Get());
    commandList->Dispatch(state.tilesX, state.tilesY, 1u);
    TransitionTile(commandList, state.tileMax, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionTile(commandList, state.neighborMax, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(state.neighborMaxPipeline.Get());
    commandList->Dispatch((state.tilesX + 7u) / 8u, (state.tilesY + 7u) / 8u, 1u);
    TransitionTile(commandList, state.neighborMax, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList->SetPipelineState(state.gatherPipeline.Get());
    commandList->Dispatch((static_cast<uint32_t>(source->GetWidth()) + 7u) / 8u,
                          (static_cast<uint32_t>(source->GetHeight()) + 7u) / 8u, 1u);

    state.output.TransitionColor(kShaderRead);
    if (motionRead != motionState) {
        const auto barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(inputs.motionVectors.resource, motionRead, motionState);
        commandList->ResourceBarrier(1u, &barrier);
    }
    source->TransitionColor(sourceState);
    inputs.sceneDepthOwner->EndDepthShaderRead();
    return &state.output;
}

void ShutdownCameraMotionBlurStage() noexcept {
    StageState& state = State();
    ReleaseAll(state, GFX::GetCurrentContext());
    state.device = nullptr;
    state.deviceGeneration = 0u;
}

} // namespace HIKARI::RENDER3D::MOTIONBLUR
