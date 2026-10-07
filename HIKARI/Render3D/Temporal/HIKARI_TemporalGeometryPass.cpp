#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Core/HIKARI_MathConfig.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalShader.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalPassScratch.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalResourceSystemInternal.h"

namespace HIKARI::RENDER3D::TEMPORAL {

namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kMaxJointCount = 128u;

D3D12_RECT ProjectHistoryRejectionBounds(const MATH::Vec4& bounds, const MATH::Mat4& viewProj, LONG width,
                                         LONG height) {
    const D3D12_RECT viewport{0, 0, width, height};
    if (!MATH::IsFinite(bounds))
        return viewport;
    float minimumX = static_cast<float>(width), minimumY = static_cast<float>(height);
    float maximumX = 0.0f, maximumY = 0.0f;
    bool front = false, behind = false;
    const float radius = std::abs(bounds.w);
    for (uint32_t corner = 0u; corner < 8u; ++corner) {
        const auto clip = viewProj.TransformPoint({bounds.x + ((corner & 1u) ? radius : -radius),
                                                   bounds.y + ((corner & 2u) ? radius : -radius),
                                                   bounds.z + ((corner & 4u) ? radius : -radius), 1.0f});
        if (!MATH::IsFinite(clip))
            return viewport;
        if (clip.w <= 1.0e-4f) {
            behind = true;
            continue;
        }
        front = true;
        const float x = std::clamp(clip.x / clip.w * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(width);
        const float y = std::clamp(-clip.y / clip.w * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(height);
        minimumX = (std::min)(minimumX, x);
        minimumY = (std::min)(minimumY, y);
        maximumX = (std::max)(maximumX, x);
        maximumY = (std::max)(maximumY, y);
    }
    if (!front)
        return {};
    if (behind)
        return viewport;
    return {static_cast<LONG>(std::floor(minimumX)), static_cast<LONG>(std::floor(minimumY)),
            static_cast<LONG>(std::ceil(maximumX)), static_cast<LONG>(std::ceil(maximumY))};
}

struct GeometryConstants {
    MATH::Mat4 renderViewProj{};
    MATH::Mat4 currentViewProj{};
    MATH::Mat4 previousViewProj{};
    MATH::Mat4 currentWorld{};
    MATH::Mat4 previousWorld{};
    MATH::Vec4 screenParams{};
    MATH::Vec4 temporalParams{};
};

struct SkinConstants {
    MATH::Mat4 current[kMaxJointCount]{};
    MATH::Mat4 previous[kMaxJointCount]{};
};

static_assert(sizeof(GeometryConstants) == 352u);
static_assert(offsetof(GeometryConstants, screenParams) == 320u);
static_assert(offsetof(GeometryConstants, temporalParams) == 336u);
static_assert(sizeof(SkinConstants) == 16384u);
static_assert(offsetof(SkinConstants, previous) == 8192u);

struct TemporalGeometryPassState {
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> staticPso[2]{};
    ComPtr<ID3D12PipelineState> skinnedPso[2]{};
    uint64_t ownerDeviceGeneration = 0u;
    ID3D12Device* ownerDevice = nullptr;
    bool ready = false;
};

TemporalGeometryPassState& State() {
    static TemporalGeometryPassState state{};
    return state;
}

bool CreatePipelineVariant(ID3D12Device* device, ID3D12RootSignature* rootSignature, D3D12_SHADER_BYTECODE vertexShader,
                           D3D12_SHADER_BYTECODE pixelShader, const D3D12_INPUT_ELEMENT_DESC* inputElements,
                           UINT inputElementCount, bool doubleSided, ID3D12PipelineState** outPipeline) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature;
    desc.VS = vertexShader;
    desc.PS = pixelShader;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.FrontCounterClockwise = MATH::kFrontFaceCounterClockwise ? TRUE : FALSE;
    desc.RasterizerState.CullMode = doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // Reconstruct coverage from the finished opaque depth buffer. Exact
    // depth ownership preserves alpha-tested silhouettes without
    // rebinding every material texture in this sidecar pass.
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    desc.InputLayout = {inputElements, inputElementCount};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 2;
    desc.RTVFormats[0] = kTemporalMotionVectorFormat;
    desc.RTVFormats[1] = kTemporalMotionMetadataFormat;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(outPipeline)));
}

