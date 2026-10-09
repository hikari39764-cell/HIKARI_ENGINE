#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "HIKARI_SkyRenderer.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <limits>
#include <sstream>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereRuntime.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::SKYRENDERER {

using Microsoft::WRL::ComPtr;

namespace {
constexpr uint32_t kSkyConstantDrawCapacityPerFrame = 64u;
constexpr uint64_t kInvalidSkyConstantFrameSerial = (std::numeric_limits<uint64_t>::max)();

struct SkyCB {
    MATH::Mat4 inverseViewProj{};
    // x 空の画素値を輝度へ写す倍率、y 空の回転、z このビューの Pre-Exposure、w 物理大気の地平線より下の背景
    // （0 地平線の霞、1 惑星の地表）。
    MATH::Vec4 scaleYaw{};
    MATH::Vec4 tintMode{};
    // 主光源の円盤。xyz 光源へ向かう方向、w 視半径 [rad]。
    MATH::Vec4 disk{};
    // 円盤の平均輝度（大気の透過率込み）[nits]。
    MATH::Vec4 diskLuminance{};
    // 夜空。xyz 太陽へ向かう方向（月の満ち欠け）、w 1 なら主光源の円盤は月。
    MATH::Vec4 moonPhase{};
    // 夜空。x 星と銀河の明るさの倍率、y 限界等級、z 天頂の背景輝度 [nits]、w 瞬きの時刻 [s]。
    MATH::Vec4 stars{};
    // 天球座標からワールドへの回転の列。
    MATH::Vec4 celestialX{1, 0, 0, 0};
    MATH::Vec4 celestialY{0, 1, 0, 0};
    MATH::Vec4 celestialZ{0, 0, 1, 0};
};

static_assert(sizeof(SkyCB) == 208u);

using AtmosphereCB = ATMOSPHERE::AtmosphereLutConstants;

static_assert(sizeof(AtmosphereCB) == 128u);

using ATMOSPHERE::SanitizeDirection;
using ATMOSPHERE::SanitizeFiniteRange;

void LogPanoramaTextureQuality(const std::string& path, RENDER3D::TextureResourceHandle resource) {
    const RENDER3D::RenderResourceRecord* record = RENDER3D::GetTextureResourceRecord(resource);
    if (record == nullptr) {
        return;
    }

    const uint64_t width = record->desc.width;
    const uint32_t height = record->desc.height;
    const uint32_t mipCount = RENDER3D::GetTextureResourceMipCount(resource);
    std::ostringstream summary{};
    summary << "[SkyRenderer][PanoramaTexture] path=" << path << " size=" << width << "x" << height
            << " mips=" << mipCount;
    HIKARI_LOG_INFO(summary.str());

    const float aspect = height > 0u ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
    if (height == 0u || std::abs(aspect - 2.0f) > 0.02f) {
        HIKARI_LOG_WARN("[SkyRenderer][PanoramaTexture] expected a 2:1 "
                        "equirectangular texture: " +
                        path);
    }
    if (mipCount <= 1u) {
        HIKARI_LOG_WARN("[SkyRenderer][PanoramaTexture] texture has no mip chain; "
                        "reimport it as mipmapped HTEX: " +
                        path);
    }
}

SunGlareSettings SanitizeSunGlare(const SunGlareSettings& settings) noexcept {
    SunGlareSettings result = settings;
    result.intensity = SanitizeFiniteRange(result.intensity, 0.15f, 0.0f, 8.0f);
    result.haloRadius = SanitizeFiniteRange(result.haloRadius, 0.08f, 0.0f, 2.0f);
    result.haloFalloff = SanitizeFiniteRange(result.haloFalloff, 3.0f, 0.1f, 16.0f);
    result.streakIntensity = SanitizeFiniteRange(result.streakIntensity, 0.0f, 0.0f, 8.0f);
    result.streakLength = SanitizeFiniteRange(result.streakLength, 0.15f, 0.0f, 2.0f);
    result.occlusionRadius = SanitizeFiniteRange(result.occlusionRadius, 0.012f, 0.0f, 0.25f);
    result.edgeFade = SanitizeFiniteRange(result.edgeFade, 0.15f, 0.0f, 1.0f);
    result.maxRadiance = SanitizeFiniteRange(result.maxRadiance, 4.0f, 0.0f, 64.0f);
    result.shaftIntensity = SanitizeFiniteRange(result.shaftIntensity, 0.0f, 0.0f, 8.0f);
    result.shaftLength = SanitizeFiniteRange(result.shaftLength, 0.55f, 0.01f, 2.0f);
    result.shaftDecay = SanitizeFiniteRange(result.shaftDecay, 0.94f, 0.5f, 0.999f);
    result.ghostIntensity = SanitizeFiniteRange(result.ghostIntensity, 0.0f, 0.0f, 4.0f);
    return result;
}

AtmosphereFrameData BuildAtmosphereFrameDataInternal(const SceneEnvironment& environment, const Camera3D* camera,
                                                     const ATMOSPHERE::AtmosphereSolarState& solar) {
    AtmosphereFrameData result{};
    result.valid = solar.valid;
    result.atmosphereEnabled = solar.atmosphereEnabled;
    result.sunDirection = solar.sunDirection;
    result.sunSourceColor = solar.sunSourceColor;
    result.sunColor = solar.sunColor;
    result.sunTransmittance = solar.sunTransmittance;
    result.sunSourceRadiance = solar.sunSourceRadiance;
    result.sunRadiance = solar.sunRadiance;
    result.sunAngularRadius = solar.sunAngularRadius;
    result.sunGlare = SanitizeSunGlare(environment.sunGlare);
    result.usingAnalyticFallback = result.atmosphereEnabled;
    result.lutsReady = false;

    if (camera != nullptr) {
        const MATH::Vec3 probe = camera->GetPosition() + result.sunDirection * 1000.0f;
        const MATH::Vec4 projected = camera->GetViewProj().TransformPoint({
            probe.x,
            probe.y,
            probe.z,
            1.0f,
        });
        if (std::isfinite(projected.x) && std::isfinite(projected.y) && std::isfinite(projected.w) &&
            projected.w > 1.0e-5f) {
            const float ndcX = projected.x / projected.w;
            const float ndcY = projected.y / projected.w;
            result.screenSunPosition = {
                ndcX * 0.5f + 0.5f,
                1.0f - (ndcY * 0.5f + 0.5f),
            };
            result.screenSunVisible = true;
        }
    }
    return result;
}

struct State {
    bool initialized = false;
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3D12Resource> cb;
    ComPtr<ID3D12Resource> atmosphereCb;
    UINT constantStride = 0;
    UINT atmosphereConstantStride = 0;
    uint8_t* constantMapped = nullptr;
    uint8_t* atmosphereConstantMapped = nullptr;
    std::array<uint64_t, GFX::kFrameResourceCount> constantFrameSerials{};
    std::array<uint32_t, GFX::kFrameResourceCount> constantDrawCursors{};
    std::string loadedTexturePath{};
    std::string loadedCubemapPath{};
    bool loadedTextureIsLinear = false;
    bool loadedCubemapIsLinear = false;
    bool textureLoadAttempted = false;
    bool cubemapLoadAttempted = false;
    RENDER3D::TextureResourceHandle textureResource{};
    RENDER3D::TextureResourceHandle cubemapResource{};
    RENDER3D::TextureResourceHandle fallbackTextureResource{};
    int textureHandle = -1;
    int cubemapHandle = -1;
    int fallbackTextureHandle = -1;
    SkyRendererDebugState debug{};
    SkyEnvironmentData environmentData{};
    AtmosphereFrameData atmosphereFrame{};
    AerialPerspectiveView aerialPerspective{};
    ATMOSPHERE::AtmosphereLutOwner atmosphereLuts{};
};

State g;

struct SkyConstantAllocation {
    SkyCB* cpu = nullptr;
    AtmosphereCB* atmosphereCpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    D3D12_GPU_VIRTUAL_ADDRESS atmosphereGpu = 0;
    uint32_t frameSlot = 0;
    uint32_t drawSlot = 0;

