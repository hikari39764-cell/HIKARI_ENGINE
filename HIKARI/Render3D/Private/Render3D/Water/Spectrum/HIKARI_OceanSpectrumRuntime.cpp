#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_UploadTraffic.h"
#include "Render3D/Water/Spectrum/HIKARI_OceanSpectrumRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ResourceStateTracker.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::WATER::SPECTRUM {
namespace {
using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kSpectrumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr uint32_t kWorkgroupSize = 8u;
constexpr uint32_t kJonswapPeakEnhancement = 3u;
constexpr float kGravity = 9.81f;
constexpr float kDepth = 54.0f;
constexpr float kWindSpeed = 11.5f;
constexpr float kFetch = 120000.0f;
constexpr float kWindAngle = -0.48f;
constexpr float kSwell = 0.38f;

struct Float2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Float4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

static_assert(sizeof(Float4) == 16u);

struct GpuBuffer {
    ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    uint32_t elementCount = 0u;
    uint64_t byteSize = 0u;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;
};

struct GpuTexture {
    ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    RenderResourceView uav{};
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t byteSize = 0u;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct CascadeState {
    OceanSpectrumCascadeDesc desc{};
    GpuBuffer initialSpectrum{};
    GpuBuffer waveData{};
    // [ping][field], where field 0 stores X displacement/height and
    // field 1 stores Z displacement/height slope.
    GpuTexture fields[2][2]{};
    GpuTexture displacement{};
    GpuTexture slope{};
};

bool HasSameExecutionContext(const GFX::Context& lhs, const GFX::Context& rhs) noexcept {
    return lhs.device == rhs.device && lhs.queue == rhs.queue && lhs.cmdList == rhs.cmdList &&
           lhs.srvHeap == rhs.srvHeap && lhs.resourceStates == rhs.resourceStates &&
           lhs.deferredReleaseQueue == rhs.deferredReleaseQueue && lhs.frameFence == rhs.frameFence &&
           lhs.currentFrameRetireFenceValue == rhs.currentFrameRetireFenceValue;
}

bool HasSameSubmissionOwner(const GFX::Context& context, const GFX::Context& owner) noexcept {
    return context.device == owner.device && context.queue == owner.queue && context.srvHeap == owner.srvHeap;
}

} // namespace

struct OceanSpectrumRuntime::State {
    GFX::Context context{};
    GFX::Context ownerContext{};
    OceanSpectrumRuntimeConfig config{};
    ComPtr<ID3D12RootSignature> evolutionRoot{};
    ComPtr<ID3D12RootSignature> stockhamRoot{};
    ComPtr<ID3D12RootSignature> resolveRoot{};
    ComPtr<ID3D12PipelineState> evolutionPso{};
    ComPtr<ID3D12PipelineState> stockhamPso{};
    ComPtr<ID3D12PipelineState> resolvePso{};
    GpuBuffer twiddleTable{};
    std::array<CascadeState, kOceanSpectrumCascadeCount> cascades{};
    std::array<OceanSpectrumCascadeOutput, kOceanSpectrumCascadeCount> outputs{};
    std::array<RenderResourceView, kOceanSpectrumCascadeCount * 2u> outputTableDescriptors{};
    OceanSpectrumOutputDescriptorTable outputTable{};
    OceanSpectrumRuntimeStats stats{};
    uint64_t lastFrameIndex = 0u;
    uint64_t contextConflictFrameIndex = 0u;
    bool hasRecordedFrame = false;
    bool contextConflict = false;
    bool initialized = false;
};

namespace {

struct XorShift32 {
    uint32_t state = 1u;

    explicit XorShift32(uint32_t seed) : state(seed == 0u ? 1u : seed) {}

