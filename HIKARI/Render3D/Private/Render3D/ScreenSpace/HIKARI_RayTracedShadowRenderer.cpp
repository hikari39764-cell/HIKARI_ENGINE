#include "Render3D/ScreenSpace/HIKARI_RayTracedShadowRenderer.h"

#include <algorithm>
#include <cstring>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayQueryBindings.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::SCREENSPACE {
namespace {

struct alignas(16) RayTracedShadowCB {
    MATH::Mat4 invViewProj{};
    MATH::Vec4 cameraPosition{};
    MATH::Vec4 screenParams{};
    MATH::Vec4 sunCone{};
    MATH::Vec4 rayParams{};
    MATH::Vec4 inputFlags{};
    RAYTRACING::RuntimeRayLightingGpuData lighting{};
    MATH::Mat4 previousViewProj{};
    MATH::Vec4 previousCamera{};
};
static_assert(sizeof(RayTracedShadowCB) == 384);

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8_UNORM;
constexpr DXGI_FORMAT kHistoryFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr size_t kUploadBytes = 4096u;
constexpr const wchar_t* kShaderPath = L"HIKARI/Shaders/Post_RayTracedShadowCS.hlsl";
// 共通の 0..9 + 空(10) の後：深度(11)・面(12)・可視率の UAV(13)・フィルター入力の生の可視率(14)。
constexpr UINT kSkyParameter = RAYTRACING::kRuntimeRayQueryRootParameterCount;
constexpr UINT kDepthParameter = kSkyParameter + 1u;
constexpr UINT kSurfaceParameter = kSkyParameter + 2u;
constexpr UINT kOutputParameter = kSkyParameter + 3u;
constexpr UINT kFilterInputParameter = kSkyParameter + 4u;
constexpr UINT kHistoryParameter = kFilterInputParameter + 1u;
constexpr UINT kFilterOutputParameter = kHistoryParameter + 1u;

} // namespace