    bool IsValid() const noexcept {
        return cpu != nullptr && atmosphereCpu != nullptr && gpu != 0 && atmosphereGpu != 0;
    }
};

bool CreateBuffers(ID3D12Device* device) {
    g.constantStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(SkyCB));
    const UINT64 cbBytes =
        static_cast<UINT64>(g.constantStride) * kSkyConstantDrawCapacityPerFrame * GFX::kFrameResourceCount;
    g.atmosphereConstantStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(AtmosphereCB));
    const UINT64 atmosphereCbBytes =
        static_cast<UINT64>(g.atmosphereConstantStride) * kSkyConstantDrawCapacityPerFrame * GFX::kFrameResourceCount;
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(cbBytes);
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(g.cb.GetAddressOf())))) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create constant buffer failed.");
        return false;
    }
    auto atmosphereCbDesc = CD3DX12_RESOURCE_DESC::Buffer(atmosphereCbBytes);
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &atmosphereCbDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(g.atmosphereCb.GetAddressOf())))) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create atmosphere constant buffer failed.");
        return false;
    }
    if (FAILED(g.cb->Map(0, nullptr, reinterpret_cast<void**>(&g.constantMapped)))) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Map constant buffer failed.");
        return false;
    }
    if (FAILED(g.atmosphereCb->Map(0, nullptr, reinterpret_cast<void**>(&g.atmosphereConstantMapped)))) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Map atmosphere constant buffer failed.");
        return false;
    }
    g.constantFrameSerials.fill(kInvalidSkyConstantFrameSerial);
    g.constantDrawCursors.fill(0u);
    g.debug.constantCapacityPerFrame = kSkyConstantDrawCapacityPerFrame;
    return true;
}

