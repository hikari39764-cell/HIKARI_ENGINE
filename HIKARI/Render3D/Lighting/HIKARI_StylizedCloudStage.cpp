#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Lighting/HIKARI_StylizedCloudStage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Assets/Shader/HIKARI_VolumetricShaderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::CLOUDS {

    namespace {

        using Microsoft::WRL::ComPtr;

        constexpr DXGI_FORMAT kCloudColorFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kCloudDepthFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr uint32_t kShapeWidth = 64u;
        constexpr uint32_t kShapeHeight = 64u;
        constexpr uint32_t kShapeDepth = 64u;
        constexpr uint32_t kDetailWidth = 32u;
        constexpr uint32_t kDetailHeight = 32u;
        constexpr uint32_t kDetailDepth = 32u;
        constexpr uint32_t kWeatherWidth = 256u;
        constexpr uint32_t kWeatherHeight = 256u;
        constexpr uint32_t kCloudTextureCount = 6u;
        constexpr uint32_t kNoiseThreadGroupSize = 8u;
        constexpr uint32_t kRtvDescriptorCount = kCloudTextureCount;
        constexpr uint32_t kMaxCloudPrimarySteps = 64u;
        constexpr uint32_t kMaxCloudLightSteps = 8u;
        constexpr uint64_t kInvalidSignature = 0ull;
        static_assert(48u <= kMaxCloudPrimarySteps);
        static_assert(3u <= kMaxCloudLightSteps);

        struct alignas(16) StylizedCloudConstants {
            MATH::Mat4 inverseViewProj{};
            MATH::Mat4 previousViewProj{};
            MATH::Vec4 cameraPositionTime{};
            MATH::Vec4 previousCameraDeltaTime{};
            MATH::Vec4 traceSize{};
            MATH::Vec4 fullSize{};
            MATH::Vec4 layer{};
            MATH::Vec4 shape{};
            MATH::Vec4 motion{};
            MATH::Vec4 lighting{};
            MATH::Vec4 sunDirectionIntensity{};
            MATH::Vec4 brightColor{};
            MATH::Vec4 shadowColor{};
            MATH::Vec4 temporal{};
            MATH::Vec4 quality{};
            MATH::Vec4 ambientLighting{};
            MATH::Vec4 sunColorAnisotropy{};
        };

        static_assert(sizeof(StylizedCloudConstants) == 368u);
        static_assert(offsetof(StylizedCloudConstants, inverseViewProj) == 0u);
        static_assert(offsetof(StylizedCloudConstants, previousViewProj) == 64u);
        static_assert(offsetof(StylizedCloudConstants, cameraPositionTime) == 128u);
        static_assert(offsetof(StylizedCloudConstants, previousCameraDeltaTime) == 144u);
        static_assert(offsetof(StylizedCloudConstants, traceSize) == 160u);
        static_assert(offsetof(StylizedCloudConstants, fullSize) == 176u);
        static_assert(offsetof(StylizedCloudConstants, layer) == 192u);
        static_assert(offsetof(StylizedCloudConstants, shape) == 208u);
        static_assert(offsetof(StylizedCloudConstants, motion) == 224u);
        static_assert(offsetof(StylizedCloudConstants, lighting) == 240u);
        static_assert(offsetof(StylizedCloudConstants, sunDirectionIntensity) == 256u);
        static_assert(offsetof(StylizedCloudConstants, brightColor) == 272u);
        static_assert(offsetof(StylizedCloudConstants, shadowColor) == 288u);
        static_assert(offsetof(StylizedCloudConstants, temporal) == 304u);
        static_assert(offsetof(StylizedCloudConstants, quality) == 320u);
        static_assert(offsetof(StylizedCloudConstants, ambientLighting) == 336u);
        static_assert(offsetof(StylizedCloudConstants, sunColorAnisotropy) == 352u);

        struct alignas(16) NoiseConstants {
            uint32_t size[3] = { 1u, 1u, 1u };
            uint32_t kind = 0u;
        };

        static_assert(sizeof(NoiseConstants) == 16u);

        struct CloudTexture {
            ComPtr<ID3D12Resource> resource{};
            RenderResourceView srv{};
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
            D3D12_RESOURCE_STATES state =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            uint32_t width = 0u;
            uint32_t height = 0u;

            bool IsValid() const noexcept {
                return resource != nullptr && srv.IsValid() && rtv.ptr != 0u &&
                    width != 0u && height != 0u;
            }
        };

        struct NoiseTexture {
            ComPtr<ID3D12Resource> resource{};
            RenderResourceView srv{};
            RenderResourceView uav{};
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            uint32_t width = 0u;
            uint32_t height = 0u;
            uint32_t depth = 1u;

            bool IsValid() const noexcept {
                return resource != nullptr && srv.IsValid() && uav.IsValid() &&
                    width != 0u && height != 0u && depth != 0u;
            }
        };

        struct QualityPolicy {
            uint32_t primarySteps = 32u;
            uint32_t lightSteps = 2u;
            uint32_t resolutionDivisor = 2u;
            float historyWeight = 0.86f;
            uint32_t qualityIndex = 1u;
        };

        struct StageState {
            GFX::Context context{};
            ID3D12Device* device = nullptr;

            ComPtr<ID3D12RootSignature> passRoot{};
            ComPtr<ID3D12RootSignature> noiseRoot{};
            ComPtr<ID3D12RootSignature> solarRoot{};
            ComPtr<ID3D12PipelineState> solarPso{};
            ComPtr<ID3D12PipelineState> tracePso{};
            ComPtr<ID3D12PipelineState> reprojectPso{};
            ComPtr<ID3D12PipelineState> compositePso{};
            std::array<ComPtr<ID3D12PipelineState>, 3u> noisePso{};

            std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads{};
            D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = 0;

            ComPtr<ID3D12DescriptorHeap> rtvHeap{};
            UINT rtvDescriptorSize = 0u;

            NoiseTexture shapeNoise{};
            NoiseTexture detailNoise{};
            NoiseTexture weatherNoise{};
            NoiseTexture solarAtlas{};
            TextureResourceHandle solarTexture{};
            CloudSolarVisibilityFrame solarFrame{};
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};

            CloudTexture currentColor{};
            CloudTexture currentDepth{};
            std::array<CloudTexture, 2u> historyColor{};
            std::array<CloudTexture, 2u> historyDepth{};

            StylizedCloudConstants constants{};
            StylizedCloudStageStats stats{};

            uint32_t historyReadIndex = 0u;
            uint32_t historyWriteIndex = 1u;
            uint64_t preparedFrameIndex = 0u;
            uint64_t lastSignature = kInvalidSignature;
            RenderViewId lastViewId{};
            RenderViewPurpose lastPurpose = RenderViewPurpose::Game;
            bool historyValid = false;
            bool noiseReady = false;
            bool pipelineReady = false;
            uint64_t pipelineRetryFence = 0;
            uint64_t solarRetryFence = 0;
            bool resourcesReady = false;
            bool prepared = false;
            bool hasExecutedFrame = false;
        };

        StageState& State() {
            static StageState state{};
            return state;
        }

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

        MATH::Vec3 SanitizeColor(
            const MATH::Vec3& value,
            const MATH::Vec3& fallback) noexcept {
            return {
                SanitizeFiniteRange(value.x, fallback.x, 0.0f, 16.0f),
                SanitizeFiniteRange(value.y, fallback.y, 0.0f, 16.0f),
                SanitizeFiniteRange(value.z, fallback.z, 0.0f, 16.0f),
            };
        }

        MATH::Vec2 SanitizeWind(const MATH::Vec2& value) noexcept {
            const MATH::Vec2 fallback{ 1.0f, 0.22f };
            const MATH::Vec2 candidate{
                std::isfinite(value.x) ? value.x : fallback.x,
                std::isfinite(value.y) ? value.y : fallback.y,
            };
            const float length = std::hypot(candidate.x, candidate.y);
            if (!std::isfinite(length) || length < 1.0e-5f) {
                return fallback;
            }
            return { candidate.x / length, candidate.y / length };
        }

        bool IsPrimaryPurpose(RenderViewPurpose purpose) noexcept {
            return purpose == RenderViewPurpose::Game ||
                purpose == RenderViewPurpose::EditorScene;
        }

        QualityPolicy ResolveQualityPolicy(StylizedCloudQuality quality) noexcept {
            switch (quality) {
            case StylizedCloudQuality::Low:
                return { 24u, 1u, 4u, 0.78f, 0u };
            case StylizedCloudQuality::High:
                return { 48u, 3u, 2u, 0.90f, 2u };
            case StylizedCloudQuality::Balanced:
            default:
                return { 32u, 2u, 2u, 0.86f, 1u };
            }
        }

        void HashBytes(uint64_t& hash, const void* data, size_t size) noexcept {
            constexpr uint64_t kPrime = 1099511628211ull;
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t index = 0u; index < size; ++index) {
                hash ^= static_cast<uint64_t>(bytes[index]);
                hash *= kPrime;
            }
        }

        void HashFloat(uint64_t& hash, float value) noexcept {
            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            HashBytes(hash, &bits, sizeof(bits));
        }

        void HashVec2(uint64_t& hash, const MATH::Vec2& value) noexcept {
            HashFloat(hash, value.x);
            HashFloat(hash, value.y);
        }

        void HashVec3(uint64_t& hash, const MATH::Vec3& value) noexcept {
            HashFloat(hash, value.x);
            HashFloat(hash, value.y);
            HashFloat(hash, value.z);
        }

        uint64_t BuildSignature(
            const PIPELINE::RenderFrameContext& frame,
            const StylizedCloudSettings& settings,
            StylizedCloudQuality quality,
            const ATMOSPHERE::AtmosphereSolarState& solar,
            uint32_t traceWidth,
            uint32_t traceHeight) noexcept {
            uint64_t hash = 1469598103934665603ull;
            HashBytes(hash, &frame.viewId.value, sizeof(frame.viewId.value));
            const auto purpose = static_cast<uint8_t>(frame.purpose);
            HashBytes(hash, &purpose, sizeof(purpose));
            const auto qualityIndex = static_cast<uint8_t>(quality);
            HashBytes(hash, &qualityIndex, sizeof(qualityIndex));
            HashBytes(hash, &traceWidth, sizeof(traceWidth));
            HashBytes(hash, &traceHeight, sizeof(traceHeight));
            HashFloat(hash, settings.coverage);
            HashFloat(hash, settings.density);
            HashFloat(hash, settings.altitude);
            HashFloat(hash, settings.thickness);
            HashFloat(hash, settings.scale);
            HashVec2(hash, settings.windDirection);
            HashFloat(hash, settings.windSpeed);
            HashFloat(hash, settings.shapeDetail);
            HashFloat(hash, settings.softness);
            HashFloat(hash, settings.lightAbsorption);
            HashFloat(hash, settings.silverLining);
            HashFloat(hash, settings.horizonFade);
            HashVec3(hash, settings.brightColor);
            HashVec3(hash, settings.shadowColor);
            HashVec3(hash, solar.sunDirection);
            HashVec3(hash, solar.sunColor);
            HashFloat(hash, solar.sunRadiance);
            HashBytes(hash, &settings.physicallyBasedLighting, sizeof(settings.physicallyBasedLighting));
            HashFloat(hash, settings.anisotropy);
            if (frame.environment) {
                HashVec3(hash, SKYRENDERER::ResolveAmbientLightColor(*frame.environment));
                HashFloat(hash, frame.environment->ambient.intensity);
            }
            return hash;
        }

        void RetireView(
            StageState& state,
            RenderResourceView& view,
            const char* debugName) {
            if (!view.IsValid()) {
                return;
            }
            const RenderResourceView retired = view;
            view = {};
            if (state.context.deferredReleaseQueue != nullptr &&
                state.context.currentFrameRetireFenceValue != 0u) {
                state.context.deferredReleaseQueue->Enqueue(
                    state.context.currentFrameRetireFenceValue,
                    [retired]() {
                        (void)ReleaseRenderResourceDescriptor(retired);
                    },
                    debugName != nullptr ? debugName :
                        "StylizedCloudStage.Descriptor");
                return;
            }
            (void)ReleaseRenderResourceDescriptor(retired);
        }

        void RetireResource(
            StageState& state,
            ComPtr<ID3D12Resource>& resource,
            const char* debugName) {
            if (!resource) {
                return;
            }
            GFX::RetireD3D12ObjectForFrame(
                resource,
                state.context,
                debugName != nullptr ? debugName :
                    "StylizedCloudStage.Resource");
        }

        D3D12_CPU_DESCRIPTOR_HANDLE RtvAt(
            const StageState& state,
            uint32_t index) noexcept {
            D3D12_CPU_DESCRIPTOR_HANDLE handle{};
            if (state.rtvHeap == nullptr || index >= kRtvDescriptorCount) {
                return handle;
            }
            handle = state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) *
                static_cast<SIZE_T>(state.rtvDescriptorSize);
            return handle;
        }

        void RetireCloudTexture(
            StageState& state,
            CloudTexture& texture,
            const char* debugName) {
            RetireView(state, texture.srv, debugName);
            RetireResource(state, texture.resource, debugName);
            texture.rtv = {};
            texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            texture.format = DXGI_FORMAT_UNKNOWN;
            texture.width = 0u;
            texture.height = 0u;
        }

        void RetireNoiseTexture(
            StageState& state,
            NoiseTexture& texture,
            const char* debugName) {
            RetireView(state, texture.srv, debugName);
            RetireView(state, texture.uav, debugName);
            RetireResource(state, texture.resource, debugName);
            texture.state = D3D12_RESOURCE_STATE_COMMON;
            texture.width = 0u;
            texture.height = 0u;
            texture.depth = 1u;
        }

        void InvalidateHistory(StageState& state) noexcept {
            if (state.historyValid) {
                ++state.stats.historyResetCount;
            }
            state.historyValid = false;
            state.stats.historyValid = false;
            state.historyReadIndex = 0u;
            state.historyWriteIndex = 1u;
        }

        bool CreateRtvHeap(StageState& state) {
            if (state.rtvHeap != nullptr) {
                return true;
            }
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = kRtvDescriptorCount;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (!HIKARI_DX_CHECK(
                    state.device->CreateDescriptorHeap(
                        &desc,
                        IID_PPV_ARGS(state.rtvHeap.GetAddressOf())),
                    "StylizedCloudStage::CreateRtvHeap")) {
                return false;
            }
            state.rtvDescriptorSize = state.device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            state.rtvHeap->SetName(L"HIKARI.StylizedCloudStage.RTVHeap");
            return state.rtvDescriptorSize != 0u;
        }

        bool CreateCloudTexture(
            StageState& state,
            CloudTexture& texture,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format,
            uint32_t rtvIndex,
            const wchar_t* debugName) {
            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
                format,
                width,
                height,
                1u,
                1u,
                1u,
                0u,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            D3D12_CLEAR_VALUE clear{};
            clear.Format = format;
            clear.Color[0] = 0.0f;
            clear.Color[1] = 0.0f;
            clear.Color[2] = 0.0f;
            clear.Color[3] = 0.0f;
            if (!HIKARI_DX_CHECK(
                    state.device->CreateCommittedResource(
                        &heap,
                        D3D12_HEAP_FLAG_NONE,
                        &desc,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        &clear,
                        IID_PPV_ARGS(texture.resource.GetAddressOf())),
                    "StylizedCloudStage::CreateCloudTexture")) {
                return false;
            }
            if (debugName != nullptr) {
                texture.resource->SetName(debugName);
            }
            texture.srv = AllocateTexture2DSrvDescriptor(
                texture.resource.Get(),
                format,
                0u,
                1u);
            texture.rtv = RtvAt(state, rtvIndex);
            if (!texture.srv.IsValid() || texture.rtv.ptr == 0u) {
                RetireView(state, texture.srv, "StylizedCloudStage.CloudSRV");
                texture.resource.Reset();
                texture.rtv = {};
                return false;
            }
            state.device->CreateRenderTargetView(
                texture.resource.Get(),
                nullptr,
                texture.rtv);
            texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            texture.format = format;
            texture.width = width;
            texture.height = height;
            return true;
        }

        bool CreateNoiseTexture(
            StageState& state,
            NoiseTexture& texture,
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            const char* debugName,
            DXGI_FORMAT format = kCloudColorFormat) {
            D3D12_RESOURCE_DESC desc{};
            if (depth > 1u) {
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                desc.Width = width;
                desc.Height = height;
                desc.DepthOrArraySize = static_cast<UINT16>(depth);
            } else {
                desc = CD3DX12_RESOURCE_DESC::Tex2D(
                    format,
                    width,
                    height,
                    1u,
                    1u,
                    1u,
                    0u,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            }
            desc.MipLevels = 1u;
            desc.Format = format;
            desc.SampleDesc.Count = 1u;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            if (!HIKARI_DX_CHECK(
                    state.device->CreateCommittedResource(
                        &heap,
                        D3D12_HEAP_FLAG_NONE,
                        &desc,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr,
                        IID_PPV_ARGS(texture.resource.GetAddressOf())),
                    "StylizedCloudStage::CreateNoiseTexture")) {
                return false;
            }
            if (debugName != nullptr) {
                std::wstring wideName;
                for (const char* cursor = debugName; *cursor != '\0'; ++cursor) {
                    wideName.push_back(static_cast<wchar_t>(*cursor));
                }
                texture.resource->SetName(wideName.c_str());
            }
            if (depth > 1u) {
                texture.srv = AllocateTexture3DSrvDescriptor(
                    texture.resource.Get(),
                    format,
                    0u,
                    1u);
                texture.uav = AllocateTexture3DUavDescriptor(
                    texture.resource.Get(),
                    format,
                    0u,
                    0u,
                    depth);
            } else {
                texture.srv = AllocateTexture2DSrvDescriptor(
                    texture.resource.Get(),
                    format,
                    0u,
                    1u);
                texture.uav = AllocateTexture2DUavDescriptor(
                    texture.resource.Get(),
                    format,
                    0u);
            }
            if (!texture.srv.IsValid() || !texture.uav.IsValid()) {
                RetireView(state, texture.srv, "StylizedCloudStage.NoiseSRV");
                RetireView(state, texture.uav, "StylizedCloudStage.NoiseUAV");
                texture.resource.Reset();
                return false;
            }
            texture.state = D3D12_RESOURCE_STATE_COMMON;
            texture.width = width;
            texture.height = height;
            texture.depth = depth;
            return true;
        }

        bool EnsureNoiseResources(StageState& state) {
            if (state.shapeNoise.IsValid() &&
                state.detailNoise.IsValid() &&
                state.weatherNoise.IsValid()) {
                return true;
            }
            RetireNoiseTexture(state, state.shapeNoise, "StylizedCloudStage.ShapeNoise");
            RetireNoiseTexture(state, state.detailNoise, "StylizedCloudStage.DetailNoise");
            RetireNoiseTexture(state, state.weatherNoise, "StylizedCloudStage.WeatherNoise");
            const bool shapeOk = CreateNoiseTexture(
                state,
                state.shapeNoise,
                kShapeWidth,
                kShapeHeight,
                kShapeDepth,
                "HIKARI.StylizedCloud.ShapeNoise64");
            const bool detailOk = shapeOk && CreateNoiseTexture(
                state,
                state.detailNoise,
                kDetailWidth,
                kDetailHeight,
                kDetailDepth,
                "HIKARI.StylizedCloud.DetailNoise32");
            const bool weatherOk = detailOk && CreateNoiseTexture(
                state,
                state.weatherNoise,
                kWeatherWidth,
                kWeatherHeight,
                1u,
                "HIKARI.StylizedCloud.WeatherNoise256");
            if (!shapeOk || !detailOk || !weatherOk) {
                ++state.stats.resourceFailureCount;
                RetireNoiseTexture(state, state.shapeNoise, "StylizedCloudStage.ShapeNoise");
                RetireNoiseTexture(state, state.detailNoise, "StylizedCloudStage.DetailNoise");
                RetireNoiseTexture(state, state.weatherNoise, "StylizedCloudStage.WeatherNoise");
                state.noiseReady = false;
                return false;
            }
            state.noiseReady = false;
            state.stats.noiseReady = false;
            return true;
        }

        bool EnsureCloudTargets(
            StageState& state,
            uint32_t renderWidth,
            uint32_t renderHeight,
            uint32_t divisor) {
            const uint32_t traceWidth =
                (renderWidth + divisor - 1u) / divisor;
            const uint32_t traceHeight =
                (renderHeight + divisor - 1u) / divisor;
            if (state.resourcesReady &&
                state.stats.renderWidth == renderWidth &&
                state.stats.renderHeight == renderHeight &&
                state.stats.traceWidth == traceWidth &&
                state.stats.traceHeight == traceHeight) {
                return true;
            }
            RetireCloudTexture(state, state.currentColor, "StylizedCloudStage.CurrentColor");
            RetireCloudTexture(state, state.currentDepth, "StylizedCloudStage.CurrentDepth");
            for (uint32_t index = 0u; index < 2u; ++index) {
                RetireCloudTexture(
                    state,
                    state.historyColor[index],
                    "StylizedCloudStage.HistoryColor");
                RetireCloudTexture(
                    state,
                    state.historyDepth[index],
                    "StylizedCloudStage.HistoryDepth");
            }
            // RTV descriptors are CPU handles into the heap, not immutable
            // resource views. Retire the whole heap before recreating the
            // six target descriptors; reusing the old heap would let an
            // in-flight command observe descriptors rewritten for the new
            // resolution.
            if (state.rtvHeap != nullptr) {
                GFX::RetireD3D12ObjectForFrame(
                    state.rtvHeap,
                    state.context,
                    "StylizedCloudStage.RtvHeap.Resize");
                state.rtvDescriptorSize = 0u;
            }
            if (!CreateRtvHeap(state) ||
                !CreateCloudTexture(
                    state,
                    state.currentColor,
                    traceWidth,
                    traceHeight,
                    kCloudColorFormat,
                    0u,
                    L"HIKARI.StylizedCloud.CurrentColor") ||
                !CreateCloudTexture(
                    state,
                    state.currentDepth,
                    traceWidth,
                    traceHeight,
                    kCloudDepthFormat,
                    1u,
                    L"HIKARI.StylizedCloud.CurrentDepth") ||
                !CreateCloudTexture(
                    state,
                    state.historyColor[0],
                    traceWidth,
                    traceHeight,
                    kCloudColorFormat,
                    2u,
                    L"HIKARI.StylizedCloud.HistoryColor0") ||
                !CreateCloudTexture(
                    state,
                    state.historyDepth[0],
                    traceWidth,
                    traceHeight,
                    kCloudDepthFormat,
                    3u,
                    L"HIKARI.StylizedCloud.HistoryDepth0") ||
                !CreateCloudTexture(
                    state,
                    state.historyColor[1],
                    traceWidth,
                    traceHeight,
                    kCloudColorFormat,
                    4u,
                    L"HIKARI.StylizedCloud.HistoryColor1") ||
                !CreateCloudTexture(
                    state,
                    state.historyDepth[1],
                    traceWidth,
                    traceHeight,
                    kCloudDepthFormat,
                    5u,
                    L"HIKARI.StylizedCloud.HistoryDepth1")) {
                ++state.stats.resourceFailureCount;
                RetireCloudTexture(state, state.currentColor, "StylizedCloudStage.CurrentColor");
                RetireCloudTexture(state, state.currentDepth, "StylizedCloudStage.CurrentDepth");
                for (uint32_t index = 0u; index < 2u; ++index) {
                    RetireCloudTexture(
                        state,
                        state.historyColor[index],
                        "StylizedCloudStage.HistoryColor");
                    RetireCloudTexture(
                        state,
                        state.historyDepth[index],
                        "StylizedCloudStage.HistoryDepth");
                }
                state.resourcesReady = false;
                InvalidateHistory(state);
                return false;
            }
            state.stats.renderWidth = renderWidth;
            state.stats.renderHeight = renderHeight;
            state.stats.traceWidth = traceWidth;
            state.stats.traceHeight = traceHeight;
            state.stats.workingSetBytes =
                static_cast<uint64_t>(traceWidth) *
                static_cast<uint64_t>(traceHeight) *
                36ull +
                static_cast<uint64_t>(kShapeWidth) * kShapeHeight *
                    kShapeDepth * 8ull +
                static_cast<uint64_t>(kDetailWidth) * kDetailHeight *
                    kDetailDepth * 8ull +
                static_cast<uint64_t>(kWeatherWidth) * kWeatherHeight * 8ull;
            ++state.stats.resizeCount;
            state.resourcesReady = true;
            InvalidateHistory(state);
            return true;
        }

        void TransitionCloudTexture(
            ID3D12GraphicsCommandList* cmd,
            CloudTexture& texture,
            D3D12_RESOURCE_STATES nextState) {
            if (cmd == nullptr || texture.resource == nullptr ||
                texture.state == nextState) {
                return;
            }
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texture.resource.Get(),
                texture.state,
                nextState);
            cmd->ResourceBarrier(1u, &barrier);
            texture.state = nextState;
        }

        void TransitionNoiseTexture(
            ID3D12GraphicsCommandList* cmd,
            NoiseTexture& texture,
            D3D12_RESOURCE_STATES nextState) {
            if (cmd == nullptr || texture.resource == nullptr ||
                texture.state == nextState) {
                return;
            }
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texture.resource.Get(),
                texture.state,
                nextState);
            cmd->ResourceBarrier(1u, &barrier);
            texture.state = nextState;
        }

        bool CreatePassRootSignature(StageState& state) {
            std::array<D3D12_DESCRIPTOR_RANGE, 8u> ranges{};
            std::array<D3D12_ROOT_PARAMETER, 9u> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[0].Descriptor.ShaderRegister = 0u;
            for (uint32_t index = 0u; index < ranges.size(); ++index) {
                ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                ranges[index].NumDescriptors = 1u;
                ranges[index].BaseShaderRegister = index;
                ranges[index].OffsetInDescriptorsFromTableStart =
                    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                parameters[index + 1u].ParameterType =
                    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[index + 1u].ShaderVisibility =
                    D3D12_SHADER_VISIBILITY_PIXEL;
                parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
                parameters[index + 1u].DescriptorTable.pDescriptorRanges =
                    &ranges[index];
            }
            std::array<D3D12_STATIC_SAMPLER_DESC, 3u> samplers{};
            samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
            samplers[0].ShaderRegister = 0u;
            samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            samplers[1] = samplers[0];
            samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[1].ShaderRegister = 1u;
            samplers[2] = samplers[1];
            samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            samplers[2].ShaderRegister = 2u;
            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters = static_cast<UINT>(parameters.size());
            rootDesc.pParameters = parameters.data();
            rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
            rootDesc.pStaticSamplers = samplers.data();
            rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            if (!HIKARI_DX_CHECK(
                    D3D12SerializeRootSignature(
                        &rootDesc,
                        D3D_ROOT_SIGNATURE_VERSION_1,
                        signature.GetAddressOf(),
                        errors.GetAddressOf()),
                    "StylizedCloudStage::SerializePassRootSignature")) {
                if (errors) {
                    DEBUGLOG::PushRenderError(
                        static_cast<const char*>(errors->GetBufferPointer()));
                }
                return false;
            }
            return HIKARI_DX_CHECK(
                state.device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(state.passRoot.GetAddressOf())),
                "StylizedCloudStage::CreatePassRootSignature");
        }

        bool CreateNoiseRootSignature(StageState& state) {
            std::array<D3D12_DESCRIPTOR_RANGE, 2u> ranges{};
            std::array<D3D12_ROOT_PARAMETER, 3u> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            parameters[0].Descriptor.ShaderRegister = 0u;
            for (uint32_t index = 0u; index < ranges.size(); ++index) {
                ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                ranges[index].NumDescriptors = 1u;
                ranges[index].BaseShaderRegister = index;
                ranges[index].OffsetInDescriptorsFromTableStart =
                    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                parameters[index + 1u].ParameterType =
                    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[index + 1u].ShaderVisibility =
                    D3D12_SHADER_VISIBILITY_ALL;
                parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
                parameters[index + 1u].DescriptorTable.pDescriptorRanges =
                    &ranges[index];
            }
            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters = static_cast<UINT>(parameters.size());
            rootDesc.pParameters = parameters.data();
            rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            if (!HIKARI_DX_CHECK(
                    D3D12SerializeRootSignature(
                        &rootDesc,
                        D3D_ROOT_SIGNATURE_VERSION_1,
                        signature.GetAddressOf(),
                        errors.GetAddressOf()),
                    "StylizedCloudStage::SerializeNoiseRootSignature")) {
                if (errors) {
                    DEBUGLOG::PushRenderError(
                        static_cast<const char*>(errors->GetBufferPointer()));
                }
                return false;
            }
            return HIKARI_DX_CHECK(
                state.device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(state.noiseRoot.GetAddressOf())),
                "StylizedCloudStage::CreateNoiseRootSignature");
        }

        bool PrepareFrameUploads(StageState& state) {
            if (state.context.frameIndex >= GFX::kFrameResourceCount ||
                state.context.currentFrameRetireFenceValue == 0) return false;
            auto& uploads = state.uploads[state.context.frameIndex];
            if (!uploads.IsInitialized() && !uploads.Init(state.device, 4096)) return false;
            uploads.ResetForFrame(state.context.currentFrameRetireFenceValue);
            return true;
        }

        bool CreateSolarRoot(StageState& state) {
            std::array<CD3DX12_DESCRIPTOR_RANGE,4> ranges;
            std::array<CD3DX12_ROOT_PARAMETER,6> parameters;
            parameters[0].InitAsConstantBufferView(0); parameters[1].InitAsConstantBufferView(1);
            for(UINT i=0;i<4;++i) {
                ranges[i].Init(i==3 ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i==3 ? 0 : i);
                parameters[i+2].InitAsDescriptorTable(1,&ranges[i]);
            }
            CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP);
            const CD3DX12_ROOT_SIGNATURE_DESC desc(UINT(parameters.size()),parameters.data(),1,&sampler);
            ComPtr<ID3DBlob> signature;
            return SUCCEEDED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,nullptr)) &&
                SUCCEEDED(state.device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&state.solarRoot)));
        }

        bool EnsurePipeline(StageState& state) {
            if (state.pipelineReady) {
                return true;
            }
            if (state.context.currentFrameRetireFenceValue < state.pipelineRetryFence) return false;
            state.pipelineRetryFence = state.context.currentFrameRetireFenceValue + 60;
            if (state.device == nullptr ||
                !GFX::SupportsShaderModel6(state.device) ||
                !CreatePassRootSignature(state) ||
                !CreateNoiseRootSignature(state)) {
                return false;
            }
            namespace SHADER=ASSETS::SHADER;
            using Program=SHADER::VolumetricShaderProgram;
            const auto deviceCaps=GFX::QueryShaderDeviceCapabilities(state.device);
            const SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel,deviceCaps.rootSignatureVersion,deviceCaps.featureMask};
            const auto vertexShader=SHADER::ResolveVolumetricShaderEntry(Program::CloudPass,"FullscreenVS",SHADER::ShaderStage::Vertex,caps);
            const auto traceShader=SHADER::ResolveVolumetricShaderEntry(Program::CloudPass,"RenderCloudPS",SHADER::ShaderStage::Pixel,caps);
            const auto reprojectShader=SHADER::ResolveVolumetricShaderEntry(Program::CloudPass,"ReprojectCloudPS",SHADER::ShaderStage::Pixel,caps);
            const auto compositeShader=SHADER::ResolveVolumetricShaderEntry(Program::CloudPass,"CompositeCloudPS",SHADER::ShaderStage::Pixel,caps);
            const std::array noiseShader{
                SHADER::ResolveVolumetricShaderEntry(Program::CloudNoise,"GenerateStylizedCloudShapeCS",SHADER::ShaderStage::Compute,caps),
                SHADER::ResolveVolumetricShaderEntry(Program::CloudNoise,"GenerateStylizedCloudDetailCS",SHADER::ShaderStage::Compute,caps),
                SHADER::ResolveVolumetricShaderEntry(Program::CloudNoise,"GenerateStylizedCloudWeatherCS",SHADER::ShaderStage::Compute,caps)};
            if (!vertexShader || !traceShader || !reprojectShader || !compositeShader ||
                !noiseShader[0] || !noiseShader[1] || !noiseShader[2]) {
                DEBUGLOG::PushRenderError(
                    "[StylizedCloudStage][ERROR] cooked cloud shader unavailable or incompatible.");
                state.passRoot.Reset();
                state.noiseRoot.Reset();
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = state.passRoot.Get();
            desc.VS = { vertexShader.lease.GetDxil().data(), vertexShader.lease.GetDxil().size() };
            desc.PS = { traceShader.lease.GetDxil().data(), traceShader.lease.GetDxil().size() };
            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.InputLayout = { nullptr, 0u };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 2u;
            desc.RTVFormats[0] = kCloudColorFormat;
            desc.RTVFormats[1] = kCloudDepthFormat;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1u;
            if (!HIKARI_DX_CHECK(
                    state.device->CreateGraphicsPipelineState(
                        &desc,
                        IID_PPV_ARGS(state.tracePso.GetAddressOf())),
                    "StylizedCloudStage::CreateTracePSO")) {
                return false;
            }
            desc.PS = {
                reprojectShader.lease.GetDxil().data(),
                reprojectShader.lease.GetDxil().size()
            };
            if (!HIKARI_DX_CHECK(
                    state.device->CreateGraphicsPipelineState(
                        &desc,
                        IID_PPV_ARGS(state.reprojectPso.GetAddressOf())),
                    "StylizedCloudStage::CreateReprojectPSO")) {
                return false;
            }

            desc.PS = {
                compositeShader.lease.GetDxil().data(),
                compositeShader.lease.GetDxil().size()
            };
            desc.NumRenderTargets = 1u;
            desc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
            desc.RTVFormats[0] = kCloudColorFormat;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            if (!HIKARI_DX_CHECK(
                    state.device->CreateGraphicsPipelineState(
                        &desc,
                        IID_PPV_ARGS(state.compositePso.GetAddressOf())),
                    "StylizedCloudStage::CreateCompositePSO")) {
                return false;
            }

            for (uint32_t index = 0u; index < noiseShader.size(); ++index) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC noiseDesc{};
                noiseDesc.pRootSignature = state.noiseRoot.Get();
                noiseDesc.CS = {
                    noiseShader[index].lease.GetDxil().data(),
                    noiseShader[index].lease.GetDxil().size()
                };
                if (!HIKARI_DX_CHECK(
                        state.device->CreateComputePipelineState(
                            &noiseDesc,
                            IID_PPV_ARGS(state.noisePso[index].GetAddressOf())),
                        "StylizedCloudStage::CreateNoisePSO")) {
                    return false;
                }
            }
            state.pipelineReady = true;
            return true;
        }

        bool EnsureSolarPipeline(StageState& state) {
            if (state.solarPso) return true;
            if (state.context.currentFrameRetireFenceValue < state.solarRetryFence) return false;
            state.solarRetryFence = state.context.currentFrameRetireFenceValue + 60;
            if (!CreateSolarRoot(state)) return false;
            namespace SHADER = ASSETS::SHADER;
            const auto deviceCaps = GFX::QueryShaderDeviceCapabilities(state.device);
            const SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion, deviceCaps.featureMask};
            const auto shader = SHADER::ResolveVolumetricShaderEntry(SHADER::VolumetricShaderProgram::CloudPass,
                "SunTransmittanceCS", SHADER::ShaderStage::Compute, caps);
            if (!shader) return false;
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = state.solarRoot.Get();
            desc.CS = {shader.lease.GetDxil().data(), shader.lease.GetDxil().size()};
            return HIKARI_DX_CHECK(state.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&state.solarPso)),
                "CloudSolar.CreatePSO");
        }

        void BindPassInputs(
            ID3D12GraphicsCommandList* cmd,
            const StageState& state,
            D3D12_GPU_DESCRIPTOR_HANDLE historyColor,
            D3D12_GPU_DESCRIPTOR_HANDLE historyDepth,
            D3D12_GPU_DESCRIPTOR_HANDLE currentColor,
            D3D12_GPU_DESCRIPTOR_HANDLE currentDepth) {
            cmd->SetGraphicsRootDescriptorTable(1u, state.shapeNoise.srv.gpu);
            cmd->SetGraphicsRootDescriptorTable(2u, state.detailNoise.srv.gpu);
            cmd->SetGraphicsRootDescriptorTable(3u, state.weatherNoise.srv.gpu);
            cmd->SetGraphicsRootDescriptorTable(4u, currentColor);
            cmd->SetGraphicsRootDescriptorTable(5u, currentDepth);
            cmd->SetGraphicsRootDescriptorTable(6u, historyColor);
            cmd->SetGraphicsRootDescriptorTable(7u, historyDepth);
            cmd->SetGraphicsRootDescriptorTable(8u, state.sceneDepthSrv);
        }

        void SetCloudViewport(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height) {
            D3D12_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MaxDepth = 1.0f;
            const D3D12_RECT scissor{
                0,
                0,
                static_cast<LONG>(width),
                static_cast<LONG>(height)
            };
            cmd->RSSetViewports(1u, &viewport);
            cmd->RSSetScissorRects(1u, &scissor);
        }

        D3D12_GPU_VIRTUAL_ADDRESS WriteNoiseConstants(StageState& state, uint32_t kind, const NoiseTexture& target) {
            const NoiseConstants constants{{target.width, target.height, target.depth}, kind};
            const auto allocation = state.uploads[state.context.frameIndex].Allocate(sizeof(constants));
            if (!allocation) return 0;
            std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
            return allocation.gpuAddress;
        }

        bool GenerateNoise(StageState& state) {
            if (state.noiseReady || !state.noisePso[0] ||
                !state.noisePso[1] || !state.noisePso[2] || !state.noiseRoot ||
                !state.context.cmdList || !state.context.srvHeap) {
                return state.noiseReady;
            }
            ID3D12GraphicsCommandList* cmd = state.context.cmdList;
            ID3D12DescriptorHeap* heaps[] = { state.context.srvHeap };
            cmd->SetDescriptorHeaps(1u, heaps);
            cmd->SetComputeRootSignature(state.noiseRoot.Get());
            const auto DispatchNoise = [&](NoiseTexture& target, uint32_t kind) {
                const auto address = WriteNoiseConstants(state, kind, target);
                if (!address) return false;
                TransitionNoiseTexture(
                    cmd,
                    target,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cmd->SetPipelineState(state.noisePso[kind].Get());
                cmd->SetComputeRootConstantBufferView(0u, address);
                const D3D12_GPU_DESCRIPTOR_HANDLE volumeUav = target.depth > 1u
                    ? target.uav.gpu
                    : state.shapeNoise.uav.gpu;
                cmd->SetComputeRootDescriptorTable(1u, volumeUav);
                cmd->SetComputeRootDescriptorTable(2u, state.weatherNoise.uav.gpu);
                const uint32_t groupsX =
                    (target.width + kNoiseThreadGroupSize - 1u) /
                    kNoiseThreadGroupSize;
                const uint32_t groupsY =
                    (target.height + kNoiseThreadGroupSize - 1u) /
                    kNoiseThreadGroupSize;
                const uint32_t groupsZ = target.depth > 1u
                    ? target.depth
                    : 1u;
                cmd->Dispatch(groupsX, groupsY, groupsZ);
                const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(target.resource.Get());
                cmd->ResourceBarrier(1u, &barrier);
                TransitionNoiseTexture(
                    cmd,
                    target,
                    D3D12_RESOURCE_STATE_GENERIC_READ);
                ++state.stats.noiseDispatchCount;
                return true;
            };
            if (!DispatchNoise(state.shapeNoise, 0u) || !DispatchNoise(state.detailNoise, 1u) ||
                !DispatchNoise(state.weatherNoise, 2u)) return false;
            state.noiseReady = true;
            state.stats.noiseReady = true;
            return true;
        }

        bool BuildConstants(
            StageState& state,
            const PIPELINE::RenderFrameContext& frame,
            const TEMPORAL::TemporalFrameState& temporalFrame,
            const StylizedCloudSettings& settings,
            const QualityPolicy& policy,
            const ATMOSPHERE::AtmosphereSolarState& solar,
            uint64_t signature) {
            const auto& camera = temporalFrame.camera;
            const MATH::Vec3 cameraPosition{
                camera.cameraPos.x,
                camera.cameraPos.y,
                camera.cameraPos.z
            };
            const MATH::Vec3 previousPosition{
                camera.prevCameraPos.x,
                camera.prevCameraPos.y,
                camera.prevCameraPos.z
            };
            const MATH::Vec2 wind = SanitizeWind(settings.windDirection);
            const float unscaledTime = SanitizeFiniteRange(
                TIME::GetUnscaledElapsedSeconds(),
                0.0f,
                -1000000.0f,
                1000000.0f);
            const float unscaledDt = SanitizeFiniteRange(
                TIME::GetFrameContext().unscaledDt,
                0.0f,
                0.0f,
                0.25f);
            const bool useHistory = state.historyValid &&
                temporalFrame.historyValid &&
                !temporalFrame.resetHistory &&
                camera.previousValid &&
                state.lastSignature == signature;

            StylizedCloudConstants constants{};
            constants.inverseViewProj = camera.invViewProj;
            constants.previousViewProj = camera.prevViewProj;
            constants.cameraPositionTime = {
                cameraPosition.x,
                cameraPosition.y,
                cameraPosition.z,
                unscaledTime
            };
            constants.previousCameraDeltaTime = {
                previousPosition.x,
                previousPosition.y,
                previousPosition.z,
                unscaledDt
            };
            constants.traceSize = {
                static_cast<float>(state.stats.traceWidth),
                static_cast<float>(state.stats.traceHeight),
                1.0f / static_cast<float>((std::max)(1u, state.stats.traceWidth)),
                1.0f / static_cast<float>((std::max)(1u, state.stats.traceHeight))
            };
            constants.fullSize = {
                static_cast<float>(frame.renderWidth),
                static_cast<float>(frame.renderHeight),
                1.0f / static_cast<float>((std::max)(1u, frame.renderWidth)),
                1.0f / static_cast<float>((std::max)(1u, frame.renderHeight))
            };
            const float altitude = SanitizeFiniteRange(
                settings.altitude,
                180.0f,
                1.0f,
                100000.0f);
            const float thickness = SanitizeFiniteRange(
                settings.thickness,
                90.0f,
                1.0f,
                100000.0f);
            const float planetRadius = (std::max)(
                600000.0f,
                altitude + thickness + 1000.0f);
            // Keep the shell interval sample bandwidth tied to authored
            // shape scale. A fixed multi-kilometre cap would let a low
            // elevation ray cross several 3D features per step and recreate
            // the diagonal bands this stage replaces.
            const float shapeScale = SanitizeFiniteRange(
                settings.scale,
                140.0f,
                1.0f,
                100000.0f);
            const float maxDistance = std::clamp(
                (std::max)(
                    altitude + thickness + 1.0f,
                    shapeScale * static_cast<float>(policy.primarySteps) * 0.55f),
                0.0f,
                500000.0f);
            constants.layer = {
                altitude,
                thickness,
                planetRadius,
                maxDistance
            };
            constants.shape = {
                SanitizeFiniteRange(settings.coverage, 0.46f, 0.0f, 1.0f),
                SanitizeFiniteRange(settings.density, 1.10f, 0.0f, 16.0f),
                SanitizeFiniteRange(settings.scale, 140.0f, 1.0f, 100000.0f),
                SanitizeFiniteRange(settings.shapeDetail, 0.55f, 0.0f, 1.0f)
            };
            constants.motion = {
                wind.x,
                wind.y,
                SanitizeFiniteRange(settings.windSpeed, 6.0f, -1000.0f, 1000.0f),
                SanitizeFiniteRange(settings.softness, 0.28f, 0.0f, 1.0f)
            };
            constants.lighting = {
                SanitizeFiniteRange(settings.lightAbsorption, 0.72f, 0.0f, 16.0f),
                SanitizeFiniteRange(settings.silverLining, 0.42f, 0.0f, 1.0f),
                SanitizeFiniteRange(settings.horizonFade, 0.62f, 0.0f, 1.0f),
                useHistory ? policy.historyWeight : 0.0f
            };
            constants.sunDirectionIntensity = {
                solar.sunDirection.x,
                solar.sunDirection.y,
                solar.sunDirection.z,
                solar.sunRadiance
            };
            const MATH::Vec3 bright = SanitizeColor(
                settings.brightColor,
                MATH::Vec3{ 1.0f, 0.985f, 0.94f });
            const MATH::Vec3 shadow = SanitizeColor(
                settings.shadowColor,
                MATH::Vec3{ 0.48f, 0.67f, 0.86f });
            constants.brightColor = {
                bright.x * solar.sunColor.x, bright.y * solar.sunColor.y,
                bright.z * solar.sunColor.z, 1.0f };
            if (settings.physicallyBasedLighting)
                constants.brightColor = { std::clamp(bright.x, 0.0f, 1.0f), std::clamp(bright.y, 0.0f, 1.0f),
                    std::clamp(bright.z, 0.0f, 1.0f), 1.0f };
            const auto ambient = frame.environment
                ? SKYRENDERER::ResolveAmbientLightColor(*frame.environment) *
                    SanitizeFiniteRange(frame.environment->ambient.intensity, 0.0f, 0.0f, 1000000.0f)
                : MATH::Vec3{};
            constants.ambientLighting = { ambient.x, ambient.y, ambient.z, settings.physicallyBasedLighting ? 1.0f : 0.0f };
            constants.sunColorAnisotropy = { solar.sunColor.x, solar.sunColor.y, solar.sunColor.z,
                SanitizeFiniteRange(settings.anisotropy, 0.7f, -0.8f, 0.8f) };
            constants.shadowColor = { shadow.x, shadow.y, shadow.z, 1.0f };
            constants.temporal = {
                static_cast<float>(temporalFrame.frameIndex & 0xffffu),
                useHistory ? 1.0f : 0.0f,
                1.0f,
                2.4f
            };
            constants.quality = {
                static_cast<float>(policy.primarySteps),
                static_cast<float>(policy.lightSteps),
                static_cast<float>(policy.resolutionDivisor),
                static_cast<float>(policy.qualityIndex)
            };
            state.constants = constants;
            const auto allocation = state.uploads[state.context.frameIndex].Allocate(sizeof(constants));
            if (!allocation) return false;
            std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
            state.constantsAddress = allocation.gpuAddress;
            state.stats.primarySteps = policy.primarySteps;
            state.stats.lightSteps = policy.lightSteps;
            state.stats.resolutionDivisor = policy.resolutionDivisor;
            state.stats.qualityIndex = policy.qualityIndex;
            state.stats.historyValid = useHistory;
            return true;
        }

        bool ExecuteTrace(StageState& state) {
            ID3D12GraphicsCommandList* cmd = state.context.cmdList;
            ID3D12DescriptorHeap* heaps[] = { state.context.srvHeap };
            cmd->SetDescriptorHeaps(1u, heaps);
            TransitionCloudTexture(
                cmd,
                state.currentColor,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            TransitionCloudTexture(
                cmd,
                state.currentDepth,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
                state.currentColor.rtv,
                state.currentDepth.rtv
            };
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            cmd->ClearRenderTargetView(rtvs[0], clearColor, 0u, nullptr);
            cmd->ClearRenderTargetView(rtvs[1], clearColor, 0u, nullptr);
            cmd->OMSetRenderTargets(2u, rtvs, FALSE, nullptr);
            SetCloudViewport(cmd, state.stats.traceWidth, state.stats.traceHeight);
            cmd->SetGraphicsRootSignature(state.passRoot.Get());
            cmd->SetPipelineState(state.tracePso.Get());
            cmd->SetGraphicsRootConstantBufferView(0u, state.constantsAddress);
            const uint32_t historyIndex = state.historyReadIndex;
            BindPassInputs(
                cmd,
                state,
                state.historyColor[historyIndex].srv.gpu,
                state.historyDepth[historyIndex].srv.gpu,
                state.historyColor[historyIndex].srv.gpu,
                state.historyDepth[historyIndex].srv.gpu);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->IASetVertexBuffers(0u, 0u, nullptr);
            cmd->IASetIndexBuffer(nullptr);
            cmd->DrawInstanced(3u, 1u, 0u, 0u);
            TransitionCloudTexture(
                cmd,
                state.currentColor,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            TransitionCloudTexture(
                cmd,
                state.currentDepth,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            ++state.stats.traceDispatchCount;
            return true;
        }

        bool ExecuteReproject(StageState& state) {
            ID3D12GraphicsCommandList* cmd = state.context.cmdList;
            const uint32_t readIndex = state.historyReadIndex;
            const uint32_t writeIndex = state.historyWriteIndex;
            const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
                state.historyColor[writeIndex].rtv,
                state.historyDepth[writeIndex].rtv
            };
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            TransitionCloudTexture(
                cmd,
                state.historyColor[writeIndex],
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            TransitionCloudTexture(
                cmd,
                state.historyDepth[writeIndex],
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->ClearRenderTargetView(rtvs[0], clearColor, 0u, nullptr);
            cmd->ClearRenderTargetView(rtvs[1], clearColor, 0u, nullptr);
            cmd->OMSetRenderTargets(2u, rtvs, FALSE, nullptr);
            SetCloudViewport(cmd, state.stats.traceWidth, state.stats.traceHeight);
            cmd->SetGraphicsRootSignature(state.passRoot.Get());
            cmd->SetPipelineState(state.reprojectPso.Get());
            cmd->SetGraphicsRootConstantBufferView(0u, state.constantsAddress);
            BindPassInputs(
                cmd,
                state,
                state.historyColor[readIndex].srv.gpu,
                state.historyDepth[readIndex].srv.gpu,
                state.currentColor.srv.gpu,
                state.currentDepth.srv.gpu);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->IASetVertexBuffers(0u, 0u, nullptr);
            cmd->IASetIndexBuffer(nullptr);
            cmd->DrawInstanced(3u, 1u, 0u, 0u);
            TransitionCloudTexture(
                cmd,
                state.historyColor[writeIndex],
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            TransitionCloudTexture(
                cmd,
                state.historyDepth[writeIndex],
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            state.historyReadIndex = writeIndex;
            state.historyWriteIndex = writeIndex ^ 1u;
            state.historyValid = true;
            ++state.stats.temporalDispatchCount;
            return true;
        }

        bool ExecuteComposite(
            StageState& state,
            const PIPELINE::RenderFrameContext& frame) {
            if (!frame.targetAccess.BeginDepthRead()) {
                return false;
            }
            bool depthReadActive = true;
            ID3D12GraphicsCommandList* cmd = state.context.cmdList;
            ID3D12DescriptorHeap* heaps[] = { state.context.srvHeap };
            cmd->SetDescriptorHeaps(1u, heaps);
            SetCloudViewport(cmd, frame.renderWidth, frame.renderHeight);
            cmd->SetGraphicsRootSignature(state.passRoot.Get());
            cmd->SetPipelineState(state.compositePso.Get());
            cmd->SetGraphicsRootConstantBufferView(0u, state.constantsAddress);
            const uint32_t resolvedIndex = state.historyReadIndex;
            BindPassInputs(
                cmd,
                state,
                state.historyColor[resolvedIndex].srv.gpu,
                state.historyDepth[resolvedIndex].srv.gpu,
                state.currentColor.srv.gpu,
                state.currentDepth.srv.gpu);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->IASetVertexBuffers(0u, 0u, nullptr);
            cmd->IASetIndexBuffer(nullptr);
            cmd->DrawInstanced(3u, 1u, 0u, 0u);
            if (depthReadActive) {
                frame.targetAccess.EndDepthRead();
                depthReadActive = false;
            }
            if (!frame.targetAccess.Rebind()) {
                return false;
            }
            ++state.stats.compositeDispatchCount;
            return true;
        }

        void ReleaseGpuState(StageState& state) {
            if(state.solarTexture) ReleaseTextureResource(state.solarTexture);
            state.solarTexture={}; state.solarFrame={};
            RetireNoiseTexture(state,state.solarAtlas,"CloudSolar.Atlas");
            GFX::RetireD3D12ObjectForFrame(state.solarPso,state.context,"CloudSolar.PSO");
            GFX::RetireD3D12ObjectForFrame(state.solarRoot,state.context,"CloudSolar.Root");
            RetireNoiseTexture(state, state.shapeNoise, "StylizedCloudStage.ShapeNoise");
            RetireNoiseTexture(state, state.detailNoise, "StylizedCloudStage.DetailNoise");
            RetireNoiseTexture(state, state.weatherNoise, "StylizedCloudStage.WeatherNoise");
            RetireCloudTexture(state, state.currentColor, "StylizedCloudStage.CurrentColor");
            RetireCloudTexture(state, state.currentDepth, "StylizedCloudStage.CurrentDepth");
            for (uint32_t index = 0u; index < 2u; ++index) {
                RetireCloudTexture(
                    state,
                    state.historyColor[index],
                    "StylizedCloudStage.HistoryColor");
                RetireCloudTexture(
                    state,
                    state.historyDepth[index],
                    "StylizedCloudStage.HistoryDepth");
            }
            for (auto& uploads : state.uploads) uploads.Finalize(&state.context);
            state.constantsAddress = 0;
            GFX::RetireD3D12ObjectForFrame(
                state.tracePso,
                state.context,
                "StylizedCloudStage.TracePSO");
            GFX::RetireD3D12ObjectForFrame(
                state.reprojectPso,
                state.context,
                "StylizedCloudStage.ReprojectPSO");
            GFX::RetireD3D12ObjectForFrame(
                state.compositePso,
                state.context,
                "StylizedCloudStage.CompositePSO");
            for (ComPtr<ID3D12PipelineState>& noisePso : state.noisePso) {
                GFX::RetireD3D12ObjectForFrame(
                    noisePso,
                    state.context,
                    "StylizedCloudStage.NoisePSO");
            }
            GFX::RetireD3D12ObjectForFrame(
                state.passRoot,
                state.context,
                "StylizedCloudStage.PassRootSignature");
            GFX::RetireD3D12ObjectForFrame(
                state.noiseRoot,
                state.context,
                "StylizedCloudStage.NoiseRootSignature");
            GFX::RetireD3D12ObjectForFrame(
                state.rtvHeap,
                state.context,
                "StylizedCloudStage.RtvHeap");
            state.rtvDescriptorSize = 0u;
            state.device = nullptr;
            state.pipelineReady = false;
            state.pipelineRetryFence = state.solarRetryFence = 0;
            state.stats.solarVisibilityReady = false;
            state.stats.solarBytes = 0;
            state.resourcesReady = false;
            state.noiseReady = false;
            state.prepared = false;
            state.hasExecutedFrame = false;
            state.historyValid = false;
            state.lastSignature = kInvalidSignature;
        }

    } // namespace

    CloudSolarVisibilityFrame PrepareCloudSolarVisibilityFrame(const PIPELINE::RenderFrameContext& frame,
        const TEMPORAL::TemporalFrameState& temporalFrame, const StylizedCloudSettings& settings, StylizedCloudQuality quality) {
        auto& state=State();
        state.stats.solarVisibilityReady=false;
        const auto& graphics=frame.graphics;
        if(!settings.enabled || !settings.physicallyBasedLighting || settings.density<=0 || settings.coverage<=0 || !frame.environment ||
            !IsPrimaryPurpose(frame.purpose) || frame.viewId!=kPrimaryRenderViewId || !temporalFrame.camera.valid ||
            !graphics.device || !graphics.cmdList || !graphics.srvHeap || !graphics.frameFence ||
            graphics.frameIndex>=GFX::kFrameResourceCount || !graphics.currentFrameRetireFenceValue) return {};
        const auto solar=ATMOSPHERE::BuildAtmosphereSolarState(*frame.environment,settings.altitude);
        if(solar.sunRadiance<=0 || !MATH::IsFinite(temporalFrame.camera.cameraPos)) return {};
        if(state.device && (state.device!=graphics.device || state.context.deviceGeneration!=graphics.deviceGeneration ||
            state.context.frameFence!=graphics.frameFence)) ReleaseGpuState(state);
        state.context=graphics; state.device=graphics.device;
        UpdateRenderResourceDescriptorPoolContext(graphics);
        if(state.solarFrame && state.solarFrame.fenceValue==graphics.currentFrameRetireFenceValue) {
            state.stats.solarVisibilityReady=true; return state.solarFrame;
        }
        const UINT resolution=quality==StylizedCloudQuality::Low ? 64 : quality==StylizedCloudQuality::High ? 128 : 96;
        const UINT steps=quality==StylizedCloudQuality::Low ? 16 : quality==StylizedCloudQuality::High ? 32 : 24;
        if(state.solarAtlas.IsValid() && state.solarAtlas.width!=resolution*8) {
            if(graphics.frameFence->GetCompletedValue()<state.solarFrame.fenceValue) return {};
            if(state.solarTexture) ReleaseTextureResource(state.solarTexture);
            state.solarTexture={}; state.solarFrame={}; RetireNoiseTexture(state,state.solarAtlas,"CloudSolar.Resize");
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R32G32_FLOAT};
        if(FAILED(graphics.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) ||
            !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) return {};
        if(!PrepareFrameUploads(state) || !EnsurePipeline(state) || !EnsureSolarPipeline(state) || !EnsureNoiseResources(state) || !GenerateNoise(state)) return {};
        if(!state.solarAtlas.IsValid()) {
            if(!CreateNoiseTexture(state,state.solarAtlas,resolution*8,((steps+8)/8)*resolution,1,"CloudSolar.Transmittance",DXGI_FORMAT_R32G32_FLOAT)) return {};
            state.solarTexture=RegisterTextureResourceFromNative(state.solarAtlas.resource.Get(),DXGI_FORMAT_R32G32_FLOAT);
            if(!IsTextureResourceValid(state.solarTexture) || GetTextureResourceSrvDescriptorIndex(state.solarTexture)>=GFX::DESCRIPTOR::kUserSrvCount) {
                if(state.solarTexture) ReleaseTextureResource(state.solarTexture);
                state.solarTexture={}; RetireNoiseTexture(state,state.solarAtlas,"CloudSolar.RegisterFailed"); return {};
            }
        }
        const auto policy=ResolveQualityPolicy(quality);
        if(!BuildConstants(state,frame,temporalFrame,settings,policy,solar,0)) return {};
        LIGHTING::CloudSolarVisibilityGpuData data;
        const auto direction=solar.sunDirection;
        const auto right=MATH::Normalize(MATH::Cross(std::abs(direction.y)<.99f ? MATH::Vec3{0,1,0} : MATH::Vec3{0,0,1},direction));
        const auto up=MATH::Cross(direction,right);
        const MATH::Vec3 camera{temporalFrame.camera.cameraPos.x,temporalFrame.camera.cameraPos.y,temporalFrame.camera.cameraPos.z};
        constexpr float extent=1024;
        const float cell=2*extent/float(resolution);
        const auto origin=right*(std::floor(MATH::Dot(camera,right)/cell)*cell)+up*(std::floor(MATH::Dot(camera,up)/cell)*cell)+direction*MATH::Dot(camera,direction);
        data.originDescriptor={origin.x,origin.y,origin.z,float(GetTextureResourceSrvDescriptorIndex(state.solarTexture)+1)};
        data.rightExtent={right.x,right.y,right.z,extent}; data.upResolution={up.x,up.y,up.z,float(resolution)};
        data.directionSteps={direction.x,direction.y,direction.z,float(steps)};
        const auto allocation=state.uploads[graphics.frameIndex].Allocate(sizeof(data)); if(!allocation) return {};
        std::memcpy(allocation.cpuAddress,&data,sizeof(data));
        auto* commands=graphics.cmdList;
        GFX::GPU_PROFILE::ScopedGpuTimer timer(commands,GFX::GPU_PROFILE::Pass::CloudSolarVisibility);
        ID3D12DescriptorHeap* heaps[]{graphics.srvHeap}; commands->SetDescriptorHeaps(1,heaps);
        TransitionNoiseTexture(commands,state.solarAtlas,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->SetComputeRootSignature(state.solarRoot.Get()); commands->SetPipelineState(state.solarPso.Get());
        commands->SetComputeRootConstantBufferView(0,state.constantsAddress); commands->SetComputeRootConstantBufferView(1,allocation.gpuAddress);
        commands->SetComputeRootDescriptorTable(2,state.shapeNoise.srv.gpu); commands->SetComputeRootDescriptorTable(3,state.detailNoise.srv.gpu);
        commands->SetComputeRootDescriptorTable(4,state.weatherNoise.srv.gpu); commands->SetComputeRootDescriptorTable(5,state.solarAtlas.uav.gpu);
        commands->Dispatch((resolution+7)/8,(resolution+7)/8,1);
        TransitionNoiseTexture(commands,state.solarAtlas,D3D12_RESOURCE_STATE_GENERIC_READ);
        state.stats.solarVisibilityReady=true; state.stats.solarResolution=resolution; state.stats.solarSteps=steps;
        const auto desc=state.solarAtlas.resource->GetDesc(); state.stats.solarBytes=graphics.device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
        state.solarFrame={data,state.solarAtlas.srv.gpu,graphics.currentFrameRetireFenceValue};
        return state.solarFrame;
    }

    bool ExecuteStylizedCloudStage(
        const PIPELINE::RenderFrameContext& frame,
        const TEMPORAL::TemporalFrameState& temporalFrame,
        const StylizedCloudSettings& settings,
        StylizedCloudQuality quality) {
        StageState& state = State();
        state.stats.requested = settings.enabled;
        state.stats.active = false;
        state.stats.rendered = false;
        state.stats.temporalRendered = false;
        state.stats.compositeRendered = false;
        if (state.hasExecutedFrame &&
            temporalFrame.frameIndex > state.preparedFrameIndex + 1u) {
            InvalidateHistory(state);
        }
        state.prepared = false;

        if (!settings.enabled || settings.density <= 0.0f ||
            (settings.physicallyBasedLighting && settings.coverage <= 0.0f) ||
            !IsPrimaryPurpose(frame.purpose) ||
            !(frame.viewId == kPrimaryRenderViewId) ||
            frame.graphics.device == nullptr ||
            frame.graphics.cmdList == nullptr ||
            frame.graphics.srvHeap == nullptr ||
            frame.renderWidth == 0u || frame.renderHeight == 0u ||
            frame.scene.depth.resource == nullptr ||
            !frame.scene.depth.IsSampleable() ||
            frame.scene.readOnlyDsv.ptr == 0u ||
            frame.targetAccess.beginDepthRead == nullptr ||
            frame.targetAccess.endDepthRead == nullptr ||
            frame.targetAccess.rebind == nullptr ||
            !temporalFrame.camera.valid ||
            !MATH::IsFinite(temporalFrame.camera.invViewProj) ||
            !MATH::IsFinite(temporalFrame.camera.prevViewProj)) {
            InvalidateHistory(state);
            return false;
        }

        // 雲層高度の大気透過率を使う。地表色の clamp や独立した太陽を持たない。
        const auto solar = frame.environment != nullptr
            ? ATMOSPHERE::BuildAtmosphereSolarState(*frame.environment, settings.altitude)
            : ATMOSPHERE::AtmosphereSolarState{};
        QualityPolicy policy = ResolveQualityPolicy(quality);
        if (settings.physicallyBasedLighting)
            policy.lightSteps = (std::min)(kMaxCloudLightSteps, policy.lightSteps * 2u + 2u);
        const uint32_t traceWidth =
            (frame.renderWidth + policy.resolutionDivisor - 1u) /
            policy.resolutionDivisor;
        const uint32_t traceHeight =
            (frame.renderHeight + policy.resolutionDivisor - 1u) /
            policy.resolutionDivisor;
        const uint64_t signature = BuildSignature(
            frame,
            settings,
            quality,
            solar,
            traceWidth,
            traceHeight);

        if (state.device != nullptr && (state.device != frame.graphics.device ||
            state.context.deviceGeneration != frame.graphics.deviceGeneration ||
            state.context.frameFence != frame.graphics.frameFence)) {
            ReleaseGpuState(state);
        }
        state.context = frame.graphics;
        state.device = frame.graphics.device;
        UpdateRenderResourceDescriptorPoolContext(state.context);
        if (state.lastSignature != kInvalidSignature &&
            state.lastSignature != signature) {
            InvalidateHistory(state);
        }
        if (state.lastViewId.value != 0u &&
            (!(state.lastViewId == frame.viewId) ||
                state.lastPurpose != frame.purpose)) {
            InvalidateHistory(state);
        }
        if (frame.cameraCut || temporalFrame.resetHistory) {
            InvalidateHistory(state);
        }
        if (!PrepareFrameUploads(state) || !EnsurePipeline(state) ||
            !EnsureNoiseResources(state) ||
            !EnsureCloudTargets(
                state,
                frame.renderWidth,
                frame.renderHeight,
                policy.resolutionDivisor)) {
            InvalidateHistory(state);
            return false;
        }

        state.stats.requested = true;
        state.stats.pipelineReady = state.pipelineReady;
        state.stats.resourcesReady = state.resourcesReady;
        state.preparedFrameIndex = temporalFrame.frameIndex;
        if (!state.noiseReady && !GenerateNoise(state)) {
            InvalidateHistory(state);
            return false;
        }
        if (!BuildConstants(
                state,
                frame,
                temporalFrame,
                settings,
                policy,
                solar,
                signature)) {
            InvalidateHistory(state);
            return false;
        }

        GFX::PIX::ScopedGpuEvent event(
            state.context.cmdList,
            GFX::PIX::kColorPost,
            "StylizedCloudStage");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            state.context.cmdList,
            GFX::GPU_PROFILE::Pass::StylizedClouds);
        if (!frame.targetAccess.BeginDepthRead()) return false;
        state.sceneDepthSrv = frame.scene.depth.srv;
        if (!ExecuteTrace(state) || !ExecuteReproject(state)) {
            frame.targetAccess.EndDepthRead();
            (void)frame.targetAccess.Rebind();
            InvalidateHistory(state);
            return false;
        }
        state.stats.rendered = true;
        state.stats.temporalRendered = true;
        if (!ExecuteComposite(state, frame)) {
            frame.targetAccess.EndDepthRead();
            (void)frame.targetAccess.Rebind();
            InvalidateHistory(state);
            return false;
        }
        state.stats.compositeRendered = true;
        state.stats.active = true;
        state.stats.historyValid = true;
        if (!state.hasExecutedFrame) {
            HIKARI_LOG_INFO(
                "[StylizedCloudStage] active trace=" +
                std::to_string(state.stats.traceWidth) + "x" +
                std::to_string(state.stats.traceHeight) +
                " primarySteps=" + std::to_string(state.stats.primarySteps) +
                " lightSteps=" + std::to_string(state.stats.lightSteps) +
                " workingSetBytes=" +
                std::to_string(state.stats.workingSetBytes));
        }
        state.lastSignature = signature;
        state.lastViewId = frame.viewId;
        state.lastPurpose = frame.purpose;
        state.prepared = true;
        state.hasExecutedFrame = true;
        return true;
    }

    bool IsStylizedCloudStageActive() {
        return State().prepared && State().stats.active;
    }

    const StylizedCloudStageStats& GetStylizedCloudStageStats() {
        return State().stats;
    }

    void ShutdownStylizedCloudStage() {
        StageState& state = State();
        ReleaseGpuState(state);
        state.context = {};
        state.stats = {};
        state.historyReadIndex = 0u;
        state.historyWriteIndex = 1u;
        state.preparedFrameIndex = 0u;
        state.lastViewId = {};
        state.lastPurpose = RenderViewPurpose::Game;
        state.constants = {};
    }

} // namespace HIKARI::RENDER3D::CLOUDS