bool EnsurePipeline(const GFX::Context& graphics) {
    TemporalGeometryPassState& state = State();
    ID3D12Device* device = graphics.device;
    if (device == nullptr || graphics.deviceGeneration == 0u) {
        return false;
    }
    if (state.ready) {
        return state.ownerDeviceGeneration == graphics.deviceGeneration && state.ownerDevice == device &&
               state.rootSignature != nullptr && state.staticPso[0] != nullptr && state.staticPso[1] != nullptr &&
               state.skinnedPso[0] != nullptr && state.skinnedPso[1] != nullptr;
    }
    if (state.ownerDeviceGeneration != 0u &&
        (state.ownerDeviceGeneration != graphics.deviceGeneration || state.ownerDevice != device)) {
        return false;
    }

    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> staticPso[2]{};
    ComPtr<ID3D12PipelineState> skinnedPso[2]{};

    D3D12_ROOT_PARAMETER params[2]{};
    for (uint32_t index = 0; index < 2u; ++index) {
        params[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[index].Descriptor.ShaderRegister = index;
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    HRESULT hr = GFX::CreateRootSignature(device, rootDesc, rootSignature.GetAddressOf());
    if (!HIKARI_DX_CHECK(hr, "TemporalGeometryPass::CreateRootSignature")) {
        return false;
    }
    rootSignature->SetName(L"HIKARI.Temporal.GeometryRootSignature");

    const auto staticVs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::Geometry, "VSStatic",
                                                       ASSETS::SHADER::ShaderStage::Vertex);
    const auto skinnedVs = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::Geometry, "VSSkinned",
                                                        ASSETS::SHADER::ShaderStage::Vertex);
    const auto ps = INTERNAL::LoadTemporalShader(ASSETS::SHADER::TemporalShaderPass::Geometry, "PSMain",
                                                 ASSETS::SHADER::ShaderStage::Pixel);
    if (!staticVs || !skinnedVs || !ps) {
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC staticLayout[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                                                      static_cast<UINT>(offsetof(VertexStatic3D, position)),
                                                      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    const D3D12_INPUT_ELEMENT_DESC skinnedLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    for (uint32_t doubleSided = 0; doubleSided < 2u; ++doubleSided) {
        if (!CreatePipelineVariant(device, rootSignature.Get(), INTERNAL::TemporalShaderBytecode(staticVs),
                                   INTERNAL::TemporalShaderBytecode(ps), staticLayout,
                                   static_cast<UINT>(std::size(staticLayout)), doubleSided != 0u,
                                   staticPso[doubleSided].GetAddressOf()) ||
            !CreatePipelineVariant(device, rootSignature.Get(), INTERNAL::TemporalShaderBytecode(skinnedVs),
                                   INTERNAL::TemporalShaderBytecode(ps), skinnedLayout,
                                   static_cast<UINT>(std::size(skinnedLayout)), doubleSided != 0u,
                                   skinnedPso[doubleSided].GetAddressOf())) {
            return false;
        }
    }
    state.rootSignature = std::move(rootSignature);
    for (uint32_t index = 0; index < 2u; ++index) {
        state.staticPso[index] = std::move(staticPso[index]);
        state.skinnedPso[index] = std::move(skinnedPso[index]);
    }
    state.ownerDeviceGeneration = graphics.deviceGeneration;
    state.ownerDevice = device;
    state.ready = true;
    return true;
}

void FillSkinConstants(const std::vector<MATH::Mat4>& current, const std::vector<MATH::Mat4>* previous,
                       SkinConstants& constants) {
    const MATH::Mat4 identity = MATH::Mat4::Identity();
    for (uint32_t index = 0; index < kMaxJointCount; ++index) {
        constants.current[index] = identity;
        constants.previous[index] = identity;
    }
    const size_t currentCount = (std::min)(current.size(), static_cast<size_t>(kMaxJointCount));
    for (size_t index = 0; index < currentCount; ++index) {
        constants.current[index] = current[index];
        constants.previous[index] = current[index];
    }
    if (previous == nullptr) {
        return;
    }
    const size_t previousCount = (std::min)(previous->size(), static_cast<size_t>(kMaxJointCount));
    for (size_t index = 0; index < previousCount; ++index) {
        constants.previous[index] = (*previous)[index];
    }
}

DETAIL::TemporalGeometryHistoryKey ResolveHistoryKey(const MESHRENDERER::TemporalVelocityDraw& draw) noexcept {
    return {draw.objectId,
            draw.objectLifetimeGeneration,
            static_cast<uint64_t>(draw.vertexBuffer.BufferLocation),
            static_cast<uint64_t>(draw.indexBuffer.BufferLocation),
            draw.sourceSurfaceInstanceIndex,
            draw.startIndex,
            draw.indexCount,
            draw.baseVertex};
}
} // namespace

bool ExecuteTemporalGeometryPass(const GFX::Context& graphics, const TemporalInputs& inputs,
                                 D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDepthDsv) {
    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    ID3D12Device* device = graphics.device;
    RenderTarget2D* motionTarget = GetMotionVectorRenderTarget(inputs.resourceFrame);
    RenderTarget2D* metadataTarget = GetMotionMetadataRenderTarget(inputs.resourceFrame);
    if (cmd == nullptr || device == nullptr || !inputs.MatchesResourceFrame() ||
        graphics.deviceGeneration != inputs.resourceFrame.deviceGeneration || motionTarget == nullptr ||
        metadataTarget == nullptr || readOnlyDepthDsv.ptr == 0 || !inputs.motionVectors.valid ||
        !inputs.motionMetadata.valid) {
        return false;
    }
    DETAIL::TemporalResourceState* temporalState = DETAIL::FindWritableState(inputs.resourceFrame);
    if (temporalState == nullptr || !DETAIL::ContextMatchesOwner(*temporalState, graphics)) {
        return false;
    }
    auto& rejectionRects = temporalState->passScratch.geometryHistoryRejectionRects;
    rejectionRects.clear();
    temporalState->passScratch.geometryHistoryRejectionFrame = inputs.resourceFrame;

    std::vector<MESHRENDERER::TemporalVelocityDraw> draws;
    if (!MESHRENDERER::GatherTemporalVelocityDraws(graphics, draws, DETAIL::kTemporalMaximumGeometryDrawCount,
                                                   DETAIL::kTemporalMaximumSkinnedDrawCount)) {
        return false;
    }
    if (draws.size() > static_cast<size_t>(DETAIL::kTemporalMaximumGeometryDrawCount)) {
        return false;
    }
    // 圧縮 MS の法線・LOD と CPU triangle sidecar は同一形状とは限らない。
    // 動的 Shell は偽の EQUAL motion を書かず、描画範囲の履歴を明示的に失効する。
    std::erase_if(draws, [&](const auto& draw) {
        if (draw.shellSettings.x < 2.0f || draw.shellSettings.y <= 0.0f)
            return false;
        const auto rect = ProjectHistoryRejectionBounds(draw.boundsCenterRadius, inputs.frame.camera.viewProj,
                                                        (std::max)(1, metadataTarget->GetWidth()),
                                                        (std::max)(1, metadataTarget->GetHeight()));
        if (rect.left < rect.right && rect.top < rect.bottom)
            rejectionRects.push_back(rect);
        return true;
    });
    const auto invalidateShellHistory = [&] {
        if (rejectionRects.empty())
            return;
        metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float invalid[4]{1.0f, 0.0f, 0.0f, 0.0f};
        cmd->ClearRenderTargetView(metadataTarget->GetRtvHandle(), invalid, static_cast<UINT>(rejectionRects.size()),
                                   rejectionRects.data());
        metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    };
    uint32_t rigidDrawCount = 0;
    uint32_t skinnedDrawCount = 0;
    uint32_t alphaMaskedDrawCount = 0;
    for (const MESHRENDERER::TemporalVelocityDraw& draw : draws) {
        if (draw.skinned) {
            if (draw.jointPalette == nullptr) {
                return false;
            }
            ++skinnedDrawCount;
            if (skinnedDrawCount > DETAIL::kTemporalMaximumSkinnedDrawCount) {
                return false;
            }
        } else {
            ++rigidDrawCount;
        }
        if (draw.alphaMasked) {
            ++alphaMaskedDrawCount;
        }
    }
    if (!SetTemporalGeometryDrawCounts(inputs.resourceFrame, rigidDrawCount, skinnedDrawCount, alphaMaskedDrawCount)) {
        return false;
    }
    TemporalGeometryPassState& state = State();
    if (inputs.frame.resetHistory) {
        DETAIL::ClearTemporalGeometryHistory(*temporalState);
    }
    if (draws.empty()) {
        DETAIL::ClearTemporalGeometryHistory(*temporalState);
        invalidateShellHistory();
        return true;
    }
    if (!EnsurePipeline(graphics)) {
        return false;
    }

    const UINT geometryStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(GeometryConstants));
    const UINT skinStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(SkinConstants));
    const uint64_t geometryBytes = static_cast<uint64_t>(geometryStride) * draws.size();
    const uint64_t skinBytes = static_cast<uint64_t>(skinStride) * skinnedDrawCount;
    DETAIL::TemporalGeometryUploadSlice upload{};
    if (!DETAIL::TryAcquireTemporalGeometryUpload(graphics, inputs.resourceFrame, geometryBytes, skinBytes, upload)) {
        return false;
    }
    temporalState->passScratch.geometryHistory.reserve(draws.size());

    GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorRender, "Temporal.GeometryMotion");
    motionTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {motionTarget->GetRtvHandle(), metadataTarget->GetRtvHandle()};
    cmd->OMSetRenderTargets(2, rtvs, FALSE, &readOnlyDepthDsv);
    const D3D12_VIEWPORT viewport{0.0f,
                                  0.0f,
                                  static_cast<float>((std::max)(1, motionTarget->GetWidth())),
                                  static_cast<float>((std::max)(1, motionTarget->GetHeight())),
                                  0.0f,
                                  1.0f};
    const D3D12_RECT scissor{0, 0, (std::max)(1, motionTarget->GetWidth()), (std::max)(1, motionTarget->GetHeight())};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootSignature(state.rootSignature.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto& geometryHistory = temporalState->passScratch.geometryHistory;
    uint32_t skinDrawIndex = 0u;
    for (size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex) {
        const MESHRENDERER::TemporalVelocityDraw& draw = draws[drawIndex];
        const DETAIL::TemporalGeometryHistoryKey historyKey = ResolveHistoryKey(draw);
        const auto historyIt = historyKey.IsValid() ? geometryHistory.find(historyKey) : geometryHistory.end();
        const bool previousValid = inputs.frame.historyValid && historyIt != geometryHistory.end() &&
                                   inputs.frame.frameIndex > historyIt->second.frameIndex &&
                                   inputs.frame.frameIndex - historyIt->second.frameIndex == 1u &&
                                   draw.objectVersion >= historyIt->second.objectVersion;

        GeometryConstants geometry{};
        geometry.renderViewProj = inputs.frame.camera.viewProj;
        geometry.currentViewProj = inputs.frame.camera.unjitteredViewProj;
        geometry.previousViewProj = inputs.frame.camera.prevUnjitteredViewProj;
        geometry.currentWorld = draw.world;
        geometry.previousWorld = previousValid ? historyIt->second.world : draw.world;
        geometry.screenParams = inputs.frame.camera.screenParams;
        geometry.temporalParams = {previousValid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        std::memcpy(upload.geometryMapped + geometryStride * drawIndex, &geometry, sizeof(geometry));

        if (draw.skinned) {
            SkinConstants skin{};
            const std::vector<MATH::Mat4>* previousPalette = previousValid ? &historyIt->second.jointPalette : nullptr;
            FillSkinConstants(*draw.jointPalette, previousPalette, skin);
            std::memcpy(upload.skinMapped + skinStride * skinDrawIndex, &skin, sizeof(skin));
        }

        cmd->SetPipelineState(draw.skinned ? state.skinnedPso[draw.doubleSided ? 1u : 0u].Get()
                                           : state.staticPso[draw.doubleSided ? 1u : 0u].Get());
        cmd->SetGraphicsRootConstantBufferView(0, upload.geometryGpuAddress +
                                                      static_cast<UINT64>(geometryStride) * drawIndex);
        if (draw.skinned) {
            cmd->SetGraphicsRootConstantBufferView(1, upload.skinGpuAddress +
                                                          static_cast<UINT64>(skinStride) * skinDrawIndex);
            ++skinDrawIndex;
        }
        cmd->IASetVertexBuffers(0, 1, &draw.vertexBuffer);
        cmd->IASetIndexBuffer(&draw.indexBuffer);
        cmd->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
    }
    motionTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    invalidateShellHistory();

    for (const MESHRENDERER::TemporalVelocityDraw& draw : draws) {
        const DETAIL::TemporalGeometryHistoryKey historyKey = ResolveHistoryKey(draw);
        if (!historyKey.IsValid()) {
            continue;
        }
        DETAIL::TemporalGeometryObjectHistory& objectHistory = geometryHistory[historyKey];
        objectHistory.world = draw.world;
        objectHistory.frameIndex = inputs.frame.frameIndex;
        objectHistory.objectVersion = draw.objectVersion;
        if (draw.skinned) {
            const size_t paletteCount = (std::min)(draw.jointPalette->size(), static_cast<size_t>(kMaxJointCount));
            objectHistory.jointPalette.assign(draw.jointPalette->begin(), draw.jointPalette->begin() + paletteCount);
        } else {
            objectHistory.jointPalette.clear();
        }
    }
    for (auto it = geometryHistory.begin(); it != geometryHistory.end();) {
        // Motion history is valid only for consecutive visible frames.
        // Reappearing geometry intentionally starts cold instead of
        // inventing velocity across an occlusion/culling gap.
        if (it->second.frameIndex != inputs.frame.frameIndex) {
            it = geometryHistory.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

void ShutdownTemporalGeometryPass() {
    TemporalGeometryPassState& state = State();
    for (uint32_t index = 0; index < 2u; ++index) {
        state.staticPso[index].Reset();
        state.skinnedPso[index].Reset();
    }
    state.rootSignature.Reset();
    state.ownerDeviceGeneration = 0u;
    state.ownerDevice = nullptr;
    state.ready = false;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