bool RayTracedShadowRenderer::Render(const GFX::Context& graphics, const RayTracedShadowInputs& inputs) {
    valid_ = false;
    auto* commands = graphics.cmdList;
    if (commands == nullptr || graphics.device == nullptr || graphics.srvHeap == nullptr || inputs.camera == nullptr ||
        inputs.rayLighting == nullptr || inputs.materials == nullptr || !*inputs.materials ||
        !inputs.sceneDepth.IsSampleable() ||
        inputs.rayLighting->status != RAYTRACING::RuntimeRayLightingStatus::Ready || !inputs.rayLighting->scene.ready ||
        !RAYTRACING::CanTraceShadows(inputs.rayLighting->scene.excludedShadowCasters) ||
        inputs.rayLighting->scene.accelerationStructure == 0) {
        return false;
    }
    if (!EnsurePipeline(graphics) || !EnsureTargets(graphics, inputs.sceneDepth.width, inputs.sceneDepth.height)) {
        return false;
    }
    auto& upload = uploads_[graphics.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() && !upload.Init(graphics.device, kUploadBytes)) {
        return false;
    }
    upload.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const auto slice = upload.Allocate(sizeof(RayTracedShadowCB));
    if (!slice) {
        return false;
    }
    const bool surfaceValid = inputs.packedSurface.IsSampleable() && inputs.packedSurface.width == width_ &&
                              inputs.packedSurface.height == height_;
    RayTracedShadowCB constants{};
    constants.invViewProj = inputs.camera->invViewProj;
    constants.cameraPosition = inputs.camera->cameraPos;
    constants.screenParams = {static_cast<float>(width_), static_cast<float>(height_), 1.0f / float(width_),
                              1.0f / float(height_)};
    const MATH::Vec3 toSun = MATH::Normalize(inputs.directionToSun);
    constants.sunCone = {toSun.x, toSun.y, toSun.z, std::clamp(inputs.coneHalfAngle, 0.0f, 0.5f)};
    constants.rayParams = {std::max(inputs.maxDistance, 0.1f),
                           static_cast<float>(std::clamp(inputs.sampleCount, 1u, 8u)),
                           static_cast<float>(inputs.frameIndex % 1024u), std::max(inputs.normalOffset, 0.0f)};
    const auto* temporal = inputs.temporal;
    const bool historyValid = temporal && temporal->camera.previousValid && !temporal->resetHistory &&
                              historyView_.Matches(temporal->view) && historyFrame_ + 1 == temporal->frameIndex;
    constants.inputFlags = {surfaceValid ? 1.0f : 0.0f, historyValid ? 1.0f : 0.0f, 0.0f, 0.0f};
    if (temporal) {
        constants.previousViewProj = temporal->camera.prevViewProj;
        constants.previousCamera = temporal->camera.prevCameraPos;
    }
    constants.lighting = inputs.rayLighting->data;
    std::memcpy(slice.cpuAddress, &constants, sizeof(constants));

    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorPost, "RayTracedShadow");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::RayTracedShadow);
    ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(rootSignature_.Get());
    commands->SetComputeRootConstantBufferView(0, slice.gpuAddress);
    RAYTRACING::BindRuntimeRayQueryRoots(commands, graphics, *inputs.rayLighting, *inputs.materials, kSkyParameter);
    commands->SetComputeRootDescriptorTable(kDepthParameter, inputs.sceneDepth.srv);
    commands->SetComputeRootDescriptorTable(kSurfaceParameter,
                                            surfaceValid ? inputs.packedSurface.srv : inputs.sceneDepth.srv);
    const UINT groupsX = (width_ + 7u) / 8u;
    const UINT groupsY = (height_ + 7u) / 8u;
    // 1) 影光線を飛ばして生の可視率を書く。
    Transition(commands, raw_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetPipelineState(tracePipeline_.Get());
    commands->SetComputeRootDescriptorTable(kFilterInputParameter, inputs.sceneDepth.srv);
    commands->SetComputeRootDescriptorTable(kOutputParameter, raw_.uav.gpu);
    commands->Dispatch(groupsX, groupsY, 1u);
    // 2) 同じ面の近傍で平均してフィルター済みの面へ。
    Transition(commands, raw_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto& filtered = filtered_[writeIndex_];
    auto& history = filtered_[writeIndex_ ^ 1u];
    Transition(commands, filtered, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commands, history, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->SetPipelineState(filterPipeline_.Get());
    commands->SetComputeRootDescriptorTable(kFilterInputParameter, raw_.srv.gpu);
    commands->SetComputeRootDescriptorTable(kFilterOutputParameter, filtered.uav.gpu);
    commands->SetComputeRootDescriptorTable(kHistoryParameter, history.srv.gpu);
    commands->Dispatch(groupsX, groupsY, 1u);
    Transition(commands, filtered, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (temporal) {
        historyFrame_ = temporal->frameIndex;
        historyView_ = temporal->view;
    } else {
        historyView_ = {};
    }
    writeIndex_ ^= 1u;
    valid_ = true;
    return true;
}

RenderTextureView RayTracedShadowRenderer::GetVisibilityView() const noexcept {
    const auto& filtered = filtered_[writeIndex_ ^ 1u];
    return {filtered.resource.Get(),
            filtered.srv.gpu,
            kHistoryFormat,
            width_,
            height_,
            1u,
            filtered.state,
            valid_ && filtered.resource != nullptr};
}

bool RayTracedShadowRenderer::EnsurePipeline(const GFX::Context& graphics) {
    if (rootSignature_ && tracePipeline_ && filterPipeline_ && deviceGeneration_ == graphics.deviceGeneration) {
        return true;
    }
    if (pipelineFailed_ && deviceGeneration_ == graphics.deviceGeneration) {
        return false;
    }
    rootSignature_.Reset();
    tracePipeline_.Reset();
    filterPipeline_.Reset();
    deviceGeneration_ = graphics.deviceGeneration;
    pipelineFailed_ = true;
    ID3D12Device* device = graphics.device;
    if (device == nullptr || !GFX::SupportsShaderModel6(device)) {
        return false;
    }
    std::array<CD3DX12_ROOT_PARAMETER, kFilterOutputParameter + 1u> parameters;
    std::array<CD3DX12_DESCRIPTOR_RANGE, RAYTRACING::kRuntimeRayQueryRangeCount + 6u> ranges;
    RAYTRACING::InitRuntimeRayQueryRootParameters(parameters, ranges, kSkyParameter);
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 9);
    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 9);
    ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 9);
    ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 9);
    parameters[kDepthParameter].InitAsDescriptorTable(1, &ranges[3]);
    parameters[kSurfaceParameter].InitAsDescriptorTable(1, &ranges[4]);
    parameters[kOutputParameter].InitAsDescriptorTable(1, &ranges[5]);
    parameters[kFilterInputParameter].InitAsDescriptorTable(1, &ranges[6]);
    ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 9);
    ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 9);
    parameters[kHistoryParameter].InitAsDescriptorTable(1, &ranges[7]);
    parameters[kFilterOutputParameter].InitAsDescriptorTable(1, &ranges[8]);
    const auto common = RAYTRACING::RuntimeRayQueryStaticSamplers();
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 3> samplers{common[0], common[1], {}};
    samplers[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                     D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data(), UINT(samplers.size()),
                                               samplers.data());
    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(device, rootDesc, rootSignature_.GetAddressOf()),
                         "RayTracedShadow root signature")) {
        return false;
    }
    const struct {
        const char* entry;
        Microsoft::WRL::ComPtr<ID3D12PipelineState>* pipeline;
        const char* label;
    } stages[]{{"CSMain", &tracePipeline_, "RayTracedShadow trace pipeline"},
               {"CSFilter", &filterPipeline_, "RayTracedShadow filter pipeline"}};
    for (const auto& stage : stages) {
        Microsoft::WRL::ComPtr<ID3DBlob> shader;
        if (!GFX::CompileShaderFileSm6(kShaderPath, stage.entry, L"cs_6_5", shader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[RayTracedShadow][ERROR] compute shader compile failed; cascade shadows retained.");
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature_.Get();
        desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
        if (!HIKARI_DX_CHECK(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(stage.pipeline->GetAddressOf())),
                             stage.label)) {
            return false;
        }
    }
    pipelineFailed_ = false;
    return true;
}