SkyConstantAllocation AllocateSkyConstants(const GFX::Context& graphics) {
    if (g.cb == nullptr || g.constantMapped == nullptr || g.constantStride == 0u || g.atmosphereCb == nullptr ||
        g.atmosphereConstantMapped == nullptr || g.atmosphereConstantStride == 0u) {
        return {};
    }

    const uint32_t frameSlot = graphics.frameIndex % GFX::kFrameResourceCount;
    const uint64_t frameSerial = TIME::GetFrameContext().frameIndex;
    if (g.constantFrameSerials[frameSlot] != frameSerial) {
        g.constantFrameSerials[frameSlot] = frameSerial;
        g.constantDrawCursors[frameSlot] = 0u;
    }

    const uint32_t drawSlot = g.constantDrawCursors[frameSlot];
    if (drawSlot >= kSkyConstantDrawCapacityPerFrame) {
        ++g.debug.constantOverflowCount;
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Per-frame constant draw capacity exhausted; "
                         "additional sky draw was skipped to preserve GPU resource lifetime.");
        return {};
    }
    ++g.constantDrawCursors[frameSlot];

    const UINT64 allocationIndex = static_cast<UINT64>(frameSlot) * kSkyConstantDrawCapacityPerFrame + drawSlot;
    const UINT64 byteOffset = allocationIndex * g.constantStride;

    g.debug.constantFrameSlot = frameSlot;
    g.debug.constantDrawSlot = drawSlot;
    g.debug.constantDrawCountThisFrame = g.constantDrawCursors[frameSlot];
    g.debug.constantCapacityPerFrame = kSkyConstantDrawCapacityPerFrame;

    SkyConstantAllocation allocation{};
    allocation.cpu = reinterpret_cast<SkyCB*>(g.constantMapped + static_cast<size_t>(byteOffset));
    allocation.gpu = g.cb->GetGPUVirtualAddress() + byteOffset;
    const UINT64 atmosphereByteOffset = allocationIndex * g.atmosphereConstantStride;
    allocation.atmosphereCpu =
        reinterpret_cast<AtmosphereCB*>(g.atmosphereConstantMapped + static_cast<size_t>(atmosphereByteOffset));
    allocation.atmosphereGpu = g.atmosphereCb->GetGPUVirtualAddress() + atmosphereByteOffset;
    allocation.frameSlot = frameSlot;
    allocation.drawSlot = drawSlot;
    return allocation;
}

bool CreatePipeline(ID3D12Device* device) {
    if (!GFX::SupportsShaderModel6(device)) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Shader Model 6.0 is not supported by this device.");
        return false;
    }
    if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkyVS.hlsl", "main", GFX::ShaderStage::Vertex,
                                   g.vsBlob.GetAddressOf())) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Compile Render3D_SkyVS.hlsl failed.");
        return false;
    }
    if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkyPS.hlsl", "main", GFX::ShaderStage::Pixel,
                                   g.psBlob.GetAddressOf())) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Compile Render3D_SkyPS.hlsl failed.");
        return false;
    }

    D3D12_DESCRIPTOR_RANGE texture2DRange{};
    texture2DRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texture2DRange.NumDescriptors = 1;
    texture2DRange.BaseShaderRegister = 0;
    texture2DRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE cubeRange{};
    cubeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    cubeRange.NumDescriptors = 1;
    cubeRange.BaseShaderRegister = 1;
    cubeRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE atmosphereSkyViewRange{};
    atmosphereSkyViewRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    atmosphereSkyViewRange.NumDescriptors = 1;
    atmosphereSkyViewRange.BaseShaderRegister = 2;
    atmosphereSkyViewRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[5]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister = 1;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &texture2DRange;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &cubeRange;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[4].DescriptorTable.NumDescriptorRanges = 1;
    params[4].DescriptorTable.pDescriptorRanges = &atmosphereSkyViewRange;

    D3D12_STATIC_SAMPLER_DESC linearClampSampler{};
    linearClampSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    // Equirectangular panoramas and the atmosphere azimuth are
    // periodic horizontally; V remains clamped at the poles.
    linearClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    linearClampSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClampSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linearClampSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    linearClampSampler.ShaderRegister = 0;
    linearClampSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    linearClampSampler.MaxLOD = D3D12_FLOAT32_MAX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(std::size(params));
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &linearClampSampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    if (FAILED(GFX::CreateRootSignature(device, rsDesc, g.rootSig.GetAddressOf()))) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create root signature failed.");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = g.rootSig.Get();
    psoDesc.VS = {g.vsBlob->GetBufferPointer(), g.vsBlob->GetBufferSize()};
    psoDesc.PS = {g.psBlob->GetBufferPointer(), g.psBlob->GetBufferSize()};
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = {nullptr, 0};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(g.pso.GetAddressOf())))) {
        HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create PSO failed.");
        return false;
    }
    ++g.debug.psoCreateCount;
    return true;
}

