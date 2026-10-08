#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"

#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <sstream>
#include <string>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::PANORAMA_SKY {

using Microsoft::WRL::ComPtr;

namespace {

struct PanoramaLayerConstants {
    MATH::Mat4 inverseViewProj{};
    MATH::Vec4 yawOffsetOpacity{};
    MATH::Vec4 tintExposure{};
};

static_assert(sizeof(PanoramaLayerConstants) == 96u);

struct State {
    bool initialized = false;
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipelineState{};
    ComPtr<ID3DBlob> vertexShader{};
    ComPtr<ID3DBlob> pixelShader{};
    std::string loadedTexturePath{};
    std::string warnedMissingAsset{};
    TextureResourceHandle textureResource{};
};

State g{};

float SanitizeFinite(float value, float fallback, float minimum, float maximum) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

float WrapUnit(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    value = std::fmod(value, 1.0f);
    return value < 0.0f ? value + 1.0f : value;
}

void LogPanoramaTextureQuality(const std::string& path, TextureResourceHandle resource) {
    const RenderResourceRecord* record = GetTextureResourceRecord(resource);
    if (record == nullptr) {
        return;
    }

    const uint64_t width = record->desc.width;
    const uint32_t height = record->desc.height;
    const uint32_t mipCount = GetTextureResourceMipCount(resource);
    std::ostringstream summary{};
    summary << "[PanoramaSkyLayerStage][Texture] path=" << path << " size=" << width << "x" << height
            << " mips=" << mipCount;
    HIKARI_LOG_INFO(summary.str());

    const float aspect = height > 0u ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
    if (height == 0u || std::abs(aspect - 2.0f) > 0.02f) {
        HIKARI_LOG_WARN("[PanoramaSkyLayerStage][Texture] expected a 2:1 "
                        "equirectangular texture: " +
                        path);
    }
    if (mipCount <= 1u) {
        HIKARI_LOG_WARN("[PanoramaSkyLayerStage][Texture] texture has no mip "
                        "chain; reimport it as mipmapped HTEX: " +
                        path);
    }
}

bool CreatePipeline(ID3D12Device* device) {
    if (device == nullptr || !GFX::SupportsShaderModel6(device)) {
        return false;
    }
    if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkyVS.hlsl", "main", GFX::ShaderStage::Vertex,
                                   g.vertexShader.GetAddressOf()) ||
        !GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_PanoramaSkyLayerPS.hlsl", "main", GFX::ShaderStage::Pixel,
                                   g.pixelShader.GetAddressOf())) {
        HIKARI_LOG_ERROR("[PanoramaSkyLayerStage] shader compilation failed.");
        return false;
    }

    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1u;
    textureRange.BaseShaderRegister = 0u;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[0].Constants.ShaderRegister = 0u;
    parameters[0].Constants.Num32BitValues = sizeof(PanoramaLayerConstants) / sizeof(uint32_t);
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[1].DescriptorTable.pDescriptorRanges = &textureRange;

    D3D12_STATIC_SAMPLER_DESC panoramaSampler{};
    panoramaSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    panoramaSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    panoramaSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    panoramaSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    panoramaSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    panoramaSampler.ShaderRegister = 0u;
    panoramaSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    panoramaSampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC signatureDesc{};
    signatureDesc.NumParameters = 2u;
    signatureDesc.pParameters = parameters;
    signatureDesc.NumStaticSamplers = 1u;
    signatureDesc.pStaticSamplers = &panoramaSampler;
    signatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    if (FAILED(GFX::CreateRootSignature(device, signatureDesc, g.rootSignature.GetAddressOf()))) {
        HIKARI_LOG_ERROR("[PanoramaSkyLayerStage] root signature creation failed.");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = g.rootSignature.Get();
    pipelineDesc.VS = {
        g.vertexShader->GetBufferPointer(),
        g.vertexShader->GetBufferSize(),
    };
    pipelineDesc.PS = {
        g.pixelShader->GetBufferPointer(),
        g.pixelShader->GetBufferSize(),
    };
    pipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pipelineDesc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    // The shader emits premultiplied color. This prevents transparent
    // black PNG texels from creating a dark fringe around cloud edges.
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineDesc.SampleMask = UINT_MAX;
    pipelineDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipelineDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pipelineDesc.InputLayout = {nullptr, 0u};
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.NumRenderTargets = 1u;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineDesc.SampleDesc.Count = 1u;

    if (FAILED(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(g.pipelineState.GetAddressOf())))) {
        HIKARI_LOG_ERROR("[PanoramaSkyLayerStage] pipeline creation failed.");
        return false;
    }
    return true;
}

bool EnsureInitialized(const GFX::Context& graphics) {
    if (g.initialized) {
        return true;
    }
    if (!CreatePipeline(graphics.device)) {
        return false;
    }
    g.initialized = true;
    return true;
}