    uint32_t NextBits() {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    float NextUnit() { return static_cast<float>(NextBits()) / 4294967296.0f; }

    Float2 Gaussian() {
        const float u = (std::max)(NextUnit(), 1.0e-7f);
        const float v = NextUnit();
        const float radius = std::sqrt(-2.0f * std::log(u));
        const float angle = 6.28318530717958647692f * v;
        return {radius * std::cos(angle), radius * std::sin(angle)};
    }
};

struct SpectralCpuData {
    std::vector<Float4> initialSpectrum{};
    std::vector<Float4> waveData{};
};

bool IsFinitePositive(float value) { return std::isfinite(value) && value > 0.0f; }

bool IsSupportedConfig(const OceanSpectrumRuntimeConfig& config) {
    if (config.resolution != kOceanSpectrumMinResolution && config.resolution != kOceanSpectrumMaxResolution) {
        return false;
    }

    for (const OceanSpectrumCascadeDesc& cascade : config.cascades) {
        if (!IsFinitePositive(cascade.lengthScale) || !std::isfinite(cascade.cutoffLow) ||
            !std::isfinite(cascade.cutoffHigh) || cascade.cutoffLow < 0.0f || cascade.cutoffHigh <= cascade.cutoffLow ||
            !std::isfinite(cascade.amplitudeScale) || cascade.amplitudeScale < 0.0f ||
            !std::isfinite(cascade.choppiness) || cascade.choppiness < 0.0f || !std::isfinite(cascade.secondaryScale) ||
            cascade.secondaryScale < 0.0f) {
            return false;
        }
    }
    return true;
}

float WrapAngle(float value) {
    while (value > 3.14159265358979323846f) {
        value -= 6.28318530717958647692f;
    }
    while (value < -3.14159265358979323846f) {
        value += 6.28318530717958647692f;
    }
    return value;
}

float SpectrumNormalisationFactor(float spread) {
    const float s2 = spread * spread;
    const float s3 = s2 * spread;
    const float s4 = s3 * spread;
    return spread < 5.0f ? -0.000564f * s4 + 0.00776f * s3 - 0.044f * s2 + 0.192f * spread + 0.163f
                         : -4.8e-8f * s4 + 1.07e-5f * s3 - 9.53e-4f * s2 + 5.9e-2f * spread + 0.393f;
}

SpectralCpuData BuildSpectralCpuData(uint32_t size, const OceanSpectrumCascadeDesc& config) {
    SpectralCpuData result{};
    const size_t pixelCount = static_cast<size_t>(size) * size;
    result.initialSpectrum.resize(pixelCount);
    result.waveData.resize(pixelCount);
    std::vector<Float2> initialK(pixelCount);
    XorShift32 random(config.seed);

    const float deltaK = 6.28318530717958647692f / config.lengthScale;
    const float alpha = 0.076f * std::pow(kGravity * kFetch / (kWindSpeed * kWindSpeed), -0.22f);
    const float peakOmega = 22.0f * std::pow(kWindSpeed * kFetch / (kGravity * kGravity), -0.33f);

    for (uint32_t y = 0u; y < size; ++y) {
        for (uint32_t x = 0u; x < size; ++x) {
            const int nx = static_cast<int>(x) - static_cast<int>(size / 2u);
            const int nz = static_cast<int>(y) - static_cast<int>(size / 2u);
            const float kx = static_cast<float>(nx) * deltaK;
            const float kz = static_cast<float>(nz) * deltaK;
            const float kLength = std::sqrt(kx * kx + kz * kz);
            const size_t pixel = static_cast<size_t>(y) * size + x;

            result.initialSpectrum[pixel] = {};
            if (kLength < config.cutoffLow || kLength > config.cutoffHigh || kLength <= 1.0e-5f) {
                result.waveData[pixel] = {0.0f, 1.0f, 0.0f, 0.0f};
                continue;
            }

            const float kh = (std::min)(kLength * kDepth, 20.0f);
            const float tanhKh = std::tanh(kh);
            const float omega = std::sqrt(kGravity * kLength * tanhKh);
            const float sechSquared = 1.0f - tanhKh * tanhKh;
            const float frequencyDerivative =
                kGravity * (kDepth * kLength * sechSquared + tanhKh) / (std::max)(omega * 2.0f, 1.0e-5f);
            const float omegaH = omega * std::sqrt(kDepth / kGravity);
            const float tma = omegaH <= 1.0f ? 0.5f * omegaH * omegaH
                                             : (omegaH < 2.0f ? 1.0f - 0.5f * (2.0f - omegaH) * (2.0f - omegaH) : 1.0f);
            const float sigma = omega <= peakOmega ? 0.07f : 0.09f;
            const float peakDistance = (omega - peakOmega) / (std::max)(sigma * peakOmega, 1.0e-5f);
            const float peakShape = std::exp(-0.5f * peakDistance * peakDistance);
            const float peakRatio = peakOmega / omega;
            const float jonswap = tma * alpha * kGravity * kGravity / std::pow(omega, 5.0f) *
                                  std::exp(-1.25f * std::pow(peakRatio, 4.0f)) *
                                  std::pow(static_cast<float>(kJonswapPeakEnhancement), peakShape);
            const float theta = WrapAngle(std::atan2(kz, kx) - kWindAngle);
            const float omegaRatio = omega / peakOmega;
            const float spreadPower =
                ((omega > peakOmega ? 9.77f * std::pow(omegaRatio, -2.5f) : 6.97f * std::pow(omegaRatio, 5.0f)) +
                 16.0f * std::tanh((std::min)(omegaRatio, 20.0f)) * kSwell * kSwell) *
                0.58f;
            const float focusedDirection = SpectrumNormalisationFactor(spreadPower) *
                                           std::pow(std::abs(std::cos(theta * 0.5f)), 2.0f * spreadPower);
            const float broadDirection =
                2.0f / 3.14159265358979323846f * std::pow((std::max)(std::cos(theta), 0.0f), 2.0f);
            const float direction = focusedDirection * 0.68f + broadDirection * 0.32f;
            const float shortWaveFade = std::exp(-0.00016f * kLength * kLength);
            float spectralDensity = jonswap * direction * shortWaveFade;

            if (config.secondaryScale > 0.0f) {
                const float swellWindSpeed = 8.4f;
                const float swellFetch = 310000.0f;
                const float swellPeakOmega =
                    22.0f * std::pow(swellWindSpeed * swellFetch / (kGravity * kGravity), -0.33f);
                const float swellAlpha =
                    0.076f * std::pow(kGravity * swellFetch / (swellWindSpeed * swellWindSpeed), -0.22f);
                const float swellSigma = omega <= swellPeakOmega ? 0.07f : 0.09f;
                const float swellPeakDistance =
                    (omega - swellPeakOmega) / (std::max)(swellSigma * swellPeakOmega, 1.0e-5f);
                const float swellPeakShape = std::exp(-0.5f * swellPeakDistance * swellPeakDistance);
                const float swellPeakRatio = swellPeakOmega / omega;
                const float swellSpectrum = tma * swellAlpha * kGravity * kGravity / std::pow(omega, 5.0f) *
                                            std::exp(-1.25f * std::pow(swellPeakRatio, 4.0f)) *
                                            std::pow(2.6f, swellPeakShape);
                const float swellTheta = WrapAngle(std::atan2(kz, kx) - (kWindAngle + 0.82f));
                const float swellRatio = omega / swellPeakOmega;
                const float swellSpread = ((omega > swellPeakOmega ? 9.77f * std::pow(swellRatio, -2.5f)
                                                                   : 6.97f * std::pow(swellRatio, 5.0f)) +
                                           9.0f) *
                                          0.72f;
                const float swellDirection = SpectrumNormalisationFactor(swellSpread) *
                                             std::pow(std::abs(std::cos(swellTheta * 0.5f)), 2.0f * swellSpread);
                spectralDensity += swellSpectrum * swellDirection * shortWaveFade * config.secondaryScale;
            }

            const float amplitude = std::sqrt((std::max)(0.0f, 2.0f * spectralDensity * std::abs(frequencyDerivative) /
                                                                   kLength * deltaK * deltaK)) *
                                    config.amplitudeScale;
            const Float2 noise = random.Gaussian();
            initialK[pixel] = {noise.x * amplitude, noise.y * amplitude};
            result.waveData[pixel] = {kx, 1.0f / kLength, kz, omega};
        }
    }

    for (uint32_t y = 0u; y < size; ++y) {
        for (uint32_t x = 0u; x < size; ++x) {
            const size_t pixel = static_cast<size_t>(y) * size + x;
            const uint32_t mirrorX = (size - x) % size;
            const uint32_t mirrorY = (size - y) % size;
            const size_t mirror = static_cast<size_t>(mirrorY) * size + mirrorX;
            result.initialSpectrum[pixel] = {initialK[pixel].x, initialK[pixel].y, initialK[mirror].x,
                                             -initialK[mirror].y};
        }
    }
    return result;
}

std::vector<Float4> BuildTwiddleTable(uint32_t size) {
    uint32_t logSize = 0u;
    for (uint32_t value = size; value > 1u; value >>= 1u) {
        ++logSize;
    }

    std::vector<Float4> table(static_cast<size_t>(size) * logSize);
    for (uint32_t stage = 0u; stage < logSize; ++stage) {
        const uint32_t block = size >> (stage + 1u);
        for (uint32_t output = 0u; output < size / 2u; ++output) {
            const uint32_t first = (2u * block * (output / block) + output % block) % size;
            const float angle = -6.28318530717958647692f / static_cast<float>(size) *
                                static_cast<float>(output / block) * static_cast<float>(block);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const size_t base = static_cast<size_t>(stage) * size + output;
            table[base] = {cosine, sine, static_cast<float>(first), static_cast<float>(first + block)};
            table[base + size / 2u] = {-cosine, -sine, static_cast<float>(first), static_cast<float>(first + block)};
        }
    }
    return table;
}

void ReleaseDescriptor(RenderResourceView& view) {
    if (!view.IsValid()) {
        return;
    }
    (void)ReleaseRenderResourceDescriptor(view);
    view = {};
}

bool MustDeferRetirement(const GFX::Context* context) noexcept {
    return context != nullptr && context->deferredReleaseQueue != nullptr &&
           context->currentFrameRetireFenceValue != 0u &&
           (context->frameFence == nullptr ||
            context->frameFence->GetCompletedValue() < context->currentFrameRetireFenceValue);
}

void RetireDescriptor(RenderResourceView& view, const GFX::Context* context, const char* debugName) {
    if (!view.IsValid()) {
        return;
    }
    const RenderResourceView retired = view;
    view = {};
    if (MustDeferRetirement(context)) {
        context->deferredReleaseQueue->Enqueue(
            context->currentFrameRetireFenceValue,
            [retired]() {
                RenderResourceView copy = retired;
                ReleaseDescriptor(copy);
            },
            debugName != nullptr ? debugName : "OceanSpectrum.Descriptor");
        return;
    }
    RenderResourceView copy = retired;
    ReleaseDescriptor(copy);
}

template <typename T>
void RetireObject(ComPtr<T>& object, const GFX::Context* context, const char* debugName) {
    if (!object) {
        return;
    }
    if (MustDeferRetirement(context)) {
        GFX::RetireD3D12ObjectForFrame(object, *context, debugName != nullptr ? debugName : "OceanSpectrum.Resource");
    } else {
        object.Reset();
    }
}

void ForgetResource(ID3D12Resource* resource, const GFX::Context* context) {
    if (resource != nullptr && context != nullptr && context->resourceStates != nullptr) {
        context->resourceStates->Forget(resource);
    }
}

void DestroyBuffer(GpuBuffer& buffer, const GFX::Context* context, const char* debugName) {
    ForgetResource(buffer.resource.Get(), context);
    RetireDescriptor(buffer.srv, context, debugName);
    RetireObject(buffer.resource, context, debugName);
    buffer = {};
}

void DestroyTexture(GpuTexture& texture, const GFX::Context* context, const char* debugName) {
    ForgetResource(texture.resource.Get(), context);
    RetireDescriptor(texture.srv, context, debugName);
    RetireDescriptor(texture.uav, context, debugName);
    RetireObject(texture.resource, context, debugName);
    texture = {};
}

void DestroyGpuState(OceanSpectrumRuntime::State& state, const GFX::Context* context) {
    // Output SRVs are the first six descriptors allocated by this
    // owner. Release them last so a later re-initialize can recover
    // the same contiguous range from the descriptor allocator's
    // LIFO free list instead of accidentally interleaving UAV slots.
    std::array<RenderResourceView, kOceanSpectrumCascadeCount * 2u> outputDescriptors{};
    for (uint32_t index = 0u; index < kOceanSpectrumCascadeCount; ++index) {
        outputDescriptors[kOceanSpectrumDisplacementTableOffset + index] = state.cascades[index].displacement.srv;
        state.cascades[index].displacement.srv = {};
        outputDescriptors[kOceanSpectrumSlopeTableOffset + index] = state.cascades[index].slope.srv;
        state.cascades[index].slope.srv = {};
    }
    DestroyBuffer(state.twiddleTable, context, "OceanSpectrum.Twiddle");
    for (CascadeState& cascade : state.cascades) {
        DestroyBuffer(cascade.initialSpectrum, context, "OceanSpectrum.InitialSpectrum");
        DestroyBuffer(cascade.waveData, context, "OceanSpectrum.WaveData");
        for (auto& ping : cascade.fields) {
            for (GpuTexture& field : ping) {
                DestroyTexture(field, context, "OceanSpectrum.IfftField");
            }
        }
        DestroyTexture(cascade.displacement, context, "OceanSpectrum.Displacement");
        DestroyTexture(cascade.slope, context, "OceanSpectrum.Slope");
    }
    RetireObject(state.evolutionPso, context, "OceanSpectrum.EvolutionPSO");
    RetireObject(state.stockhamPso, context, "OceanSpectrum.StockhamPSO");
    RetireObject(state.resolvePso, context, "OceanSpectrum.ResolvePSO");
    RetireObject(state.evolutionRoot, context, "OceanSpectrum.EvolutionRoot");
    RetireObject(state.stockhamRoot, context, "OceanSpectrum.StockhamRoot");
    RetireObject(state.resolveRoot, context, "OceanSpectrum.ResolveRoot");
    for (RenderResourceView& descriptor : outputDescriptors) {
        RetireDescriptor(descriptor, context, "OceanSpectrum.OutputTableSRV");
    }
    state.outputs = {};
    for (RenderResourceView& descriptor : state.outputTableDescriptors) {
        RetireDescriptor(descriptor, context, "OceanSpectrum.OutputTableSRV");
    }
    state.outputTable = {};
    state.stats = {};
    state.ownerContext = {};
    state.hasRecordedFrame = false;
    state.contextConflictFrameIndex = 0u;
    state.contextConflict = false;
    state.initialized = false;
}

bool CreateStructuredUploadBuffer(const GFX::Context& context, const std::vector<Float4>& values, const char* debugName,
                                  GpuBuffer& output) {
    if (context.device == nullptr || values.empty()) {
        return false;
    }
    const uint64_t byteSize = static_cast<uint64_t>(values.size()) * sizeof(Float4);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             context.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                             nullptr, GFX::VIDEO_MEMORY::Owner::Geometry, IID_PPV_ARGS(output.resource.GetAddressOf())),
                         "OceanSpectrum::CreateUploadBuffer")) {
        return false;
    }