bool EnsureInitialized(const GFX::Context& graphics) {
    if (g.initialized) {
        return true;
    }
    auto* device = graphics.device;
    if (!device) {
        return false;
    }
    if (!CreateBuffers(device) || !CreatePipeline(device)) {
        return false;
    }
    g.fallbackTextureResource = RENDER3D::CreateSolidColorTextureResource("sky_renderer/fallback_white", 0xffffffffu,
                                                                          RENDER3D::TextureResourceColorSpace::Srgb);
    g.fallbackTextureHandle = RENDER3D::GetTextureResourceBackendHandle(g.fallbackTextureResource);
    g.initialized = true;
    g.debug.initialized = true;
    return true;
}

RENDER3D::TextureResourceHandle ResolveTexture2D(const SkySettings& settings, SkyManager& skyManager) {
    const SkyAsset* skyAsset = skyManager.FindAsset(settings.asset);
    g.debug.skyAssetFound = (skyAsset != nullptr);
    if (!skyAsset) {
        g.debug.activeTexturePath.clear();
        g.debug.textureValid = false;
        g.debug.usingFallback = true;
        if (!settings.asset.empty()) {
            HIKARI_LOG_WARN("[SkyRenderer] sky asset not registered: " + settings.asset);
        }
        return {};
    }

    const std::string& path = skyAsset->texturePath;
    const bool isLinear = skyAsset->textureIsLinear;
    g.debug.activeTexturePath = path;
    if (g.loadedTexturePath != path || g.loadedTextureIsLinear != isLinear) {
        RENDER3D::ReleaseTextureResource(g.textureResource);
        g.loadedTexturePath = path;
        g.loadedTextureIsLinear = isLinear;
        g.textureLoadAttempted = false;
        g.textureResource = {};
        g.textureHandle = -1;
    }
    if (!g.textureLoadAttempted && !RENDER3D::IsTextureResourceValid(g.textureResource)) {
        g.textureLoadAttempted = true;
        if (!path.empty()) {
            g.textureResource = RENDER3D::LoadTextureResourceWithColorSpace(
                "sky_renderer/scene_sky", path,
                isLinear ? RENDER3D::TextureResourceColorSpace::Linear : RENDER3D::TextureResourceColorSpace::Srgb);
            g.textureHandle = RENDER3D::GetTextureResourceBackendHandle(g.textureResource);
            if (RENDER3D::IsTextureResourceValid(g.textureResource)) {
                LogPanoramaTextureQuality(path, g.textureResource);
            }
        }
    }
    return g.textureResource;
}

RENDER3D::TextureResourceHandle ResolveCubemap(const SkySettings& settings, SkyManager& skyManager) {
    const SkyAsset* skyAsset = skyManager.FindAsset(settings.asset);
    g.debug.skyAssetFound = (skyAsset != nullptr);
    if (!skyAsset) {
        g.debug.activeTexturePath.clear();
        g.debug.cubemapLoaded = false;
        g.debug.textureValid = false;
        g.debug.usingFallback = true;
        if (!settings.asset.empty()) {
            HIKARI_LOG_WARN("[SkyRenderer] sky asset not registered: " + settings.asset);
        }
        return {};
    }

    const std::string path = skyAsset->texturePath;
    const bool isLinear = skyAsset->textureIsLinear;
    g.debug.activeTexturePath = path;
    if (path.empty()) {
        return {};
    }

    if (g.loadedCubemapPath != path || g.loadedCubemapIsLinear != isLinear) {
        RENDER3D::ReleaseTextureResource(g.cubemapResource);
        g.loadedCubemapPath = path;
        g.loadedCubemapIsLinear = isLinear;
        g.cubemapLoadAttempted = false;
        g.cubemapResource = {};
        g.cubemapHandle = -1;
    }
    if (!g.cubemapLoadAttempted && !RENDER3D::IsTextureResourceValid(g.cubemapResource)) {
        g.cubemapLoadAttempted = true;
        g.cubemapResource = RENDER3D::LoadCubemapResource("sky_renderer/scene_sky_cube", path,
                                                          isLinear ? RENDER3D::TextureResourceColorSpace::Linear
                                                                   : RENDER3D::TextureResourceColorSpace::Srgb);
        g.cubemapHandle = RENDER3D::GetTextureResourceBackendHandle(g.cubemapResource);
        if (g.cubemapHandle < 0) {
            std::ostringstream oss;
            oss << "[SkyRenderer][WARN] Cubemap load failed; the sky renders black. asset=" << settings.asset
                << " path=" << path;
            HIKARI_LOG_WARN(oss.str());
        }
    }
    return g.cubemapResource;
}
} // namespace