bool RayTracedShadowRenderer::EnsureTargets(const GFX::Context& graphics, uint32_t width, uint32_t height) {
    const auto ready = [](const Target& target) {
        return target.resource && target.srv.IsValid() && target.uav.IsValid();
    };
    if (width_ == width && height_ == height && ready(raw_) && ready(filtered_[0]) && ready(filtered_[1])) {
        return true;
    }
    ReleaseTargets(graphics);
    if (!CreateTarget(graphics, raw_, width, height, kFormat, L"HIKARI.RayTracedShadow.Visibility") ||
        !CreateTarget(graphics, filtered_[0], width, height, kHistoryFormat, L"HIKARI.RayTracedShadow.History0") ||
        !CreateTarget(graphics, filtered_[1], width, height, kHistoryFormat, L"HIKARI.RayTracedShadow.History1")) {
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool RayTracedShadowRenderer::CreateTarget(const GFX::Context& graphics, Target& target, uint32_t width,
                                           uint32_t height, DXGI_FORMAT format, const wchar_t* name) {
    ID3D12Device* device = graphics.device;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc =
        CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    target.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             device, &heap, D3D12_HEAP_FLAG_NONE, &desc, target.state, nullptr,
                             GFX::VIDEO_MEMORY::Owner::Shadow, IID_PPV_ARGS(target.resource.GetAddressOf())),
                         "RayTracedShadow visibility target")) {
        return false;
    }
    target.resource->SetName(name);
    target.srv = AllocateTexture2DSrvDescriptor(target.resource.Get(), format);
    target.uav = AllocateTexture2DUavDescriptor(target.resource.Get(), format);
    return target.srv.IsValid() && target.uav.IsValid();
}

void RayTracedShadowRenderer::ReleaseTargets(const GFX::Context& graphics) {
    ReleaseTarget(graphics, raw_);
    for (auto& filtered : filtered_)
        ReleaseTarget(graphics, filtered);
    historyView_ = {};
    historyFrame_ = UINT64_MAX;
    writeIndex_ = 0;
    width_ = 0u;
    height_ = 0u;
    valid_ = false;
}

void RayTracedShadowRenderer::ReleaseTarget(const GFX::Context& graphics, Target& target) {
    if (target.srv.IsValid()) {
        RetireRenderResourceDescriptor(target.srv, graphics, "RayTracedShadow.SRV");
    }
    if (target.uav.IsValid()) {
        RetireRenderResourceDescriptor(target.uav, graphics, "RayTracedShadow.UAV");
    }
    if (target.resource) {
        GFX::RetireD3D12ObjectForFrame(target.resource, graphics, "RayTracedShadow.Visibility");
    }
    target = {};
}

void RayTracedShadowRenderer::Transition(ID3D12GraphicsCommandList* commands, Target& target,
                                         D3D12_RESOURCE_STATES next) {
    if (target.state == next || !target.resource) {
        target.state = next;
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(), target.state, next);
    commands->ResourceBarrier(1, &barrier);
    target.state = next;
}

void RayTracedShadowRenderer::Release() {
    ReleaseTargets(GFX::GetCurrentContext());
    for (auto& upload : uploads_) {
        upload.Finalize();
    }
    rootSignature_.Reset();
    tracePipeline_.Reset();
    filterPipeline_.Reset();
    deviceGeneration_ = 0u;
    pipelineFailed_ = false;
}

} // namespace HIKARI::RENDER3D::SCREENSPACE