    void* mapped = nullptr;
    if (!HIKARI_DX_CHECK(output.resource->Map(0u, nullptr, &mapped), "OceanSpectrum::MapUploadBuffer")) {
        output.resource.Reset();
        return false;
    }
    std::memcpy(mapped, values.data(), static_cast<size_t>(byteSize));
    output.resource->Unmap(0u, nullptr);
    GFX::UPLOAD_TRAFFIC::Record(GFX::UPLOAD_TRAFFIC::Owner::Water, byteSize);
    output.elementCount = static_cast<uint32_t>(values.size());
    output.byteSize = byteSize;
    output.state = D3D12_RESOURCE_STATE_GENERIC_READ;
    output.srv =
        AllocateBufferSrvDescriptor(output.resource.Get(), DXGI_FORMAT_UNKNOWN, output.elementCount, sizeof(Float4));
    if (!output.srv.IsValid()) {
        output.resource.Reset();
        return false;
    }
    (void)debugName;
    return true;
}

bool WriteTextureSrvDescriptor(const GFX::Context& context, ID3D12Resource* resource, RenderResourceView& view) {
    if (context.device == nullptr || resource == nullptr || !view.IsValid()) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = kSpectrumFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0u;
    srvDesc.Texture2D.MipLevels = 1u;
    srvDesc.Texture2D.PlaneSlice = 0u;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    context.device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
    return true;
}