AtmosphereFrameData BuildAtmosphereFrameData(const SceneEnvironment& environment) {
    return BuildAtmosphereFrameDataInternal(environment, nullptr, ATMOSPHERE::BuildAtmosphereSolarState(environment));
}

AtmosphereFrameData BuildAtmosphereFrameData(const SceneEnvironment& environment, const Camera3D& camera) {
    return BuildAtmosphereFrameDataInternal(environment, &camera, ATMOSPHERE::BuildAtmosphereSolarState(environment));
}

void Reset() {
    IBL::BeginEnvironmentFrame();
    g.environmentData = {};
    g.atmosphereFrame = {};
    g.debug.lastRenderSubmitted = false;
    g.debug.skyAssetFound = false;
    g.debug.cubemapLoaded = false;
    g.debug.usingFallback = false;
    g.debug.textureValid = false;
    g.debug.drawCount = 0;
    g.debug.mode = SkyMode::None;
    g.debug.cubemapResource = {};
    g.debug.textureResource = {};
    g.debug.cubemapHandle = -1;
    g.debug.textureHandle = -1;
}

void ShutdownAtmosphereRuntime() {
    IBL::ShutdownEnvironmentLighting(GFX::GetCurrentContext());
    g.atmosphereLuts.Reset();
    g.atmosphereFrame = {};
}
// キャッシュされたテクスチャを解放し、次回の描画で再ロードするようにする
void InvalidateSkyTextureCache() {
    // 既にロードされているテクスチャがあれば解放する
    RENDER3D::ReleaseTextureResource(g.textureResource);
    // リソース層に登録されたキューブマップを解放する。
    RENDER3D::ReleaseTextureResource(g.cubemapResource);

    g.loadedTexturePath.clear();
    g.loadedCubemapPath.clear();
    g.loadedTextureIsLinear = false;
    g.loadedCubemapIsLinear = false;
    g.textureLoadAttempted = false;
    g.cubemapLoadAttempted = false;

    g.textureResource = {};
    g.cubemapResource = {};
    g.textureHandle = -1;
    g.cubemapHandle = -1;

    g.debug.textureResource = {};
    g.debug.cubemapResource = {};
    g.debug.textureHandle = -1;
    g.debug.cubemapHandle = -1;
    g.debug.activeTexturePath.clear();
}

