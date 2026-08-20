#include "HIKARI_SkyRenderer.h"
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
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereRuntime.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::SKYRENDERER {

    using Microsoft::WRL::ComPtr;

    namespace {
        constexpr uint32_t kSkyConstantDrawCapacityPerFrame = 64u;
        constexpr uint64_t kInvalidSkyConstantFrameSerial =
            (std::numeric_limits<uint64_t>::max)();

        struct SkyCB {
            MATH::Mat4 inverseViewProj{};
            MATH::Vec4 zenithExposure{};
            MATH::Vec4 horizonPower{};
            MATH::Vec4 groundYaw{};
            MATH::Vec4 tintMode{};
            MATH::Vec4 sunDirectionIntensity{};
            MATH::Vec4 sunSizeParams{};
        };

        static_assert(sizeof(SkyCB) == 160u);

        using AtmosphereCB = ATMOSPHERE::AtmosphereLutConstants;

        static_assert(sizeof(AtmosphereCB) == 112u);

        float SanitizeFiniteRange(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            if (!std::isfinite(value)) {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        MATH::Vec3 SanitizeDirection(const MATH::Vec3& value, const MATH::Vec3& fallback) noexcept {
            const auto NormalizeFinite = [](const MATH::Vec3& candidate) {
                if (!std::isfinite(candidate.x) ||
                    !std::isfinite(candidate.y) ||
                    !std::isfinite(candidate.z)) {
                    return MATH::Vec3{ 0.0f, -1.0f, 0.0f };
                }
                const float length = MATH::Length(candidate);
                if (!std::isfinite(length) || length < 1e-5f) {
                    return MATH::Vec3{ 0.0f, -1.0f, 0.0f };
                }
                return candidate * (1.0f / length);
            };
            if (!std::isfinite(value.x) ||
                !std::isfinite(value.y) ||
                !std::isfinite(value.z) ||
                MATH::Length(value) < 1e-5f) {
                return NormalizeFinite(fallback);
            }
            return NormalizeFinite(value);
        }

        MATH::Vec3 SanitizeRadianceColor(
            const MATH::Vec3& value,
            const MATH::Vec3& fallback = { 1.0f, 1.0f, 1.0f }) noexcept {
            return {
                SanitizeFiniteRange(value.x, fallback.x, 0.0f, 64.0f),
                SanitizeFiniteRange(value.y, fallback.y, 0.0f, 64.0f),
                SanitizeFiniteRange(value.z, fallback.z, 0.0f, 64.0f),
            };
        }

        uint64_t HashAtmosphereConstants(
            const AtmosphereCB& constants) noexcept {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&constants);
            uint64_t hash = 1469598103934665603ull;
            for (size_t index = 0u; index < sizeof(constants); ++index) {
                hash ^= static_cast<uint64_t>(bytes[index]);
                hash *= 1099511628211ull;
            }
            return hash == 0u ? 1u : hash;
        }

        SunGlareSettings SanitizeSunGlare(
            const SunGlareSettings& settings) noexcept {
            SunGlareSettings result = settings;
            result.intensity = SanitizeFiniteRange(
                result.intensity, 0.15f, 0.0f, 8.0f);
            result.haloRadius = SanitizeFiniteRange(
                result.haloRadius, 0.08f, 0.0f, 2.0f);
            result.haloFalloff = SanitizeFiniteRange(
                result.haloFalloff, 3.0f, 0.1f, 16.0f);
            result.streakIntensity = SanitizeFiniteRange(
                result.streakIntensity, 0.0f, 0.0f, 8.0f);
            result.streakLength = SanitizeFiniteRange(
                result.streakLength, 0.15f, 0.0f, 2.0f);
            result.occlusionRadius = SanitizeFiniteRange(
                result.occlusionRadius, 0.012f, 0.0f, 2.0f);
            result.edgeFade = SanitizeFiniteRange(
                result.edgeFade, 0.15f, 0.0f, 1.0f);
            result.maxRadiance = SanitizeFiniteRange(
                result.maxRadiance, 4.0f, 0.0f, 64.0f);
            return result;
        }

        float EstimateAtmosphereSunTransmittance(
            const AtmosphereSettings& atmosphere,
            const MATH::Vec3& sunDirection) noexcept {
            // This is the CPU-side source-to-ground term used by the narrow
            // glare/frame contract. It deliberately uses the same optical
            // coefficients and scale heights as the LUT/analytic shaders;
            // the view-path transmittance remains part of sky scattering.
            if (!std::isfinite(sunDirection.y) || sunDirection.y <= -0.05f) {
                return 0.0f;
            }
            const float rayleighHeight = SanitizeFiniteRange(
                atmosphere.rayleighScaleHeight,
                8000.0f,
                1.0f,
                1000000.0f);
            const float mieHeight = SanitizeFiniteRange(
                atmosphere.mieScaleHeight,
                1200.0f,
                1.0f,
                1000000.0f);
            const float ozoneWidth = SanitizeFiniteRange(
                atmosphere.ozoneWidth,
                15000.0f,
                1.0f,
                1000000.0f);
            const float ozoneCenter = SanitizeFiniteRange(
                atmosphere.ozoneCenterHeight,
                25000.0f,
                0.0f,
                1000000.0f);
            const float secant = 1.0f / std::max(
                0.05f,
                SanitizeFiniteRange(sunDirection.y, 1.0f, -1.0f, 1.0f));
            const float ozoneColumn = ozoneWidth * std::exp(
                -ozoneCenter / ozoneWidth);
            const auto coefficient = [](float value, float fallback) {
                return SanitizeFiniteRange(value, fallback, 0.0f, 1.0f);
            };
            const MATH::Vec3 opticalDepth{
                (coefficient(atmosphere.rayleighScattering.x, 5.8e-6f) *
                    rayleighHeight +
                    coefficient(atmosphere.mieScattering.x, 3.996e-6f) *
                        mieHeight +
                    coefficient(atmosphere.ozoneAbsorption.x, 6.5e-7f) *
                        ozoneColumn) *
                    secant,
                (coefficient(atmosphere.rayleighScattering.y, 13.5e-6f) *
                    rayleighHeight +
                    coefficient(atmosphere.mieScattering.y, 3.996e-6f) *
                        mieHeight +
                    coefficient(atmosphere.ozoneAbsorption.y, 1.881e-6f) *
                        ozoneColumn) *
                    secant,
                (coefficient(atmosphere.rayleighScattering.z, 33.1e-6f) *
                    rayleighHeight +
                    coefficient(atmosphere.mieScattering.z, 3.996e-6f) *
                        mieHeight +
                    coefficient(atmosphere.ozoneAbsorption.z, 8.5e-8f) *
                        ozoneColumn) *
                    secant,
            };
            const MATH::Vec3 transmittance{
                std::exp(-std::clamp(opticalDepth.x, 0.0f, 32.0f)),
                std::exp(-std::clamp(opticalDepth.y, 0.0f, 32.0f)),
                std::exp(-std::clamp(opticalDepth.z, 0.0f, 32.0f)),
            };
            return SanitizeFiniteRange(
                transmittance.x * 0.2126f +
                    transmittance.y * 0.7152f +
                    transmittance.z * 0.0722f,
                0.0f,
                0.0f,
                1.0f);
        }

        AtmosphereFrameData BuildAtmosphereFrameDataInternal(
            const SceneEnvironment& environment,
            const Camera3D* camera) {
            AtmosphereFrameData result{};
            const DirectionalLight& sun = environment.directional;
            const MATH::Vec3 directional = SanitizeDirection(
                sun.direction,
                { 0.4f, -1.0f, -0.6f });
            result.sunDirection = directional * -1.0f;
            result.sunColor = SanitizeRadianceColor(sun.color);
            const float directionalIntensity = sun.enabled
                ? SanitizeFiniteRange(sun.intensity, 0.0f, 0.0f, 64.0f)
                : 0.0f;
            result.atmosphereEnabled =
                environment.sky.enabled &&
                environment.sky.mode == SkyMode::Atmosphere &&
                environment.atmosphere.enabled;
            const float solarScale = result.atmosphereEnabled
                ? SanitizeFiniteRange(
                    environment.atmosphere.sunIlluminance,
                    1.0f,
                    0.0f,
                    16.0f) *
                    std::exp2(SanitizeFiniteRange(
                        environment.atmosphere.sunExposure,
                        0.0f,
                        -16.0f,
                        16.0f))
                : 1.0f;
            result.sunRadiance = SanitizeFiniteRange(
                directionalIntensity * solarScale,
                0.0f,
                0.0f,
                256.0f);
            if (result.atmosphereEnabled) {
                result.sunRadiance *= EstimateAtmosphereSunTransmittance(
                    environment.atmosphere,
                    result.sunDirection);
            }
            result.sunAngularRadius = result.atmosphereEnabled
                ? SanitizeFiniteRange(
                    environment.atmosphere.sunAngularRadius,
                    0.004675f,
                    0.0001f,
                    0.25f)
                : SanitizeFiniteRange(
                    environment.sky.sunDiskSize * 0.003f,
                    0.004675f,
                    0.0001f,
                    0.25f);
            result.sunGlare = SanitizeSunGlare(environment.sunGlare);
            result.valid = sun.enabled;
            result.usingAnalyticFallback = result.atmosphereEnabled;
            result.lutsReady = false;

            if (camera != nullptr) {
                const MATH::Vec3 probe =
                    camera->GetPosition() + result.sunDirection * 1000.0f;
                const MATH::Vec4 projected = camera->GetViewProj().TransformPoint({
                    probe.x,
                    probe.y,
                    probe.z,
                    1.0f,
                });
                if (std::isfinite(projected.x) &&
                    std::isfinite(projected.y) &&
                    std::isfinite(projected.w) &&
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
            std::array<uint64_t, GFX::kFrameResourceCount>
                constantFrameSerials{};
            std::array<uint32_t, GFX::kFrameResourceCount>
                constantDrawCursors{};
            std::string loadedTexturePath{};
            std::string loadedCubemapPath{};
            RENDER3D::TextureResourceHandle textureResource{};
            RENDER3D::TextureResourceHandle cubemapResource{};
            RENDER3D::TextureResourceHandle fallbackTextureResource{};
            int textureHandle = -1;
            int cubemapHandle = -1;
            int fallbackTextureHandle = -1;
            SkyRendererDebugState debug{};
            SkyEnvironmentData environmentData{};
            AtmosphereFrameData atmosphereFrame{};
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
                return cpu != nullptr && atmosphereCpu != nullptr &&
                    gpu != 0 && atmosphereGpu != 0;
            }
        };

        bool CreateBuffers(ID3D12Device* device) {
            g.constantStride =
                GFX::AlignD3D12ConstantBufferByteSize(sizeof(SkyCB));
            const UINT64 cbBytes =
                static_cast<UINT64>(g.constantStride) *
                kSkyConstantDrawCapacityPerFrame *
                GFX::kFrameResourceCount;
            g.atmosphereConstantStride =
                GFX::AlignD3D12ConstantBufferByteSize(sizeof(AtmosphereCB));
            const UINT64 atmosphereCbBytes =
                static_cast<UINT64>(g.atmosphereConstantStride) *
                kSkyConstantDrawCapacityPerFrame *
                GFX::kFrameResourceCount;
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(cbBytes);
            if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(g.cb.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create constant buffer failed.");
                return false;
            }
            auto atmosphereCbDesc =
                CD3DX12_RESOURCE_DESC::Buffer(atmosphereCbBytes);
            if (FAILED(device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &atmosphereCbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(g.atmosphereCb.GetAddressOf())))) {
                HIKARI_LOG_ERROR(
                    "[SkyRenderer][ERROR] Create atmosphere constant buffer failed.");
                return false;
            }
            if (FAILED(g.cb->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&g.constantMapped)))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Map constant buffer failed.");
                return false;
            }
            if (FAILED(g.atmosphereCb->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&g.atmosphereConstantMapped)))) {
                HIKARI_LOG_ERROR(
                    "[SkyRenderer][ERROR] Map atmosphere constant buffer failed.");
                return false;
            }
            g.constantFrameSerials.fill(kInvalidSkyConstantFrameSerial);
            g.constantDrawCursors.fill(0u);
            g.debug.constantCapacityPerFrame =
                kSkyConstantDrawCapacityPerFrame;
            return true;
        }

        SkyConstantAllocation AllocateSkyConstants(
            const GFX::Context& graphics) {

            if (g.cb == nullptr ||
                g.constantMapped == nullptr ||
                g.constantStride == 0u ||
                g.atmosphereCb == nullptr ||
                g.atmosphereConstantMapped == nullptr ||
                g.atmosphereConstantStride == 0u) {
                return {};
            }

            const uint32_t frameSlot =
                graphics.frameIndex % GFX::kFrameResourceCount;
            const uint64_t frameSerial =
                TIME::GetFrameContext().frameIndex;
            if (g.constantFrameSerials[frameSlot] != frameSerial) {
                g.constantFrameSerials[frameSlot] = frameSerial;
                g.constantDrawCursors[frameSlot] = 0u;
            }

            const uint32_t drawSlot = g.constantDrawCursors[frameSlot];
            if (drawSlot >= kSkyConstantDrawCapacityPerFrame) {
                ++g.debug.constantOverflowCount;
                HIKARI_LOG_ERROR(
                    "[SkyRenderer][ERROR] Per-frame constant draw capacity exhausted; "
                    "additional sky draw was skipped to preserve GPU resource lifetime.");
                return {};
            }
            ++g.constantDrawCursors[frameSlot];

            const UINT64 allocationIndex =
                static_cast<UINT64>(frameSlot) *
                    kSkyConstantDrawCapacityPerFrame +
                drawSlot;
            const UINT64 byteOffset =
                allocationIndex * g.constantStride;

            g.debug.constantFrameSlot = frameSlot;
            g.debug.constantDrawSlot = drawSlot;
            g.debug.constantDrawCountThisFrame =
                g.constantDrawCursors[frameSlot];
            g.debug.constantCapacityPerFrame =
                kSkyConstantDrawCapacityPerFrame;

            SkyConstantAllocation allocation{};
            allocation.cpu = reinterpret_cast<SkyCB*>(
                g.constantMapped + static_cast<size_t>(byteOffset));
            allocation.gpu =
                g.cb->GetGPUVirtualAddress() + byteOffset;
            const UINT64 atmosphereByteOffset =
                allocationIndex * g.atmosphereConstantStride;
            allocation.atmosphereCpu = reinterpret_cast<AtmosphereCB*>(
                g.atmosphereConstantMapped +
                static_cast<size_t>(atmosphereByteOffset));
            allocation.atmosphereGpu =
                g.atmosphereCb->GetGPUVirtualAddress() + atmosphereByteOffset;
            allocation.frameSlot = frameSlot;
            allocation.drawSlot = drawSlot;
            return allocation;
        }

        bool CreatePipeline(ID3D12Device* device) {
            if (!GFX::SupportsShaderModel6(device)) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Shader Model 6.0 is not supported by this device.");
                return false;
            }
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkyVS.hlsl", "main", GFX::ShaderStage::Vertex, g.vsBlob.GetAddressOf())) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Compile Render3D_SkyVS.hlsl failed.");
                return false;
            }
            if (!GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Render3D_SkyPS.hlsl", "main", GFX::ShaderStage::Pixel, g.psBlob.GetAddressOf())) {
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
            atmosphereSkyViewRange.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

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
            linearClampSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.GetAddressOf(), errBlob.GetAddressOf()))) {
                if (errBlob) OutputDebugStringA(static_cast<const char*>(errBlob->GetBufferPointer()));
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Serialize root signature failed.");
                return false;
            }
            if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(g.rootSig.GetAddressOf())))) {
                HIKARI_LOG_ERROR("[SkyRenderer][ERROR] Create root signature failed.");
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g.rootSig.Get();
            psoDesc.VS = { g.vsBlob->GetBufferPointer(), g.vsBlob->GetBufferSize() };
            psoDesc.PS = { g.psBlob->GetBufferPointer(), g.psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            psoDesc.InputLayout = { nullptr, 0 };
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
            g.fallbackTextureResource = RENDER3D::LoadTextureResource(
                "sky_renderer/fallback_white",
                "HIKARI/white1x1.png");
            g.fallbackTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(g.fallbackTextureResource);
            g.initialized = true;
            g.debug.initialized = true;
            return true;
        }

        RENDER3D::TextureResourceHandle ResolveTexture2D(const SkySettings& settings, SkyManager& skyManager) {
            const SkyAsset* skyAsset = skyManager.FindAsset(settings.skyAsset);
            g.debug.skyAssetFound = (skyAsset != nullptr);
            if (!skyAsset) {
                g.debug.activeTexturePath.clear();
                g.debug.textureValid = false;
                g.debug.usingFallback = true;
                if (!settings.skyAsset.empty()) {
                    HIKARI_LOG_WARN("[SkyRenderer] sky asset not registered: " + settings.skyAsset);
                }
                return {};
            }

            g.debug.activeTexturePath = skyAsset->texturePath;
            if (g.loadedTexturePath != skyAsset->texturePath ||
                !RENDER3D::IsTextureResourceValid(g.textureResource)) {
                RENDER3D::ReleaseTextureResource(g.textureResource);
                g.loadedTexturePath = skyAsset->texturePath;
                g.textureResource = {};
                g.textureHandle = -1;
                if (!skyAsset->texturePath.empty()) {
                    g.textureResource = RENDER3D::LoadTextureResource(
                        "sky_renderer/scene_sky",
                        skyAsset->texturePath);
                    g.textureHandle = RENDER3D::GetTextureResourceBackendHandle(g.textureResource);
                }
            }
            return g.textureResource;
        }

        RENDER3D::TextureResourceHandle ResolveCubemap(const SkySettings& settings, SkyManager& skyManager) {
            const SkyAsset* skyAsset = skyManager.FindAsset(settings.skyAsset);
            g.debug.skyAssetFound = (skyAsset != nullptr);
            if (!skyAsset) {
                g.debug.activeTexturePath.clear();
                g.debug.cubemapLoaded = false;
                g.debug.textureValid = false;
                g.debug.usingFallback = true;
                if (!settings.skyAsset.empty()) {
                    HIKARI_LOG_WARN("[SkyRenderer] sky asset not registered: " + settings.skyAsset);
                }
                return {};
            }

            const std::string path = skyAsset->texturePath;
            g.debug.activeTexturePath = path;
            if (path.empty()) {
                return {};
            }

            if (g.loadedCubemapPath != path ||
                !RENDER3D::IsTextureResourceValid(g.cubemapResource)) {
                RENDER3D::ReleaseTextureResource(g.cubemapResource);
                g.loadedCubemapPath = path;
                g.cubemapResource = RENDER3D::LoadCubemapResource(
                    "sky_renderer/scene_sky_cube",
                    path,
                    skyAsset->textureIsLinear
                        ? RENDER3D::TextureResourceColorSpace::Linear
                        : RENDER3D::TextureResourceColorSpace::Srgb);
                g.cubemapHandle = RENDER3D::GetTextureResourceBackendHandle(g.cubemapResource);
                if (g.cubemapHandle < 0) {
                    std::ostringstream oss;
                    oss << "[SkyRenderer][WARN] Cubemap load failed. Falling back to Gradient sky. skyAsset="
                        << settings.skyAsset << " path=" << path;
                    HIKARI_LOG_WARN(oss.str());
                }
            }
            return g.cubemapResource;
        }
    }

    AtmosphereFrameData BuildAtmosphereFrameData(
        const SceneEnvironment& environment) {
        return BuildAtmosphereFrameDataInternal(environment, nullptr);
    }

    AtmosphereFrameData BuildAtmosphereFrameData(
        const SceneEnvironment& environment,
        const Camera3D& camera) {
        return BuildAtmosphereFrameDataInternal(environment, &camera);
    }

    void Reset() {
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
        g.atmosphereLuts.Reset();
        g.atmosphereFrame = {};
    }
	// キャッシュされたテクスチャを解放し、次回の描画で再ロードするようにする
    void InvalidateSkyTextureCache()
    {
		// 既にロードされているテクスチャがあれば解放する
        RENDER3D::ReleaseTextureResource(g.textureResource);
        // リソース層に登録されたキューブマップを解放する。
        RENDER3D::ReleaseTextureResource(g.cubemapResource);

        g.loadedTexturePath.clear();
        g.loadedCubemapPath.clear();

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

    void Render(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        ModelManager&,
        SkyManager& skyManager) {
        const SkySettings& settings = environment.sky;
        const DirectionalLight& sun = environment.directional;
        g.atmosphereLuts.SetContext(graphics);
        g.atmosphereFrame = BuildAtmosphereFrameData(environment, camera);
        g.debug.initialized = g.initialized;
        g.debug.activeSkyAsset = settings.skyAsset;
        g.debug.activeTexturePath.clear();
        g.debug.usingFallback = false;

        if (!settings.enabled ||
            settings.mode == SkyMode::None ||
            !EnsureInitialized(graphics)) {
            g.debug.initialized = g.initialized;
            return;
        }

        RENDER3D::TextureResourceHandle texture2DResource = g.fallbackTextureResource;
        RENDER3D::TextureResourceHandle cubemapResource{};
        SkyMode renderMode = settings.mode;

        if (settings.mode == SkyMode::Atmosphere &&
            !environment.atmosphere.enabled) {
            g.debug.usingFallback = true;
            renderMode = SkyMode::Gradient;
        }

        AtmosphereSettings atmosphereLutSettings = environment.atmosphere;
        atmosphereLutSettings.enabled =
            renderMode == SkyMode::Atmosphere &&
            settings.enabled &&
            atmosphereLutSettings.enabled;
        (void)g.atmosphereLuts.Configure(atmosphereLutSettings);

        if (settings.mode == SkyMode::Texture2D) {
            const RENDER3D::TextureResourceHandle resolved = ResolveTexture2D(settings, skyManager);
            if (RENDER3D::IsTextureResourceValid(resolved)) {
                texture2DResource = resolved;
            } else {
                g.debug.usingFallback = true;
                renderMode = SkyMode::Gradient;
            }
        } else if (settings.mode == SkyMode::Cubemap) {
            cubemapResource = ResolveCubemap(settings, skyManager);
            if (!RENDER3D::IsTextureResourceValid(cubemapResource)) {
                g.debug.usingFallback = true;
                renderMode = SkyMode::Gradient;
            }
        }

        auto* cmd = graphics.cmdList;
        if (!cmd) {
            return;
        }
        const SkyConstantAllocation constants =
            AllocateSkyConstants(graphics);
        if (!constants.IsValid()) {
            return;
        }
        // PIX 上で sky pass の境界を追いやすくする。
        GFX::PIX::ScopedGpuEvent pixSky(cmd, GFX::PIX::kColorRender, "SkyRenderer.Render");

        const MATH::Vec3 sunDir = SanitizeDirection(
            sun.direction,
            { 0.4f, -1.0f, -0.6f });
        const float sunIntensity = sun.enabled
            ? SanitizeFiniteRange(sun.intensity, 0.0f, 0.0f, 64.0f)
            : 0.0f;

        constants.cpu->inverseViewProj = MATH::Inverse(camera.GetViewProj());
        constants.cpu->zenithExposure = { settings.zenithColor.x, settings.zenithColor.y, settings.zenithColor.z, std::max(0.0f, settings.exposure) };
        constants.cpu->horizonPower = { settings.horizonColor.x, settings.horizonColor.y, settings.horizonColor.z, std::max(0.01f, settings.horizonPower) };
        constants.cpu->groundYaw = { settings.groundColor.x, settings.groundColor.y, settings.groundColor.z, settings.yaw };
        constants.cpu->tintMode = { settings.tint.x, settings.tint.y, settings.tint.z, static_cast<float>(renderMode) };
        constants.cpu->sunDirectionIntensity = { sunDir.x, sunDir.y, sunDir.z, settings.showSunDisk ? settings.sunDiskIntensity * sunIntensity : 0.0f };
        constants.cpu->sunSizeParams = { std::max(0.0001f, settings.sunDiskSize), settings.ambientFromSky, settings.reflectionIntensity, sunIntensity };
        bool atmosphereLutsReady = false;
        if (renderMode == SkyMode::Atmosphere &&
            g.atmosphereFrame.atmosphereEnabled) {
            const AtmosphereSettings& atmosphere = environment.atmosphere;
            constants.atmosphereCpu->planetParams = {
                SanitizeFiniteRange(atmosphere.planetRadius, 6360000.0f, 1.0f, 100000000.0f),
                SanitizeFiniteRange(
                    atmosphere.planetRadius + atmosphere.atmosphereHeight,
                    6440000.0f,
                    1.0f,
                    100000000.0f),
                SanitizeFiniteRange(atmosphere.rayleighScaleHeight, 8000.0f, 1.0f, 1000000.0f),
                SanitizeFiniteRange(atmosphere.mieScaleHeight, 1200.0f, 1.0f, 1000000.0f),
            };
            constants.atmosphereCpu->rayleighScattering = {
                SanitizeFiniteRange(atmosphere.rayleighScattering.x, 5.8e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.rayleighScattering.y, 13.5e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.rayleighScattering.z, 33.1e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(
                    atmosphere.sunAngularRadius,
                    0.004675f,
                    0.0001f,
                    0.25f),
            };
            constants.atmosphereCpu->mieScattering = {
                SanitizeFiniteRange(atmosphere.mieScattering.x, 3.996e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.mieScattering.y, 3.996e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.mieScattering.z, 3.996e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.mieAnisotropy, 0.76f, -0.95f, 0.95f),
            };
            constants.atmosphereCpu->ozoneAbsorption = {
                SanitizeFiniteRange(atmosphere.ozoneAbsorption.x, 6.5e-7f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.ozoneAbsorption.y, 1.881e-6f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.ozoneAbsorption.z, 8.5e-8f, 0.0f, 1.0f),
                SanitizeFiniteRange(atmosphere.ozoneCenterHeight, 25000.0f, 0.0f, 1000000.0f),
            };
            constants.atmosphereCpu->ozoneParams = {
                SanitizeFiniteRange(atmosphere.ozoneWidth, 15000.0f, 1.0f, 1000000.0f),
                SanitizeFiniteRange(
                    atmosphere.aerialPerspectiveStrength,
                    1.0f,
                    0.0f,
                    8.0f),
                SanitizeFiniteRange(atmosphere.sunExposure, 0.0f, -16.0f, 16.0f),
                SanitizeFiniteRange(atmosphere.groundAlbedo, 0.1f, 0.0f, 1.0f),
            };
            constants.atmosphereCpu->sunDirection = {
                g.atmosphereFrame.sunDirection.x,
                g.atmosphereFrame.sunDirection.y,
                g.atmosphereFrame.sunDirection.z,
                1.0f,
            };
            constants.atmosphereCpu->sunColorRadiance = {
                g.atmosphereFrame.sunColor.x,
                g.atmosphereFrame.sunColor.y,
                g.atmosphereFrame.sunColor.z,
                g.atmosphereFrame.sunRadiance,
            };

            if (atmosphere.preferLut) {
                atmosphereLutsReady = g.atmosphereLuts.Dispatch(
                    graphics,
                    constants.atmosphereGpu,
                    HashAtmosphereConstants(*constants.atmosphereCpu));
            }
            constants.atmosphereCpu->sunDirection.w =
                atmosphereLutsReady ? 2.0f : 1.0f;
        } else {
            *constants.atmosphereCpu = {};
        }

        g.atmosphereFrame.lutsReady = atmosphereLutsReady;
        g.atmosphereFrame.usingAnalyticFallback =
            g.atmosphereFrame.atmosphereEnabled && !atmosphereLutsReady;

        cmd->SetGraphicsRootSignature(g.rootSig.Get());
        cmd->SetPipelineState(g.pso.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, constants.gpu);
        cmd->SetGraphicsRootConstantBufferView(1, constants.atmosphereGpu);

        ID3D12DescriptorHeap* srvHeap = graphics.srvHeap != nullptr
            ? graphics.srvHeap
            : RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv =
            RENDER3D::GetTextureResourceSrvGpuHandle(texture2DResource);
        if (textureSrv.ptr != 0) {
            cmd->SetGraphicsRootDescriptorTable(2, textureSrv);
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE cubeSrv =
            RENDER3D::GetTextureResourceSrvGpuHandle(cubemapResource);
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

        g.debug.textureValid = (textureSrv.ptr != 0) || (cubeSrv.ptr != 0) || renderMode == SkyMode::Gradient || renderMode == SkyMode::Atmosphere;
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
        g.environmentData.zenithColor = {
            settings.zenithColor.x * settings.tint.x,
            settings.zenithColor.y * settings.tint.y,
            settings.zenithColor.z * settings.tint.z
        };

        g.environmentData.horizonColor = {
            settings.horizonColor.x * settings.tint.x,
            settings.horizonColor.y * settings.tint.y,
            settings.horizonColor.z * settings.tint.z
        };

        g.environmentData.groundColor = {
            settings.groundColor.x * settings.tint.x,
            settings.groundColor.y * settings.tint.y,
            settings.groundColor.z * settings.tint.z
        };
        g.environmentData.tint = settings.tint;
        g.environmentData.exposure = std::max(0.0f, settings.exposure);
        g.environmentData.ambientFromSky = std::max(0.0f, settings.ambientFromSky);
        g.environmentData.reflectionIntensity = std::max(0.0f, settings.reflectionIntensity);
        g.environmentData.horizonPower = std::max(0.01f, settings.horizonPower);
        g.environmentData.yaw = settings.yaw;
        g.environmentData.activeSkyAsset = settings.skyAsset;
        g.environmentData.activeTexturePath = g.debug.activeTexturePath;
        g.environmentData.atmosphereEnabled =
            g.atmosphereFrame.atmosphereEnabled &&
            renderMode == SkyMode::Atmosphere;
        g.environmentData.atmosphereLutsReady = atmosphereLutsReady;
        g.environmentData.atmosphereAnalyticFallback =
            g.environmentData.atmosphereEnabled && !atmosphereLutsReady;
        g.environmentData.sunDirection = g.atmosphereFrame.sunDirection;
        g.environmentData.sunColor = g.atmosphereFrame.sunColor;
        g.environmentData.sunRadiance = g.atmosphereFrame.sunRadiance;
        g.environmentData.sunAngularRadius = g.atmosphereFrame.sunAngularRadius;
        g.environmentData.sunGlare = g.atmosphereFrame.sunGlare;
        if (g.environmentData.atmosphereEnabled) {
            const AtmosphereSettings& atmosphere = environment.atmosphere;
            g.environmentData.rayleighScattering = atmosphere.rayleighScattering;
            g.environmentData.mieScattering = atmosphere.mieScattering;
            g.environmentData.ozoneAbsorption = atmosphere.ozoneAbsorption;
            g.environmentData.mieAnisotropy = atmosphere.mieAnisotropy;
            g.environmentData.planetRadius = atmosphere.planetRadius;
            g.environmentData.atmosphereHeight = atmosphere.atmosphereHeight;
            g.environmentData.rayleighScaleHeight = atmosphere.rayleighScaleHeight;
            g.environmentData.mieScaleHeight = atmosphere.mieScaleHeight;
            g.environmentData.ozoneCenterHeight = atmosphere.ozoneCenterHeight;
            g.environmentData.ozoneWidth = atmosphere.ozoneWidth;
            g.environmentData.groundAlbedo = atmosphere.groundAlbedo;
            g.environmentData.sunIlluminance = atmosphere.sunIlluminance;
            g.environmentData.sunExposure = atmosphere.sunExposure;
            g.environmentData.aerialPerspectiveStrength =
                atmosphere.aerialPerspectiveStrength;
        }

        cmd->DrawInstanced(3, 1, 0, 0);
        g.debug.lastRenderSubmitted = true;
        ++g.debug.drawCount;
    }

    const SkyRendererDebugState& GetDebugState() {
        return g.debug;
    }

    const SkyEnvironmentData& GetEnvironmentData() {
        return g.environmentData;
    }

    const AtmosphereFrameData& GetAtmosphereFrameData() {
        return g.atmosphereFrame;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetActiveCubemapSrv() {
        return g.environmentData.cubemapSrv;
    }

    int GetActiveCubemapHandle() {
        return g.environmentData.cubemapHandle;
    }

    bool HasActiveCubemap() {
        return g.environmentData.valid && g.environmentData.hasCubemap;
    }

} // namespace HIKARI::SKYRENDERER