bool CreateSpectrumTexture(const GFX::Context& context, uint32_t resolution, const char* debugName, GpuTexture& output,
                           RenderResourceView providedSrv = {}) {
    if (context.device == nullptr || resolution == 0u) {
        return false;
    }
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(kSpectrumFormat, resolution, resolution, 1u, 1u, 1u, 0u,
                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             context.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                             GFX::VIDEO_MEMORY::Owner::Geometry, IID_PPV_ARGS(output.resource.GetAddressOf())),
                         "OceanSpectrum::CreateTexture")) {
        return false;
    }
    output.width = resolution;
    output.height = resolution;
    output.byteSize = static_cast<uint64_t>(resolution) * resolution * 8u;
    output.state = D3D12_RESOURCE_STATE_COMMON;
    output.srv =
        providedSrv.IsValid() ? providedSrv : AllocateTexture2DSrvDescriptor(output.resource.Get(), kSpectrumFormat);
    output.uav = AllocateTexture2DUavDescriptor(output.resource.Get(), kSpectrumFormat);
    if (!output.srv.IsValid() || !output.uav.IsValid() ||
        !WriteTextureSrvDescriptor(context, output.resource.Get(), output.srv)) {
        ReleaseDescriptor(output.srv);
        ReleaseDescriptor(output.uav);
        output.resource.Reset();
        return false;
    }
    if (context.resourceStates != nullptr) {
        context.resourceStates->Track(output.resource.Get(), D3D12_RESOURCE_STATE_COMMON);
    }
    (void)debugName;
    return true;
}

bool CreateRootSignature(ID3D12Device* device, D3D12_ROOT_PARAMETER* parameters, UINT parameterCount,
                         ID3D12RootSignature** output) {
    if (device == nullptr || parameters == nullptr || output == nullptr) {
        return false;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = parameterCount;
    desc.pParameters = parameters;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, desc, output), "OceanSpectrum::CreateRootSignature");
}