void Render(const GFX::Context& graphics, const Camera3D& camera, const SceneEnvironment& environment, ModelManager&,
            SkyManager& skyManager, bool drawSky) {
    const SkySettings& settings = environment.sky;
    IBL::BeginEnvironmentFrame();
    g.aerialPerspective = {};
    g.atmosphereLuts.SetContext(graphics);
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    g.atmosphereFrame = BuildAtmosphereFrameDataInternal(environment, &camera, solar);
    g.debug.initialized = g.initialized;
    g.debug.activeSkyAsset = settings.asset;
    g.debug.activeTexturePath.clear();
    g.debug.usingFallback = false;

    if (!EnsureInitialized(graphics)) {
        g.debug.initialized = g.initialized;
        return;
    }

    RENDER3D::TextureResourceHandle texture2DResource = g.fallbackTextureResource;
    RENDER3D::TextureResourceHandle cubemapResource{};
    // 読み込めない空の資産は黒い空（None）にする。別の空へ黙って差し替えない。
    SkyMode renderMode = ResolveSkyRenderMode(settings.source);
    (void)g.atmosphereLuts.Configure(environment.atmosphere, renderMode == SkyMode::Atmosphere);

    if (renderMode == SkyMode::Texture2D) {
        const RENDER3D::TextureResourceHandle resolved = ResolveTexture2D(settings, skyManager);
        if (RENDER3D::IsTextureResourceValid(resolved)) {
            texture2DResource = resolved;
        } else {
            g.debug.usingFallback = true;
            renderMode = SkyMode::None;
        }
    } else if (renderMode == SkyMode::Cubemap) {
        cubemapResource = ResolveCubemap(settings, skyManager);
        if (!RENDER3D::IsTextureResourceValid(cubemapResource)) {
            g.debug.usingFallback = true;
            renderMode = SkyMode::None;
        }
    }

    auto* cmd = graphics.cmdList;
    if (!cmd) {
        return;
    }
    const SkyConstantAllocation constants = AllocateSkyConstants(graphics);
    if (!constants.IsValid()) {
        return;
    }
    // PIX 上で sky pass の境界を追いやすくする。
    GFX::PIX::ScopedGpuEvent pixSky(cmd, GFX::PIX::kColorRender, "SkyRenderer.Render");

    const MATH::Mat4 inverseViewProj = MATH::Inverse(camera.GetViewProj());
    constants.cpu->inverseViewProj = inverseViewProj;
    // 空の描画だけがこのビューの HDR 空間で書く。前向きの空/IBL 参照は絶対量のまま。
    constants.cpu->scaleYaw = {ResolveSkyTextureRadianceScale(settings), settings.yaw,
                               RENDER3D::COLOR::GetActiveViewExposure().x,
                               environment.atmosphere.belowHorizon == BelowHorizonMode::Ground ? 1.0f : 0.0f};
    constants.cpu->tintMode = {settings.tint.x, settings.tint.y, settings.tint.z, static_cast<float>(renderMode)};
    // 円盤の平均輝度 = 大気外の法線照度 / 立体角 × 色ごとの大気の透過率（落日が赤いのはこの透過率）。
    const float diskSolidAngle = (std::max)(2.0f * 3.14159265f * (1.0f - std::cos(solar.sunAngularRadius)), 1.0e-9f);
    // 夜は大気の光源が月になる。月の円盤は満月の照度で描き、満ち欠けは shader が太陽の向きから付ける。
    const EnvironmentLightingFrame& frame = environment.frame;
    const bool moonDisk = frame.moonIsPrimary && renderMode == SkyMode::Atmosphere && environment.moon.enabled;
    float diskIlluminance = 0.0f;
    if (moonDisk) {
        diskIlluminance = environment.moon.illuminance;
    } else if (environment.sun.showDisk && !frame.moonIsPrimary) {
        diskIlluminance = solar.sunSourceRadiance;
    }
    const float diskScale = diskIlluminance / diskSolidAngle;
    constants.cpu->disk = {solar.sunDirection.x, solar.sunDirection.y, solar.sunDirection.z, solar.sunAngularRadius};
    constants.cpu->diskLuminance = {solar.sunSourceColor.x * solar.sunTransmittance.x * diskScale,
                                    solar.sunSourceColor.y * solar.sunTransmittance.y * diskScale,
                                    solar.sunSourceColor.z * solar.sunTransmittance.z * diskScale, 0.0f};
    constants.cpu->moonPhase = {frame.sun.toLight.x, frame.sun.toLight.y, frame.sun.toLight.z, moonDisk ? 1.0f : 0.0f};
    const StarSettings& stars = environment.stars;
    const MATH::Vec4 nightSky = stars.enabled && renderMode == SkyMode::Atmosphere
                                    ? MATH::Vec4{stars.intensity, stars.limitingMagnitude, stars.backgroundLuminance,
                                                 std::fmod(TIME::GetUnscaledElapsedSeconds(), 3600.0f)}
                                    : MATH::Vec4{};
    constants.cpu->stars = nightSky;
    constants.cpu->celestialX = {frame.celestialToWorldX.x, frame.celestialToWorldX.y, frame.celestialToWorldX.z, 0.0f};
    constants.cpu->celestialY = {frame.celestialToWorldY.x, frame.celestialToWorldY.y, frame.celestialToWorldY.z, 0.0f};
    constants.cpu->celestialZ = {frame.celestialToWorldZ.x, frame.celestialToWorldZ.y, frame.celestialToWorldZ.z, 0.0f};
    bool atmosphereLutsReady = false;
    if (renderMode == SkyMode::Atmosphere && g.atmosphereFrame.atmosphereEnabled) {
        *constants.atmosphereCpu = ATMOSPHERE::BuildAtmosphereLutConstants(environment.atmosphere, solar);
        // 空の LUT の観測者の高さと、このビューの空気遠近の視線。空気遠近の体積は合成（不透明面へ掛ける）で
        // このビューの Pre-Exposure のまま使う。
        ATMOSPHERE::AtmosphereViewConstants view{};
        view.inverseViewProj = inverseViewProj;
        const MATH::Vec3 position = camera.GetPosition();
        view.cameraPreExposure = {position.x, position.y, position.z, RENDER3D::COLOR::GetActiveViewExposure().x};
        view.params = {ATMOSPHERE::kAerialPerspectiveMaxDistance, 0.0f, 0.0f, 0.0f};
        const RENDER3D::CLOUDS::CloudShadingEstimate cloudShading = RENDER3D::CLOUDS::EstimateCloudShading(environment);
        view.cloudShading = {cloudShading.ambientRadiance.x, cloudShading.ambientRadiance.y,
                             cloudShading.ambientRadiance.z, cloudShading.sunVisibility};
        atmosphereLutsReady =
            g.atmosphereLuts.Dispatch(graphics, constants.atmosphereGpu, *constants.atmosphereCpu, view);
        constants.atmosphereCpu->sunDirection.w = atmosphereLutsReady ? 2.0f : 1.0f;
        if (atmosphereLutsReady) {
            const D3D12_GPU_DESCRIPTOR_HANDLE srv =
                g.atmosphereLuts.DispatchAerialPerspective(graphics, constants.atmosphereGpu, view);
            g.aerialPerspective = {srv, ATMOSPHERE::kAerialPerspectiveMaxDistance, srv.ptr != 0u};
        }
    } else {
        *constants.atmosphereCpu = {};
    }

    g.atmosphereFrame.lutsReady = atmosphereLutsReady;
    g.atmosphereFrame.usingAnalyticFallback = g.atmosphereFrame.atmosphereEnabled && !atmosphereLutsReady;

    cmd->SetGraphicsRootSignature(g.rootSig.Get());
    cmd->SetPipelineState(g.pso.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(0, constants.gpu);
    cmd->SetGraphicsRootConstantBufferView(1, constants.atmosphereGpu);

    ID3D12DescriptorHeap* srvHeap =
        graphics.srvHeap != nullptr ? graphics.srvHeap : RENDER3D::GetTextureResourceSrvHeap();
    if (srvHeap != nullptr) {
        ID3D12DescriptorHeap* heaps[] = {srvHeap};
        cmd->SetDescriptorHeaps(1, heaps);
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = RENDER3D::GetTextureResourceSrvGpuHandle(texture2DResource);
    if (textureSrv.ptr != 0) {
        cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE cubeSrv = RENDER3D::GetTextureResourceSrvGpuHandle(cubemapResource);
    if (cubeSrv.ptr != 0) {
        cmd->SetGraphicsRootDescriptorTable(3, cubeSrv);
    } else if (textureSrv.ptr != 0) {
        // Keep every root descriptor table initialized for Gradient/Texture2D fallback paths.
        cmd->SetGraphicsRootDescriptorTable(3, textureSrv);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE atmosphereSkyViewSrv = textureSrv;
    if (atmosphereLutsReady) {
        atmosphereSkyViewSrv = g.atmosphereLuts.GetSkyViewSrv().gpu;
    }
    if (atmosphereSkyViewSrv.ptr != 0) {
        cmd->SetGraphicsRootDescriptorTable(4, atmosphereSkyViewSrv);
    }

    g.debug.textureValid = (textureSrv.ptr != 0) || (cubeSrv.ptr != 0) || renderMode == SkyMode::Atmosphere;
    g.debug.cubemapLoaded = (RENDER3D::IsTextureResourceValid(cubemapResource) && cubeSrv.ptr != 0);
    g.debug.cubemapResource = cubemapResource;
    g.debug.textureResource = texture2DResource;
    g.debug.cubemapHandle = RENDER3D::GetTextureResourceBackendHandle(cubemapResource);
    g.debug.textureHandle = RENDER3D::GetTextureResourceBackendHandle(texture2DResource);
    g.debug.mode = renderMode;

    g.environmentData.valid = true;
    g.environmentData.hasCubemap = g.debug.cubemapLoaded;
    g.environmentData.usingFallback = g.debug.usingFallback;
    g.environmentData.mode = renderMode;
    g.environmentData.cubemapResource = cubemapResource;
    g.environmentData.textureResource = texture2DResource;
    g.environmentData.cubemapHandle = g.debug.cubemapHandle;
    g.environmentData.textureHandle = g.debug.textureHandle;
    g.environmentData.cubemapSrv = cubeSrv;
    g.environmentData.tint = settings.tint;
    g.environmentData.exposure = ResolveSkyTextureRadianceScale(settings);
    g.environmentData.ambientFromSky = std::max(0.0f, settings.lightIntensity);
    g.environmentData.reflectionIntensity = std::max(0.0f, settings.reflectionIntensity);
    g.environmentData.yaw = settings.yaw;
    g.environmentData.activeSkyAsset = settings.asset;
    g.environmentData.activeTexturePath = g.debug.activeTexturePath;
    g.environmentData.atmosphereEnabled = g.atmosphereFrame.atmosphereEnabled && renderMode == SkyMode::Atmosphere;
    g.environmentData.atmosphereLutsReady = atmosphereLutsReady;
    g.environmentData.atmosphereAnalyticFallback = g.environmentData.atmosphereEnabled && !atmosphereLutsReady;
    g.environmentData.sunDirection = g.atmosphereFrame.sunDirection;
    g.environmentData.sunSourceColor = g.atmosphereFrame.sunSourceColor;
    g.environmentData.sunColor = g.atmosphereFrame.sunColor;
    g.environmentData.sunTransmittance = g.atmosphereFrame.sunTransmittance;
    g.environmentData.sunSourceRadiance = g.atmosphereFrame.sunSourceRadiance;
    g.environmentData.sunRadiance = g.atmosphereFrame.sunRadiance;
    g.environmentData.sunAngularRadius = g.atmosphereFrame.sunAngularRadius;
    g.environmentData.sunGlare = g.atmosphereFrame.sunGlare;
    g.environmentData.atmosphereConstants = *constants.atmosphereCpu;

    if (drawSky) {
        cmd->DrawInstanced(3, 1, 0, 0);
    }
    // 雲の空は前のフレームの雲の段が描いたもの（空の描画は雲より先に走る）。
    const RENDER3D::CLOUDS::CloudSkyView cloudSky = RENDER3D::CLOUDS::GetCloudSkyView();
    IBL::PrepareEnvironmentLighting(graphics, renderMode, settings.asset, *constants.atmosphereCpu,
                                    renderMode == SkyMode::Cubemap ? cubemapResource : texture2DResource,
                                    atmosphereLutsReady ? g.atmosphereLuts.GetSkyViewSrv().gpu
                                                        : D3D12_GPU_DESCRIPTOR_HANDLE{},
                                    cloudSky.srv, cloudSky.version, nightSky.z, TIME::GetFrameContext().frameIndex);
    g.debug.lastRenderSubmitted = true;
    ++g.debug.drawCount;
}

const SkyRendererDebugState& GetDebugState() { return g.debug; }

MATH::Vec3 EstimateSkyAmbientRadiance(const SceneEnvironment& environment) {
    const SkySettings& sky = environment.sky;
    MATH::Vec3 radiance{};
    if (sky.source == SkySource::Atmosphere) {
        // 晴天の空は地表の全天照度のおよそ 2 割を与える。夜は星と大気光の床を残す。
        const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
        const float skyIlluminance = 0.2f * solar.sunRadiance;
        const float nightFloor = environment.stars.enabled ? environment.stars.backgroundLuminance : 0.0f;
        const float luminance = skyIlluminance / 3.14159265f + nightFloor;
        radiance = {luminance * 0.85f, luminance * 0.95f, luminance * 1.15f};
    } else {
        const float luminance = ResolveSkyTextureRadianceScale(sky);
        radiance = {luminance, luminance, luminance};
    }
    const float scale = std::max(0.0f, sky.lightIntensity);
    return {radiance.x * sky.tint.x * scale, radiance.y * sky.tint.y * scale, radiance.z * sky.tint.z * scale};
}

RENDER3D::COLOR::ExposureIllumination EstimateExposureIllumination(const SceneEnvironment& environment) {
    RENDER3D::COLOR::ExposureIllumination illumination{};
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    if (!solar.valid) {
        return illumination;
    }
    float visibility = 1.0f;
    MATH::Vec3 sky = EstimateSkyAmbientRadiance(environment);
    if (RENDER3D::CLOUDS::HasVisibleClouds(environment)) {
        const RENDER3D::CLOUDS::CloudShadingEstimate clouds = RENDER3D::CLOUDS::EstimateCloudShading(environment);
        visibility = clouds.sunVisibility;
        sky = clouds.ambientRadiance;
    }
    const float elevation = (std::max)(solar.sunDirection.y, 0.0f);
    const float skyLuminance = sky.x * 0.2126f + sky.y * 0.7152f + sky.z * 0.0722f;
    const float illuminance = solar.sunRadiance * elevation * visibility + 3.14159265f * skyLuminance;
    // 入射式露出計（ISO 100、平板 C = 250）の EV100。
    illumination.incidentEv100 = std::log2((std::max)(illuminance, 1.0e-6f) * 100.0f / 250.0f);
    // 太陽が 10° より低い間は空の色に合わせた露出のまま（夕焼けの空を白く飛ばさない）、25° で直射の重み 1。
    const float rise = std::clamp((elevation - 0.17365f) / (0.42262f - 0.17365f), 0.0f, 1.0f);
    illumination.directSun = visibility * rise * rise * (3.0f - 2.0f * rise);
    return illumination;
}

const SkyEnvironmentData& GetEnvironmentData() { return g.environmentData; }

const AerialPerspectiveView& GetAerialPerspectiveView() { return g.aerialPerspective; }

D3D12_GPU_DESCRIPTOR_HANDLE GetSkyViewLutSrv() {
    return g.atmosphereFrame.lutsReady ? g.atmosphereLuts.GetSkyViewSrv().gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

} // namespace HIKARI::SKYRENDERER