TextureResourceHandle ResolveTexture(const SkySettings& settings, SkyManager& skyManager) {
    const SkyAsset* asset = skyManager.FindAsset(settings.panoramaCloudAsset);
    if (asset == nullptr || asset->texturePath.empty()) {
        if (!settings.panoramaCloudAsset.empty() && g.warnedMissingAsset != settings.panoramaCloudAsset) {
            g.warnedMissingAsset = settings.panoramaCloudAsset;
            HIKARI_LOG_WARN("[PanoramaSkyLayerStage] cloud texture asset is unresolved: " +
                            settings.panoramaCloudAsset);
        }
        return {};
    }
    g.warnedMissingAsset.clear();
    if (g.loadedTexturePath != asset->texturePath || !IsTextureResourceValid(g.textureResource)) {
        ReleaseTextureResource(g.textureResource);
        g.loadedTexturePath = asset->texturePath;
        g.textureResource = LoadTextureResourceWithColorSpace("sky_renderer/panorama_cloud_layer", asset->texturePath,
                                                              asset->textureIsLinear ? TextureResourceColorSpace::Linear
                                                                                     : TextureResourceColorSpace::Srgb);
        if (IsTextureResourceValid(g.textureResource)) {
            LogPanoramaTextureQuality(asset->texturePath, g.textureResource);
        }
    }
    return g.textureResource;
}

} // namespace

bool RenderPanoramaSkyLayer(const GFX::Context& graphics, const Camera3D& camera, const SkySettings& settings,
                            SkyManager& skyManager) {
    const float opacity = SanitizeFinite(settings.panoramaCloudOpacity, 1.0f, 0.0f, 1.0f);
    if (settings.source != SkySource::Panorama || settings.panoramaCloudAsset.empty() || opacity <= 1.0e-4f ||
        graphics.cmdList == nullptr || !EnsureInitialized(graphics)) {
        return false;
    }

    const TextureResourceHandle texture = ResolveTexture(settings, skyManager);
    const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = GetTextureResourceSrvGpuHandle(texture);
    if (!IsTextureResourceValid(texture) || textureSrv.ptr == 0u) {
        return false;
    }

    const float elapsed = SanitizeFinite(TIME::GetUnscaledElapsedSeconds(), 0.0f, 0.0f, 100000000.0f);
    const float scrollX = SanitizeFinite(settings.panoramaCloudScrollSpeed.x, 0.0015f, -1.0f, 1.0f);
    const float scrollY = SanitizeFinite(settings.panoramaCloudScrollSpeed.y, 0.0f, -1.0f, 1.0f);

    PanoramaLayerConstants constants{};
    constants.inverseViewProj = MATH::Inverse(camera.GetViewProj());
    constants.yawOffsetOpacity = {
        SanitizeFinite(settings.yaw, 0.0f, -100000.0f, 100000.0f),
        WrapUnit(scrollX * elapsed),
        WrapUnit(scrollY * elapsed),
        opacity,
    };
    constants.tintExposure = {
        SanitizeFinite(settings.tint.x, 1.0f, 0.0f, 64.0f),
        SanitizeFinite(settings.tint.y, 1.0f, 0.0f, 64.0f),
        SanitizeFinite(settings.tint.z, 1.0f, 0.0f, 64.0f),
        ResolveSkyTextureRadianceScale(settings) * RENDER3D::COLOR::GetActiveViewExposure().x,
    };

    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    GFX::PIX::ScopedGpuEvent event(cmd, GFX::PIX::kColorRender, "PanoramaSkyLayer");
    cmd->SetGraphicsRootSignature(g.rootSignature.Get());
    cmd->SetPipelineState(g.pipelineState.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRoot32BitConstants(0u, sizeof(constants) / sizeof(uint32_t), &constants, 0u);

    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap != nullptr ? graphics.srvHeap : GetTextureResourceSrvHeap();
    if (srvHeap != nullptr) {
        ID3D12DescriptorHeap* heaps[] = {srvHeap};
        cmd->SetDescriptorHeaps(1u, heaps);
    }
    cmd->SetGraphicsRootDescriptorTable(1u, textureSrv);
    cmd->DrawInstanced(3u, 1u, 0u, 0u);
    return true;
}

void InvalidatePanoramaSkyLayerTextureCache() {
    ReleaseTextureResource(g.textureResource);
    g.textureResource = {};
    g.loadedTexturePath.clear();
    g.warnedMissingAsset.clear();
}

void ShutdownPanoramaSkyLayerStage() {
    InvalidatePanoramaSkyLayerTextureCache();
    g.pipelineState.Reset();
    g.rootSignature.Reset();
    g.vertexShader.Reset();
    g.pixelShader.Reset();
    g.initialized = false;
}

} // namespace HIKARI::RENDER3D::PANORAMA_SKY