bool CreatePipelines(const GFX::Context& context, OceanSpectrumRuntime::State& state) {
    if (context.device == nullptr) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE evolutionRanges[4]{};
    for (uint32_t index = 0u; index < 4u; ++index) {
        evolutionRanges[index].RangeType =
            index < 2u ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        evolutionRanges[index].NumDescriptors = 1u;
        evolutionRanges[index].BaseShaderRegister = index < 2u ? index : index - 2u;
        evolutionRanges[index].OffsetInDescriptorsFromTableStart = 0u;
    }
    D3D12_ROOT_PARAMETER evolutionParameters[5]{};
    evolutionParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    evolutionParameters[0].Constants.ShaderRegister = 0u;
    evolutionParameters[0].Constants.Num32BitValues = 8u;
    for (uint32_t index = 0u; index < 4u; ++index) {
        D3D12_ROOT_PARAMETER& parameter = evolutionParameters[index + 1u];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1u;
        parameter.DescriptorTable.pDescriptorRanges = &evolutionRanges[index];
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    if (!CreateRootSignature(context.device, evolutionParameters, 5u, state.evolutionRoot.GetAddressOf())) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE stockhamRanges[5]{};
    for (uint32_t index = 0u; index < 5u; ++index) {
        stockhamRanges[index].RangeType =
            index < 3u ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        stockhamRanges[index].NumDescriptors = 1u;
        stockhamRanges[index].BaseShaderRegister = index < 3u ? index : index - 3u;
        stockhamRanges[index].OffsetInDescriptorsFromTableStart = 0u;
    }
    D3D12_ROOT_PARAMETER stockhamParameters[6]{};
    stockhamParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    stockhamParameters[0].Constants.ShaderRegister = 0u;
    stockhamParameters[0].Constants.Num32BitValues = 4u;
    for (uint32_t index = 0u; index < 5u; ++index) {
        D3D12_ROOT_PARAMETER& parameter = stockhamParameters[index + 1u];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1u;
        parameter.DescriptorTable.pDescriptorRanges = &stockhamRanges[index];
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    if (!CreateRootSignature(context.device, stockhamParameters, 6u, state.stockhamRoot.GetAddressOf())) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE resolveRanges[4]{};
    for (uint32_t index = 0u; index < 4u; ++index) {
        resolveRanges[index].RangeType = index < 2u ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        resolveRanges[index].NumDescriptors = 1u;
        resolveRanges[index].BaseShaderRegister = index < 2u ? index : index - 2u;
        resolveRanges[index].OffsetInDescriptorsFromTableStart = 0u;
    }
    D3D12_ROOT_PARAMETER resolveParameters[5]{};
    resolveParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    resolveParameters[0].Constants.ShaderRegister = 0u;
    resolveParameters[0].Constants.Num32BitValues = 8u;
    for (uint32_t index = 0u; index < 4u; ++index) {
        D3D12_ROOT_PARAMETER& parameter = resolveParameters[index + 1u];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1u;
        parameter.DescriptorTable.pDescriptorRanges = &resolveRanges[index];
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    if (!CreateRootSignature(context.device, resolveParameters, 5u, state.resolveRoot.GetAddressOf())) {
        return false;
    }

    const wchar_t* evolutionPath = L"HIKARI/Shaders/WaterSpectrum/HIKARI_WaterSpectrumEvolutionCS.hlsl";
    const wchar_t* stockhamPath = L"HIKARI/Shaders/WaterSpectrum/HIKARI_WaterSpectrumStockhamCS.hlsl";
    const wchar_t* resolvePath = L"HIKARI/Shaders/WaterSpectrum/HIKARI_WaterSpectrumResolveCS.hlsl";
    ComPtr<ID3DBlob> evolutionBlob;
    ComPtr<ID3DBlob> stockhamBlob;
    ComPtr<ID3DBlob> resolveBlob;
    if (!GFX::CompileShaderFileSm6(evolutionPath, "main", GFX::ShaderStage::Compute, evolutionBlob.GetAddressOf()) ||
        !GFX::CompileShaderFileSm6(stockhamPath, "main", GFX::ShaderStage::Compute, stockhamBlob.GetAddressOf()) ||
        !GFX::CompileShaderFileSm6(resolvePath, "main", GFX::ShaderStage::Compute, resolveBlob.GetAddressOf())) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC evolutionDesc{};
    evolutionDesc.pRootSignature = state.evolutionRoot.Get();
    evolutionDesc.CS = {evolutionBlob->GetBufferPointer(), evolutionBlob->GetBufferSize()};
    if (!HIKARI_DX_CHECK(
            context.device->CreateComputePipelineState(&evolutionDesc, IID_PPV_ARGS(state.evolutionPso.GetAddressOf())),
            "OceanSpectrum::CreateEvolutionPSO")) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC stockhamDesc{};
    stockhamDesc.pRootSignature = state.stockhamRoot.Get();
    stockhamDesc.CS = {stockhamBlob->GetBufferPointer(), stockhamBlob->GetBufferSize()};
    if (!HIKARI_DX_CHECK(
            context.device->CreateComputePipelineState(&stockhamDesc, IID_PPV_ARGS(state.stockhamPso.GetAddressOf())),
            "OceanSpectrum::CreateStockhamPSO")) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC resolveDesc{};
    resolveDesc.pRootSignature = state.resolveRoot.Get();
    resolveDesc.CS = {resolveBlob->GetBufferPointer(), resolveBlob->GetBufferSize()};
    if (!HIKARI_DX_CHECK(
            context.device->CreateComputePipelineState(&resolveDesc, IID_PPV_ARGS(state.resolvePso.GetAddressOf())),
            "OceanSpectrum::CreateResolvePSO")) {
        return false;
    }
    return true;
}

bool TransitionTexture(const GFX::Context& context, GpuTexture& texture, D3D12_RESOURCE_STATES nextState) {
    if (texture.resource == nullptr || context.cmdList == nullptr) {
        return false;
    }
    if (texture.state == nextState) {
        return true;
    }
    if (context.resourceStates != nullptr) {
        context.resourceStates->Transition(context.cmdList, texture.resource.Get(), texture.state, nextState);
    } else {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, nextState);
        context.cmdList->ResourceBarrier(1u, &barrier);
    }
    texture.state = nextState;
    return true;
}

void SetEvolutionConstants(ID3D12GraphicsCommandList* commandList, uint32_t resolution,
                           const OceanSpectrumCascadeDesc& cascade, float timeSeconds) {
    const std::array<uint32_t, 8> constants{[&]() {
                                                uint32_t bits{};
                                                std::memcpy(&bits, &timeSeconds, sizeof(bits));
                                                return bits;
                                            }(),
                                            resolution,
                                            [&]() {
                                                const float value = 6.28318530717958647692f / cascade.lengthScale;
                                                uint32_t bits{};
                                                std::memcpy(&bits, &value, sizeof(bits));
                                                return bits;
                                            }(),
                                            [&]() {
                                                uint32_t bits{};
                                                std::memcpy(&bits, &cascade.lengthScale, sizeof(bits));
                                                return bits;
                                            }(),
                                            [&]() {
                                                uint32_t bits{};
                                                std::memcpy(&bits, &cascade.choppiness, sizeof(bits));
                                                return bits;
                                            }(),
                                            0u,
                                            0u,
                                            0u};
    commandList->SetComputeRoot32BitConstants(0u, static_cast<UINT>(constants.size()), constants.data(), 0u);
}

void SetStockhamConstants(ID3D12GraphicsCommandList* commandList, uint32_t axis, uint32_t stage, uint32_t resolution,
                          uint32_t finalize) {
    const std::array<uint32_t, 4> constants{axis, stage, resolution, finalize};
    commandList->SetComputeRoot32BitConstants(0u, static_cast<UINT>(constants.size()), constants.data(), 0u);
}

void SetResolveConstants(ID3D12GraphicsCommandList* commandList, uint32_t resolution,
                         const OceanSpectrumCascadeDesc& cascade) {
    const float inverseNormalization = 1.0f / static_cast<float>(resolution * resolution);
    const float gridStep = cascade.lengthScale / static_cast<float>(resolution);
    const std::array<uint32_t, 8> constants{resolution,
                                            [&]() {
                                                uint32_t bits{};
                                                std::memcpy(&bits, &inverseNormalization, sizeof(bits));
                                                return bits;
                                            }(),
                                            [&]() {
                                                uint32_t bits{};
                                                std::memcpy(&bits, &gridStep, sizeof(bits));
                                                return bits;
                                            }(),
                                            0u,
                                            0u,
                                            0u,
                                            0u,
                                            0u};
    commandList->SetComputeRoot32BitConstants(0u, static_cast<UINT>(constants.size()), constants.data(), 0u);
}

RenderTextureView MakeTextureView(const GpuTexture& texture, bool valid) {
    RenderTextureView view{};
    view.resource = texture.resource.Get();
    view.srv = texture.srv.gpu;
    view.format = kSpectrumFormat;
    view.width = texture.width;
    view.height = texture.height;
    view.mipCount = 1u;
    view.state = texture.state;
    view.valid = valid && texture.resource != nullptr && texture.srv.IsValid();
    return view;
}

void PublishOutputs(OceanSpectrumRuntime::State& state, uint64_t frameIndex, bool valid) {
    for (uint32_t index = 0u; index < kOceanSpectrumCascadeCount; ++index) {
        const CascadeState& cascade = state.cascades[index];
        OceanSpectrumCascadeOutput& output = state.outputs[index];
        output.displacement = MakeTextureView(cascade.displacement, valid);
        output.slope = MakeTextureView(cascade.slope, valid);
        output.lengthScale = cascade.desc.lengthScale;
        output.choppiness = cascade.desc.choppiness;
        output.resolution = state.config.resolution;
        output.frameIndex = frameIndex;
        output.valid = valid;
    }
    state.stats.outputValid = valid;
}

void InvalidateOutputsForFrame(OceanSpectrumRuntime::State& state, uint64_t frameIndex) {
    state.hasRecordedFrame = false;
    state.contextConflict = false;
    state.contextConflictFrameIndex = 0u;
    state.stats.frameIndex = frameIndex;
    state.stats.dispatchCount = 0u;
    state.stats.contextConflict = false;
    PublishOutputs(state, frameIndex, false);
}

} // namespace

OceanSpectrumRuntime::~OceanSpectrumRuntime() {
    if (state_ != nullptr) {
        Shutdown(nullptr);
        delete state_;
        state_ = nullptr;
    }
}

bool OceanSpectrumRuntime::Initialize(const GFX::Context& context, const OceanSpectrumRuntimeConfig& config) {
    if (state_ == nullptr) {
        state_ = new State();
    }
    if (state_->initialized) {
        Shutdown(&context);
    }
    if (context.device == nullptr || context.srvHeap == nullptr || !IsSupportedConfig(config)) {
        return false;
    }

    state_->context = context;
    state_->ownerContext = context;
    state_->config = config;
    UpdateRenderResourceDescriptorPoolContext(context);

    const std::vector<RenderResourceView> outputDescriptors =
        AllocateRenderResourceDescriptorRange(static_cast<uint32_t>(state_->outputTableDescriptors.size()));
    if (outputDescriptors.size() != state_->outputTableDescriptors.size()) {
        DestroyGpuState(*state_, &context);
        return false;
    }
    for (size_t index = 0u; index < state_->outputTableDescriptors.size(); ++index) {
        state_->outputTableDescriptors[index] = outputDescriptors[index];
    }
    state_->outputTable.base = state_->outputTableDescriptors[0].gpu;
    state_->outputTable.descriptorCount = static_cast<uint32_t>(state_->outputTableDescriptors.size());
    state_->outputTable.valid = true;

    if (!CreatePipelines(context, *state_)) {
        DestroyGpuState(*state_, &context);
        return false;
    }

    const std::vector<Float4> twiddle = BuildTwiddleTable(config.resolution);
    if (!CreateStructuredUploadBuffer(context, twiddle, "OceanSpectrum.Twiddle", state_->twiddleTable)) {
        DestroyGpuState(*state_, &context);
        return false;
    }

    uint64_t workingSetBytes = state_->twiddleTable.byteSize;
    for (uint32_t index = 0u; index < kOceanSpectrumCascadeCount; ++index) {
        CascadeState& cascade = state_->cascades[index];
        cascade.desc = config.cascades[index];
        const SpectralCpuData cpuData = BuildSpectralCpuData(config.resolution, cascade.desc);
        if (!CreateStructuredUploadBuffer(context, cpuData.initialSpectrum, "OceanSpectrum.InitialSpectrum",
                                          cascade.initialSpectrum) ||
            !CreateStructuredUploadBuffer(context, cpuData.waveData, "OceanSpectrum.WaveData", cascade.waveData)) {
            DestroyGpuState(*state_, &context);
            return false;
        }
        workingSetBytes += cascade.initialSpectrum.byteSize + cascade.waveData.byteSize;

        for (auto& ping : cascade.fields) {
            for (GpuTexture& field : ping) {
                if (!CreateSpectrumTexture(context, config.resolution, "OceanSpectrum.IfftField", field)) {
                    DestroyGpuState(*state_, &context);
                    return false;
                }
                workingSetBytes += field.byteSize;
            }
        }
        if (!CreateSpectrumTexture(
                context, config.resolution, "OceanSpectrum.Displacement", cascade.displacement,
                [&]() {
                    RenderResourceView view =
                        state_->outputTableDescriptors[kOceanSpectrumDisplacementTableOffset + index];
                    state_->outputTableDescriptors[kOceanSpectrumDisplacementTableOffset + index] = {};
                    return view;
                }()) ||
            !CreateSpectrumTexture(context, config.resolution, "OceanSpectrum.Slope", cascade.slope, [&]() {
                RenderResourceView view = state_->outputTableDescriptors[kOceanSpectrumSlopeTableOffset + index];
                state_->outputTableDescriptors[kOceanSpectrumSlopeTableOffset + index] = {};
                return view;
            }())) {
            DestroyGpuState(*state_, &context);
            return false;
        }
        workingSetBytes += cascade.displacement.byteSize + cascade.slope.byteSize;
    }

    state_->initialized = true;
    state_->hasRecordedFrame = false;
    state_->stats.initialized = true;
    state_->stats.pipelineReady = true;
    state_->stats.resolution = config.resolution;
    state_->stats.cascadeCount = kOceanSpectrumCascadeCount;
    state_->stats.workingSetBytes = workingSetBytes;
    ++state_->stats.resizeCount;
    ++state_->stats.historyResetCount;
    PublishOutputs(*state_, 0u, false);
    return true;
}

bool OceanSpectrumRuntime::RecordCompute(const GFX::Context& context, uint64_t frameIndex, float timeSeconds) {
    const auto failFrame = [this, frameIndex]() {
        if (state_ != nullptr && state_->initialized) {
            InvalidateOutputsForFrame(*state_, frameIndex);
        }
        return false;
    };
    if (state_ == nullptr || !state_->initialized) {
        return failFrame();
    }

    if (state_->contextConflict && state_->contextConflictFrameIndex == frameIndex) {
        // A different queue/cmd context already attempted this frame.
        // Keep the output invalid until the next frame; retrying here
        // could record against resources still in flight on the first
        // context.
        return false;
    }
    if (state_->contextConflict && state_->contextConflictFrameIndex != frameIndex) {
        state_->contextConflict = false;
        state_->contextConflictFrameIndex = 0u;
        state_->stats.contextConflict = false;
    }

    if (state_->hasRecordedFrame && state_->lastFrameIndex == frameIndex) {
        if (HasSameExecutionContext(context, state_->context)) {
            return true;
        }

        InvalidateOutputsForFrame(*state_, frameIndex);
        state_->contextConflict = true;
        state_->contextConflictFrameIndex = frameIndex;
        state_->stats.contextConflict = true;
        return false;
    }

    if (context.device == nullptr || context.cmdList == nullptr || context.srvHeap == nullptr ||
        !HasSameSubmissionOwner(context, state_->ownerContext)) {
        return failFrame();
    }
    if (!std::isfinite(timeSeconds)) {
        return failFrame();
    }

    state_->context = context;
    ID3D12DescriptorHeap* descriptorHeaps[] = {context.srvHeap};
    context.cmdList->SetDescriptorHeaps(1u, descriptorHeaps);
    const uint32_t resolution = state_->config.resolution;
    const uint32_t groups = (resolution + kWorkgroupSize - 1u) / kWorkgroupSize;
    uint32_t dispatchCount = 0u;

    for (CascadeState& cascade : state_->cascades) {
        GpuTexture& evolutionField0 = cascade.fields[0][0];
        GpuTexture& evolutionField1 = cascade.fields[0][1];
        if (!TransitionTexture(context, evolutionField0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
            !TransitionTexture(context, evolutionField1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
            return failFrame();
        }
        context.cmdList->SetComputeRootSignature(state_->evolutionRoot.Get());
        context.cmdList->SetPipelineState(state_->evolutionPso.Get());
        SetEvolutionConstants(context.cmdList, resolution, cascade.desc, timeSeconds);
        context.cmdList->SetComputeRootDescriptorTable(1u, cascade.initialSpectrum.srv.gpu);
        context.cmdList->SetComputeRootDescriptorTable(2u, cascade.waveData.srv.gpu);
        context.cmdList->SetComputeRootDescriptorTable(3u, evolutionField0.uav.gpu);
        context.cmdList->SetComputeRootDescriptorTable(4u, evolutionField1.uav.gpu);
        context.cmdList->Dispatch(groups, groups, 1u);
        ++dispatchCount;

        const uint32_t logSize =
            resolution == 1u ? 0u : static_cast<uint32_t>(std::log2(static_cast<double>(resolution)));
        for (uint32_t pass = 0u; pass < logSize * 2u; ++pass) {
            const uint32_t sourcePing = pass % 2u;
            const uint32_t destinationPing = 1u - sourcePing;
            GpuTexture& source0 = cascade.fields[sourcePing][0];
            GpuTexture& source1 = cascade.fields[sourcePing][1];
            GpuTexture& destination0 = cascade.fields[destinationPing][0];
            GpuTexture& destination1 = cascade.fields[destinationPing][1];
            if (!TransitionTexture(context, source0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
                !TransitionTexture(context, source1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
                !TransitionTexture(context, destination0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
                !TransitionTexture(context, destination1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
                return failFrame();
            }
            context.cmdList->SetComputeRootSignature(state_->stockhamRoot.Get());
            context.cmdList->SetPipelineState(state_->stockhamPso.Get());
            SetStockhamConstants(context.cmdList, pass < logSize ? 0u : 1u, pass % logSize, resolution,
                                 pass + 1u == logSize * 2u ? 1u : 0u);
            context.cmdList->SetComputeRootDescriptorTable(1u, state_->twiddleTable.srv.gpu);
            context.cmdList->SetComputeRootDescriptorTable(2u, source0.srv.gpu);
            context.cmdList->SetComputeRootDescriptorTable(3u, source1.srv.gpu);
            context.cmdList->SetComputeRootDescriptorTable(4u, destination0.uav.gpu);
            context.cmdList->SetComputeRootDescriptorTable(5u, destination1.uav.gpu);
            context.cmdList->Dispatch(groups, groups, 1u);
            ++dispatchCount;
        }

        GpuTexture& finalField0 = cascade.fields[0][0];
        GpuTexture& finalField1 = cascade.fields[0][1];
        if (!TransitionTexture(context, finalField0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
            !TransitionTexture(context, finalField1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
            !TransitionTexture(context, cascade.displacement, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
            !TransitionTexture(context, cascade.slope, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
            return failFrame();
        }
        context.cmdList->SetComputeRootSignature(state_->resolveRoot.Get());
        context.cmdList->SetPipelineState(state_->resolvePso.Get());
        SetResolveConstants(context.cmdList, resolution, cascade.desc);
        context.cmdList->SetComputeRootDescriptorTable(1u, finalField0.srv.gpu);
        context.cmdList->SetComputeRootDescriptorTable(2u, finalField1.srv.gpu);
        context.cmdList->SetComputeRootDescriptorTable(3u, cascade.displacement.uav.gpu);
        context.cmdList->SetComputeRootDescriptorTable(4u, cascade.slope.uav.gpu);
        context.cmdList->Dispatch(groups, groups, 1u);
        ++dispatchCount;

        if (!TransitionTexture(context, cascade.displacement,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
            !TransitionTexture(context, cascade.slope,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
            return failFrame();
        }
    }

    state_->lastFrameIndex = frameIndex;
    state_->hasRecordedFrame = true;
    state_->contextConflict = false;
    state_->contextConflictFrameIndex = 0u;
    state_->stats.frameIndex = frameIndex;
    state_->stats.dispatchCount = dispatchCount;
    state_->stats.contextConflict = false;
    PublishOutputs(*state_, frameIndex, true);
    return true;
}

void OceanSpectrumRuntime::Shutdown(const GFX::Context* context) {
    if (state_ == nullptr) {
        return;
    }
    const GFX::Context* retireContext = context;
    if (retireContext == nullptr && state_->context.device != nullptr) {
        retireContext = &state_->context;
    }
    DestroyGpuState(*state_, retireContext);
    State replacement{};
    *state_ = std::move(replacement);
}

bool OceanSpectrumRuntime::IsInitialized() const noexcept { return state_ != nullptr && state_->initialized; }

bool OceanSpectrumRuntime::IsOutputValidForFrame(uint64_t frameIndex) const noexcept {
    if (state_ == nullptr || !state_->initialized || !state_->hasRecordedFrame || !state_->stats.outputValid ||
        state_->stats.frameIndex != frameIndex) {
        return false;
    }
    for (const OceanSpectrumCascadeOutput& output : state_->outputs) {
        if (!output.valid || output.frameIndex != frameIndex) {
            return false;
        }
    }
    return true;
}

uint32_t OceanSpectrumRuntime::GetResolution() const noexcept {
    return state_ != nullptr ? state_->config.resolution : 0u;
}

const OceanSpectrumCascadeOutput& OceanSpectrumRuntime::GetOutput(OceanSpectrumCascadeKind cascade) const noexcept {
    static const OceanSpectrumCascadeOutput kEmpty{};
    if (state_ == nullptr) {
        return kEmpty;
    }
    const uint32_t index = static_cast<uint32_t>(cascade);
    if (index >= kOceanSpectrumCascadeCount) {
        return kEmpty;
    }
    return state_->outputs[index];
}

const OceanSpectrumOutputDescriptorTable& OceanSpectrumRuntime::GetOutputDescriptorTable() const noexcept {
    static const OceanSpectrumOutputDescriptorTable kEmpty{};
    return state_ != nullptr ? state_->outputTable : kEmpty;
}

const OceanSpectrumRuntimeStats& OceanSpectrumRuntime::GetStats() const noexcept {
    static const OceanSpectrumRuntimeStats kEmpty{};
    return state_ != nullptr ? state_->stats : kEmpty;
}

} // namespace HIKARI::RENDER3D::WATER::SPECTRUM
