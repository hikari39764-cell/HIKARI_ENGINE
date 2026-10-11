#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Lighting/HIKARI_DynamicDiffuseGi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <d3dx12.h>
#include <wrl/client.h>
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayQueryBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D::DYNAMICGI {
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto kReadState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
// 光線クエリの共通根パラメーターの後に、前フレーム SH・SH の UAV・probe atlas の UAV・空の順に並べる。
constexpr UINT kPreviousShParameter = RAYTRACING::kRuntimeRayQueryRootParameterCount;
constexpr UINT kSkyParameter = kPreviousShParameter + 3u;
// 近景の高さを決める相機の ray の結果（当たった点の高さ）を書く UAV。中央・上下左右・真下の 6 本。
constexpr UINT kFocusParameter = kSkyParameter + 1u;
constexpr uint32_t kFocusRayCount = 6;
// 一本ごとに当たった点の高さと、相機からの水平距離を書く。
constexpr uint32_t kFocusValueCount = kFocusRayCount * 2;
constexpr float kFocusMiss = 1.0e30f;
// SH0 は一様な入射 radiance の約 3.54 倍。FP16 上限で HDR を壊さない。
constexpr auto kProbeShFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
// 描画側が読む probe atlas。probe ごとに放射照度と深度 moment の八面体、validity の tile を持つ
// （HIKARI_LightingProviderSampling.hlsli の HIKARI_DYNAMIC_GI_TILE_* と同じ寸法）。
constexpr auto kProbeAtlasFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr UINT kProbeAtlasTileWidth = 28, kProbeAtlasTileHeight = 18;
// probe 一回の更新の ray 数（Render3D_DynamicDiffuseGiCS.hlsl の kRayCount）。
constexpr uint32_t kProbeRayCount = 128;
constexpr uint64_t kProbeBudgetBytes = 64ull * 1024 * 1024;
bool SameVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}
// 一つの probe 場の配置。相機を追う近景と遠景の cascade、または手置きの実時間区域。
struct FieldLayout {
    MATH::Vec3 spacing{};
    UINT x = 0, y = 0, z = 0, budget = 0;
    float blendDistance = 0.0f, normalBias = 0.25f, intensity = 1.0f;
    int priority = 0;
    bool operator==(const FieldLayout& other) const {
        return SameVec3(spacing, other.spacing) && x == other.x && y == other.y && z == other.z &&
               budget == other.budget && blendDistance == other.blendDistance && normalBias == other.normalBias &&
               intensity == other.intensity && priority == other.priority;
    }
};
// 描画側の動的 GI volume と同じ並び（優先順）。
constexpr size_t kRegionField = 0, kNearField = 1, kFieldCount = LIGHTPROBE::kDynamicGiVolumeCount;
// 区域を更新する相機からの最大距離。外れた区域は cascade に任せる。
constexpr float kRegionReach = 32.0f;
UINT ProbeBudget(DynamicGiQuality quality) {
    return quality == DynamicGiQuality::Low ? 128u : quality == DynamicGiQuality::High ? 512u : 256u;
}
// 近景は 1.5m 間隔。遠景は 4 倍の間隔で近景の外（空の光だけでは平らに見える範囲）の遮蔽と照り返しを受け持つ。
// 描画側は近景、遠景の順に重ね、近景の縁は blendDistance（1 間隔）で遠景へ移る。
std::array<FieldLayout, 2> CascadeLayouts(DynamicGiQuality quality) {
    const UINT nearCount = quality == DynamicGiQuality::Low ? 12u : quality == DynamicGiQuality::High ? 24u : 16u;
    const UINT farCount = quality == DynamicGiQuality::Low ? 12u : 16u;
    const UINT budget = ProbeBudget(quality);
    return {FieldLayout{{1.5f, 1.5f, 1.5f}, nearCount, nearCount / 2, nearCount, budget, 1.5f},
            FieldLayout{{6.0f, 6.0f, 6.0f}, farCount, farCount / 2, farCount, budget / 2, 12.0f}};
}
// 手置きの実時間区域。probe は区域の格子に固定し、近景と同じ予算で数フレームに分けて更新する。
FieldLayout RegionLayout(const LIGHTING::IrradianceVolumeProvider& volume, DynamicGiQuality quality) {
    return {volume.spacing,
            volume.countX,
            volume.countY,
            volume.countZ,
            (std::min)(ProbeBudget(quality), volume.countX * volume.countY * volume.countZ),
            volume.blendDistance,
            volume.normalBias,
            volume.intensity,
            volume.priority};
}
// 一度に更新する区域は一つ。相機を含む区域を優先度順に、無ければ最も近い区域を選ぶ。
const LIGHTING::IrradianceVolumeProvider* SelectRegion(std::span<const LIGHTING::IrradianceVolumeProvider> volumes,
                                                       MATH::Vec3 camera) {
    const LIGHTING::IrradianceVolumeProvider* best = nullptr;
    float bestDistance = kRegionReach;
    for (const auto& volume : volumes) {
        if (!volume.enabled || !volume.realtime || volume.countX * volume.countY * volume.countZ == 0)
            continue;
        const auto outside = [](float value, float minimum, float size) {
            return (std::max)({minimum - value, value - (minimum + size), 0.0f});
        };
        const float dx = outside(camera.x, volume.origin.x, volume.size.x);
        const float dy = outside(camera.y, volume.origin.y, volume.size.y);
        const float dz = outside(camera.z, volume.origin.z, volume.size.z);
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < bestDistance ||
            (best != nullptr && distance == bestDistance && volume.priority > best->priority)) {
            best = &volume;
            bestDistance = distance;
        }
    }
    return best;
}
struct Constants {
    LIGHTPROBE::IrradianceVolumeGpuData previous{};
    MATH::Vec4 origin{0, 0, 0, 0}; // xyz 格子の原点
    MATH::Vec4 spacing{0, 0, 0, 0};
    std::array<uint32_t, 4> counts{}, work{};
    RAYTRACING::RuntimeRayLightingGpuData lighting{};
    // 相機の位置（w は近景の更新で 1、その時だけ ray を撃つ）と視線の向き。
    MATH::Vec4 focusOrigin{}, focusDirection{};
};
static_assert(sizeof(Constants) == 336);
struct ProbeSet {
    std::array<ComPtr<ID3D12Resource>, 10> textures;
    std::vector<RenderResourceView> srvs, uavs;
    TextureResourceHandle atlas;
    LIGHTPROBE::LightProbeVolumeRuntimeData data;
    uint64_t bytes = 0;
    void Reset(const GFX::Context& graphics) {
        for (auto& view : srvs)
            RetireRenderResourceDescriptor(view, graphics, "DynamicGI.SRV");
        for (auto& view : uavs)
            RetireRenderResourceDescriptor(view, graphics, "DynamicGI.UAV");
        srvs.clear();
        uavs.clear();
        if (atlas)
            ReleaseTextureResource(atlas);
        atlas = {};
        for (auto& texture : textures)
            GFX::RetireD3D12ObjectForFrame(texture, graphics, "DynamicGI.Probe");
        data = {};
        bytes = 0;
    }
    static std::array<D3D12_RESOURCE_DESC, 10> Descriptions(UINT x, UINT y, UINT z) {
        std::array<D3D12_RESOURCE_DESC, 10> result;
        for (UINT index = 0; index < 9; ++index)
            result[index] = CD3DX12_RESOURCE_DESC::Tex3D(kProbeShFormat, x, y, static_cast<UINT16>(z), 1,
                                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        result[9] =
            CD3DX12_RESOURCE_DESC::Tex2D(kProbeAtlasFormat, x * y * kProbeAtlasTileWidth, z * kProbeAtlasTileHeight, 1,
                                         1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        return result;
    }
    bool Create(const GFX::Context& graphics, const FieldLayout& layout) {
        const UINT x = layout.x, y = layout.y, z = layout.z;
        const auto descriptions = Descriptions(x, y, z);
        for (const auto& desc : descriptions) {
            const auto allocation = graphics.device->GetResourceAllocationInfo(0, 1, &desc);
            if (allocation.SizeInBytes > kProbeBudgetBytes / 2 ||
                bytes > kProbeBudgetBytes / 2 - allocation.SizeInBytes)
                return false;
            bytes += allocation.SizeInBytes;
        }
        srvs = AllocateRenderResourceDescriptorRange(9);
        uavs = AllocateRenderResourceDescriptorRange(10);
        if (srvs.size() != 9 || uavs.size() != 10)
            return false;
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        for (UINT index = 0; index < 10; ++index) {
            const auto& desc = descriptions[index];
            if (FAILED(graphics.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, kReadState, nullptr,
                                                                IID_PPV_ARGS(&textures[index]))))
                return false;
            textures[index]->SetName(L"HIKARI.DynamicDiffuseGI.ProbeField");
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = desc.Format;
            uav.ViewDimension = index == 9 ? D3D12_UAV_DIMENSION_TEXTURE2D : D3D12_UAV_DIMENSION_TEXTURE3D;
            if (index < 9)
                uav.Texture3D.WSize = z;
            graphics.device->CreateUnorderedAccessView(textures[index].Get(), nullptr, &uav, uavs[index].cpu);
            if (index < 9) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = desc.Format;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                srv.Texture3D.MipLevels = 1;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                graphics.device->CreateShaderResourceView(textures[index].Get(), &srv, srvs[index].cpu);
            }
        }
        atlas = RegisterTextureResourceFromNative(textures[9].Get(), kProbeAtlasFormat);
        if (!IsTextureResourceValid(atlas))
            return false;
        data.id = "DynamicDiffuseGi";
        data.enabled = true;
        data.dynamic = true;
        data.countX = x;
        data.countY = y;
        data.countZ = z;
        data.probeCount = x * y * z;
        data.spacing = layout.spacing;
        data.size = {float(x - 1) * layout.spacing.x, float(y - 1) * layout.spacing.y, float(z - 1) * layout.spacing.z};
        data.blendDistance = layout.blendDistance;
        data.normalBias = layout.normalBias;
        data.intensity = layout.intensity;
        data.priority = layout.priority;
        // 可視性が問うのは周囲 8 probe の格子内だけ。深度は格子の対角線の 1.5 倍で打ち切る（RTXGI と同じ）。
        // 空や遠景の深度をそのまま入れると、疎な ray を広げた隣の方向の平均深度が引き上げられ、地面の下の
        // probe が地面越しに見えると判定される。
        data.visibilityDistanceScale =
            1.5f * std::sqrt(layout.spacing.x * layout.spacing.x + layout.spacing.y * layout.spacing.y +
                             layout.spacing.z * layout.spacing.z);
        data.visibilityTextureDescriptorIndex = GetTextureResourceSrvDescriptorIndex(atlas);
        return data.visibilityTextureDescriptorIndex < GFX::DESCRIPTOR::kUserSrvCount;
    }
    void Transition(ID3D12GraphicsCommandList* commands, bool toWrite) {
        std::array<D3D12_RESOURCE_BARRIER, 10> barriers;
        for (size_t i = 0; i < textures.size(); ++i)
            barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
                textures[i].Get(), toWrite ? kReadState : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                toWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : kReadState);
        commands->ResourceBarrier(UINT(barriers.size()), barriers.data());
    }
};
// 近景の cascade の置き場所（中心の高さと、視線の水平方向へ前に出す距離）。
struct NearFieldPlacement {
    float height = 0.0f;
    float reach = 0.0f;
};
// 高さは視線の 5 本が当たった点の高さの中央値の少し上（格子の高さの半分の半分）へ下ろすが、視線では相機より上へは
// 上げない。下ろさないと、高い所から見下ろした時に地面の近くが間隔 6m の遠景だけになり、小さな部屋の床や壁が塊の
// ような汚れになった。真下の ray が近くの地面に当たる時は、その地面を格子に残す（崖の
// 縁から谷を見下ろしても足元が粗くならない）。見下ろしていない時（中央値の少し上が相機以上）に真下の地面が近ければ、
// 最下段の probe が地面の 1〜2 間隔下に来るまで格子を上げる。相機の高さを中心にすると半分の段が地面の下で無駄になり、
// 低档（6 段）では高さ 3m の天井が上の縁の 1.5m の移行（間隔 6m の遠景）に入って、屋根越しに外の光で明るくなった。
// 視線がどこにも当たらなければ真下の地面、それも無ければ相機の高さ。
// 前に出す距離は、視線の当たった点までの水平距離の中央値を、相機の地面からの高さの 1.5 倍までで取る。高い所から
// 斜めに見ると見ている面が近景の外（前へ約 19m 先）になりやすく、20m 先の家が遠景の塊の汚れになった。地面に立った
// 視点では高さが小さいのでほとんど出さない（足元の近くの面が一番大事）。
NearFieldPlacement PlaceNearField(const std::array<float, kFocusValueCount>& values, float cameraHeight,
                                  float halfHeight, float spacing) {
    std::array<float, kFocusRayCount - 1> heights{}, distances{};
    size_t count = 0;
    const auto hit = [&](size_t index) { return std::isfinite(values[index]) && values[index] < kFocusMiss; };
    for (size_t index = 0; index + 1 < kFocusRayCount; ++index)
        if (hit(index)) {
            heights[count] = values[index];
            distances[count++] = values[kFocusRayCount + index];
        }
    const size_t viewCount = count;
    const size_t belowIndex = kFocusRayCount - 1;
    const bool belowHit = hit(belowIndex);
    const float below = values[belowIndex];
    NearFieldPlacement placement{cameraHeight, 0.0f};
    if (count == 0 && belowHit)
        heights[count++] = below;
    if (count == 0)
        return placement;
    std::sort(heights.begin(), heights.begin() + count);
    const float viewHeight = heights[count / 2] + 0.5f * halfHeight;
    placement.height = (std::min)(cameraHeight, viewHeight);
    // 格子の中心は（高さ - 0.5 間隔）以下の間隔の倍数へ丸められる（gridOrigin）ので、高さを地面 + halfHeight - 0.5 間隔に
    // すると最下段は地面の 1〜2 間隔下になる。
    if (belowHit && cameraHeight - below < halfHeight)
        placement.height = viewHeight >= cameraHeight
                               ? (std::max)(placement.height, below + halfHeight - 0.5f * spacing)
                               : (std::max)(placement.height, below - halfHeight + spacing);
    if (viewCount > 0) {
        std::sort(distances.begin(), distances.begin() + viewCount);
        const float elevation = belowHit ? (std::max)(cameraHeight - below, 0.0f) : distances[viewCount / 2];
        placement.reach = (std::min)(distances[viewCount / 2], 1.5f * elevation);
    }
    return placement;
}
// 一つの probe 場。履歴は二組の ProbeSet を交互に書き、前の組を読む。
struct Field {
    std::array<ProbeSet, 2> sets;
    // 画質の档位を切り替えて格子を作り直した時の、直前の公開中の組。新しい格子の最初の更新が近い probe を引き継ぎ
    // （carry）、GPU がそのフレームを終えてから放す。
    ProbeSet legacy;
    FieldLayout layout{};
    std::string id{}, failedId{};
    MATH::Vec3 fixedOrigin{};
    UINT published = 0, firstProbe = 0;
    // 履歴なしで始めた後、全 probe を更新する残りの回数。
    UINT fullUpdates = 0;
    uint64_t lastFrame = 0;
    bool history = false, active = false, carry = false;
    void Reset(const GFX::Context& graphics) {
        for (auto& set : sets)
            set.Reset(graphics);
        legacy.Reset(graphics);
        carry = false;
        fullUpdates = 0;
        layout = {};
        id.clear();
        fixedOrigin = {};
        published = firstProbe = 0;
        history = active = false;
    }
    const ProbeSet& Published() const { return sets[published]; }
};
} // namespace

struct DynamicDiffuseGi::State {
    std::array<Field, kFieldCount> fields;
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ID3D12Device* device = nullptr;
    ComPtr<ID3D12Fence> fence;
    uint64_t deviceGeneration = 0, lastFence = 0, sequence = 0, retryFrame = 0;
    uint64_t sourceLayout = 0;
    const void* sourceIdentity = nullptr;
    uint64_t lastRaySceneVersion = 0, lastLightingContentHash = 0;
    RAYTRACING::RuntimeRayTracingMaterialVersion lastMaterialVersion{};
    Stats stats;
    // 相機の ray の結果。GPU が書き、フレーム資源の数だけ遅れて読む。nearPlacement は近景の置き場所。
    struct FocusReadback {
        ComPtr<ID3D12Resource> buffer;
        uint64_t fence = 0;
    };
    ComPtr<ID3D12Resource> focus;
    std::array<FocusReadback, GFX::kFrameResourceCount> focusReadbacks;
    uint64_t focusFence = 0;
    std::optional<NearFieldPlacement> nearPlacement;

    bool EnsurePipeline(const GFX::Context& graphics) {
        if (pipeline)
            return true;
        // 共通の後に前フレーム SH・SH の UAV・probe atlas の UAV・空。
        std::array<CD3DX12_ROOT_PARAMETER, kFocusParameter + 1u> parameters;
        std::array<CD3DX12_DESCRIPTOR_RANGE, 6> ranges;
        RAYTRACING::InitRuntimeRayQueryRootParameters(parameters, ranges, kSkyParameter);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 9, 0, 2);
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 9, 0);
        ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 9);
        for (UINT i = 0; i < 3; ++i)
            parameters[kPreviousShParameter + i].InitAsDescriptorTable(1, &ranges[3 + i]);
        parameters[kFocusParameter].InitAsUnorderedAccessView(10);
        const auto samplers = RAYTRACING::RuntimeRayQueryStaticSamplers();
        const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data(), 2, samplers.data());
        if (FAILED(GFX::CreateRootSignature(graphics.device, rootDesc, &root)))
            return false;
        namespace SHADER = ASSETS::SHADER;
        SHADER::ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = "HIKARI.DynamicDiffuseGi";
        contract.engineInterfaceVersion = 4;
        contract.rootLayoutId = "HIKARI.DynamicDiffuseGi.Root";
        contract.rootLayoutVersion = 2;
        if (!HASH::ComputeSha256(
                "DynamicDiffuseGiCB:400;origin,axis spacing,focus origin/direction;SH9 radiance/validity/sky L1;atlas RGBA16F tile28x18 irradiance^(1/5)+sky oct8,moments+validity oct16,border,neighbor links A(11+octant,1) 7bit;focus hit heights+horizontal distances u10 x12;shared HCMESH/material query;CloudSolar:64 signed intervals and cumulative T",
                contract.engineInterfaceHash) ||
            !HASH::ComputeSha256(
                "CBV(b0);AS(t0,s8);Hits(t1,s8);Materials(t16);Programs(t0,t1,s6);Lights(t1,t2,s7);Geometry(t0,s1,1024);Textures(t20);Deformed(t2,s8);PreviousSH(t0,s2,9);SH(u0,9);ProbeAtlas(u9);Sky(t0,s7);Focus(u10);Sampler(s0,clamp);Sampler(s1,wrap)",
                contract.rootLayoutHash))
            return false;
        const auto caps = GFX::QueryShaderDeviceCapabilities(graphics.device);
        contract.availableDeviceCaps = {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask};
        const auto shader =
            SHADER::ResolveBuiltinShaderEntry("HIKARI.EngineShader.Render3D_DynamicDiffuseGiCS",
                                              {"Main", SHADER::ShaderStage::Compute, "cs_6_5"}, contract);
        if (!shader) {
            DEBUGLOG::PushRenderError("Dynamic GI: " + shader.diagnostic);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        const auto bytes = shader.lease.GetDxil();
        desc.CS = {bytes.data(), bytes.size()};
        if (FAILED(graphics.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline))))
            return false;
        constexpr UINT64 focusBytes = kFocusValueCount * sizeof(float);
        const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT), readbackHeap(D3D12_HEAP_TYPE_READBACK);
        const auto focusDesc = CD3DX12_RESOURCE_DESC::Buffer(focusBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(focusBytes);
        if (FAILED(graphics.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &focusDesc,
                                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                            IID_PPV_ARGS(&focus))))
            return false;
        for (auto& readback : focusReadbacks)
            if (FAILED(graphics.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                IID_PPV_ARGS(&readback.buffer))))
                return false;
        return true;
    }
};

DynamicDiffuseGi::DynamicDiffuseGi() : state_(std::make_unique<State>()) {}
DynamicDiffuseGi::~DynamicDiffuseGi() = default;
const Stats& DynamicDiffuseGi::GetStats() const { return state_->stats; }
const char* ToString(Status status) noexcept {
    switch (status) {
    case Status::Off: return "Off";
    case Status::Unsupported: return "DXR / shader format unavailable";
    case Status::PreparingScene: return "Ray scene preparing";
    case Status::MissingOccluders: return "Ray scene lacks terrain or unresolved surfaces";
    case Status::MissingEnvironment: return "Environment radiance unavailable";
    case Status::ResourceFailure: return "GPU resource or shader unavailable";
    case Status::Ready: return "Ready";
    }
    return "Unknown";
}

Frame DynamicDiffuseGi::Prepare(const GFX::Context& graphics, const RAYTRACING::RuntimeRayTracingFrame& rt,
                                const MESHRENDERER::MeshMaterialGpuFrameView& materials,
                                const RAYTRACING::RuntimeRayLightingFrame& lighting,
                                std::span<const LIGHTING::IrradianceVolumeProvider> volumes, MATH::Vec3 cameraPosition,
                                MATH::Vec3 viewTarget, DynamicGiQuality quality, uint64_t frameSerial) {
    auto& state = *state_;
    if (state.lastFence != graphics.currentFrameRetireFenceValue)
        state.stats.updatedProbes = state.stats.primaryRays = 0;
    if (quality == DynamicGiQuality::Off) {
        if (state.device)
            Reset(graphics);
        return {};
    }
    const auto invalidateHistory = [&state] {
        for (auto& field : state.fields)
            field.history = false;
    };
    if (!graphics.device || !graphics.cmdList || !graphics.srvHeap || !graphics.frameFence ||
        !graphics.currentFrameRetireFenceValue || graphics.frameIndex >= GFX::kFrameResourceCount || !materials ||
        !MATH::IsFinite(cameraPosition)) {
        state.stats.status = Status::PreparingScene;
        invalidateHistory();
        return {};
    }
    if (state.device && (state.device != graphics.device || state.deviceGeneration != graphics.deviceGeneration ||
                         state.fence.Get() != graphics.frameFence))
        Reset(graphics);
    if (!state.device) {
        if (!GFX::RAYTRACING::QueryDxrCapabilities(graphics.device).SupportsInlineRayQuery()) {
            state.stats.status = Status::Unsupported;
            return {};
        }
        for (const auto format : {kProbeShFormat, kProbeAtlasFormat}) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
            if (FAILED(graphics.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
                !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) ||
                !(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) {
                state.stats.status = Status::Unsupported;
                return {};
            }
        }
        state.device = graphics.device;
        state.deviceGeneration = graphics.deviceGeneration;
        state.fence = graphics.frameFence;
    }
    if (!lighting.Matches(graphics, rt, {materials.dataVersion, materials.bindingVersion})) {
        state.stats.status = lighting.status == RAYTRACING::RuntimeRayLightingStatus::MissingEnvironment
                                 ? Status::MissingEnvironment
                                 : Status::PreparingScene;
        invalidateHistory();
        return {};
    }
    if (!RAYTRACING::CanTraceDiffuse(rt.excluded)) {
        state.stats.status = Status::MissingOccluders;
        invalidateHistory();
        return {};
    }
    // 描画側へは手置きの区域、近景、遠景の順（優先順）に渡す。
    const auto publish = [&state] {
        Frame frame;
        for (const auto& field : state.fields)
            if (field.active && field.history)
                frame.volumes[frame.count++] = field.Published().data;
        return frame;
    };
    const bool sameSource = state.sourceIdentity == rt.sceneIdentity && state.sourceLayout == rt.sceneLayoutVersion;
    if (state.lastFence == graphics.currentFrameRetireFenceValue) {
        if (sameSource && state.lastRaySceneVersion == rt.version && state.lastMaterialVersion == rt.materialVersion &&
            state.lastLightingContentHash == lighting.contentHash)
            return publish();
        return {};
    }
    GFX::GPU_PROFILE::ScopedGpuTimer timer(graphics.cmdList, GFX::GPU_PROFILE::Pass::DynamicDiffuseGi);
    state.stats.status = Status::ResourceFailure;
    if (frameSerial < state.retryFrame)
        return {};
    if (!state.EnsurePipeline(graphics)) {
        state.retryFrame = frameSerial + 60;
        invalidateHistory();
        return {};
    }
    const auto cascades = CascadeLayouts(quality);
    const LIGHTING::IrradianceVolumeProvider* region = SelectRegion(volumes, cameraPosition);
    std::optional<FieldLayout> regionLayout;
    if (region != nullptr)
        regionLayout = RegionLayout(*region, quality);
    const std::array<const FieldLayout*, kFieldCount> layouts{regionLayout ? &*regionLayout : nullptr, &cascades[0],
                                                              &cascades[1]};
    const bool previousFenceDone = state.fence->GetCompletedValue() >= state.lastFence;
    for (size_t index = 0; index < kFieldCount; ++index) {
        auto& field = state.fields[index];
        const std::string_view id =
            index == kRegionField && region != nullptr ? std::string_view{region->id} : std::string_view{};
        field.active = false;
        if (layouts[index] == nullptr)
            continue;
        // 引き継ぎに使った前の組は、それを読んだフレームの GPU が終わってから放す。
        if (!field.carry && field.legacy.bytes != 0 && previousFenceDone)
            field.legacy.Reset(graphics);
        const bool changed = !(field.layout == *layouts[index]) || field.id != id ||
                             (index == kRegionField && !SameVec3(field.fixedOrigin, region->origin));
        if (changed && field.sets[0].bytes != 0) {
            // 配置換えは旧 field の fence を待ってから割り当て、世代数で予算を超えない。その間この field は休む。
            if (!previousFenceDone) {
                field.history = false;
                continue;
            }
            // 近景・遠景の档位の切り替えでは公開中の組を一回だけ残し、新しい格子の最初の更新が近い probe を引き継ぐ。
            // 空から作り直すと、収まるまでの数秒、部屋が暗くなった（逆に明るくなった）。
            const bool keep = field.history && index != kRegionField;
            ProbeSet legacy;
            if (keep) {
                legacy = std::move(field.sets[field.published]);
                field.sets[field.published] = ProbeSet{};
            }
            field.Reset(graphics);
            field.legacy = std::move(legacy);
            field.carry = keep;
        }
        if (!field.sets[0].textures[0]) {
            if (index == kRegionField && field.failedId == id)
                continue;
            if (!field.sets[0].Create(graphics, *layouts[index]) || !field.sets[1].Create(graphics, *layouts[index])) {
                field.Reset(graphics);
                // 予算を超える区域はその区域が変わるまで使わない。cascade の失敗は全体を作り直す。
                if (index == kRegionField) {
                    field.failedId = id;
                    DEBUGLOG::PushRenderError("Dynamic GI: realtime volume '" + std::string(id) +
                                              "' exceeds the probe memory budget.");
                    continue;
                }
                for (auto& other : state.fields)
                    other.Reset(graphics);
                state.retryFrame = frameSerial + 60;
                return {};
            }
            field.layout = *layouts[index];
            field.id = id;
            field.failedId.clear();
            field.fixedOrigin = index == kRegionField ? region->origin : MATH::Vec3{};
        }
        field.active = true;
    }
    auto& upload = state.uploads[graphics.frameIndex];
    if (!upload.IsInitialized() && !upload.Init(graphics.device, 4096))
        return {};
    upload.ResetForFrame(graphics.currentFrameRetireFenceValue);
    std::array<DX::UploadAllocation, kFieldCount> constantBuffers;
    for (size_t index = 0; index < kFieldCount; ++index)
        if (state.fields[index].active && !(constantBuffers[index] = upload.Allocate(sizeof(Constants))))
            return {};
    auto* commands = graphics.cmdList;
    ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(state.root.Get());
    commands->SetPipelineState(state.pipeline.Get());
    RAYTRACING::BindRuntimeRayQueryRoots(commands, graphics, lighting, materials, kSkyParameter);
    commands->SetComputeRootUnorderedAccessView(kFocusParameter, state.focus->GetGPUVirtualAddress());
    // 前のフレームまでに GPU が書いた相機の ray の結果のうち、終わった中で一番新しいものを読む。
    for (auto& readback : state.focusReadbacks) {
        if (readback.fence <= state.focusFence || !GFX::IsCurrentFenceComplete(readback.fence))
            continue;
        std::array<float, kFocusValueCount> values{};
        const D3D12_RANGE range{0, sizeof(values)};
        void* mapped = nullptr;
        if (SUCCEEDED(readback.buffer->Map(0, &range, &mapped)) && mapped != nullptr) {
            std::memcpy(values.data(), mapped, sizeof(values));
            const D3D12_RANGE written{0, 0};
            readback.buffer->Unmap(0, &written);
            const FieldLayout& nearLayout = cascades[0];
            const NearFieldPlacement desired = PlaceNearField(
                values, cameraPosition.y, 0.5f * float(nearLayout.y - 1) * nearLayout.spacing.y, nearLayout.spacing.y);
            // 間隔より小さい揺れでは動かさない（格子の境で行き来しない）。
            if (!state.nearPlacement)
                state.nearPlacement = desired;
            if (std::abs(desired.height - state.nearPlacement->height) > nearLayout.spacing.y)
                state.nearPlacement->height = desired.height;
            if (std::abs(desired.reach - state.nearPlacement->reach) > nearLayout.spacing.x)
                state.nearPlacement->reach = desired.reach;
        }
        state.focusFence = readback.fence;
    }
    const NearFieldPlacement nearPlacement = state.nearPlacement.value_or(NearFieldPlacement{cameraPosition.y, 0.0f});
    state.stats.probes = state.stats.updatedProbes = 0;
    state.stats.probeBytes = 0;
    // 相機の cascade の中心は視線の水平方向へ幅の 3 割だけ前に置く。相機の後ろへ半分を割くと、見えている奥が近景から
    // 外れ、遠景（間隔 6m）が薄い壁の裏の明るさを部屋の中へ漏らす。
    MATH::Vec3 ahead{viewTarget.x - cameraPosition.x, 0.0f, viewTarget.z - cameraPosition.z};
    const float aheadLength = std::sqrt(ahead.x * ahead.x + ahead.z * ahead.z);
    ahead = aheadLength > 1e-4f && std::isfinite(aheadLength)
                ? MATH::Vec3{ahead.x / aheadLength, 0.0f, ahead.z / aheadLength}
                : MATH::Vec3{};
    for (size_t index = 0; index < kFieldCount; ++index) {
        auto& field = state.fields[index];
        if (!field.active)
            continue;
        const FieldLayout& layout = field.layout;
        auto& previous = field.carry ? field.legacy : field.sets[field.published];
        auto& target = field.sets[1 - field.published];
        const bool compatible = sameSource && (field.carry || (field.history && frameSerial == field.lastFrame + 1));
        // 履歴なしで始める時と、档位の切り替えで格子が広がった時（前の格子に無かった probe は空から始まる）は、
        // 最初の数回だけ全 probe を更新し、空の probe のまま暗い期間を短くする。
        if (!compatible || field.carry) {
            field.firstProbe = 0;
            // 空から始める時は反射を重ねる分だけ多く（一回で一回分の反射が積み上がる）。
            field.fullUpdates = compatible ? 3u : 6u;
        }
        const UINT budget = field.fullUpdates > 0 ? layout.x * layout.y * layout.z : layout.budget;
        if (index == kRegionField) {
            target.data.origin = field.fixedOrigin;
        } else {
            // probe は間隔の半分ずらした格子に置く。床や壁は整数座標に揃うことが多く、面に重なった probe は
            // 面の表裏を判定できずに板の反対側へ移されてしまう。
            const float spacing = layout.spacing.x;
            const auto gridOrigin = [spacing](float camera, UINT count) {
                return (std::floor(camera / spacing - 0.5f) + 0.5f - float(count / 2)) * spacing;
            };
            const float lead =
                (std::max)(0.3f * float(layout.x - 1) * spacing, index == kNearField ? nearPlacement.reach : 0.0f);
            target.data.origin = {gridOrigin(cameraPosition.x + ahead.x * lead, layout.x),
                                  gridOrigin(index == kNearField ? nearPlacement.height : cameraPosition.y, layout.y),
                                  gridOrigin(cameraPosition.z + ahead.z * lead, layout.z)};
        }
        Constants constants{};
        constants.previous = LIGHTPROBE::BuildIrradianceVolumeGpuData(previous.data, compatible ? 3.0f : 0.0f);
        constants.origin = {target.data.origin.x, target.data.origin.y, target.data.origin.z, 0.0f};
        constants.spacing = {layout.spacing.x, layout.spacing.y, layout.spacing.z, 0.0f};
        constants.counts = {layout.x, layout.y, layout.z, layout.x * layout.y * layout.z};
        constants.work = {UINT(state.sequence), field.firstProbe % constants.counts[3], budget, compatible ? 1u : 0u};
        constants.lighting = lighting.data;
        const MATH::Vec3 forward = viewTarget - cameraPosition;
        constants.focusOrigin = {cameraPosition.x, cameraPosition.y, cameraPosition.z,
                                 index == kNearField ? 1.0f : 0.0f};
        constants.focusDirection = {forward.x, forward.y, forward.z, 0.0f};
        std::memcpy(constantBuffers[index].cpuAddress, &constants, sizeof(constants));
        target.Transition(commands, true);
        commands->SetComputeRootConstantBufferView(0, constantBuffers[index].gpuAddress);
        commands->SetComputeRootDescriptorTable(kPreviousShParameter, previous.srvs[0].gpu);
        commands->SetComputeRootDescriptorTable(kPreviousShParameter + 1u, target.uavs[0].gpu);
        commands->SetComputeRootDescriptorTable(kPreviousShParameter + 2u, target.uavs[9].gpu);
        commands->Dispatch(constants.counts[3], 1, 1);
        target.Transition(commands, false);
        if (index == kNearField) {
            auto& readback = state.focusReadbacks[graphics.frameIndex];
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                state.focus.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->ResourceBarrier(1, &barrier);
            commands->CopyBufferRegion(readback.buffer.Get(), 0, state.focus.Get(), 0,
                                       kFocusValueCount * sizeof(float));
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(state.focus.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands->ResourceBarrier(1, &barrier);
            readback.fence = graphics.currentFrameRetireFenceValue;
        }
        target.data.valid = true;
        field.published = 1 - field.published;
        field.history = true;
        field.lastFrame = frameSerial;
        field.firstProbe = (constants.work[1] + budget) % constants.counts[3];
        if (field.fullUpdates > 0)
            --field.fullUpdates;
        field.carry = false;
        state.stats.probes += constants.counts[3];
        state.stats.updatedProbes += budget;
        state.stats.probeBytes += previous.bytes + target.bytes;
    }
    state.lastFence = graphics.currentFrameRetireFenceValue;
    state.sourceIdentity = rt.sceneIdentity;
    state.sourceLayout = rt.sceneLayoutVersion;
    state.lastRaySceneVersion = rt.version;
    state.lastMaterialVersion = rt.materialVersion;
    state.lastLightingContentHash = lighting.contentHash;
    ++state.sequence;
    state.stats.status = Status::Ready;
    state.stats.primaryRays = state.stats.updatedProbes * kProbeRayCount;
    state.stats.localLights = lighting.data.scene[2];
    state.stats.droppedLights = lighting.droppedLights;
    state.stats.sequence = state.sequence;
    return publish();
}
void DynamicDiffuseGi::Reset(const GFX::Context& graphics) {
    auto& state = *state_;
    for (auto& field : state.fields)
        field.Reset(graphics);
    for (auto& upload : state.uploads)
        upload.Finalize(&graphics);
    GFX::RetireD3D12ObjectForFrame(state.pipeline, graphics, "DynamicGI.PSO");
    GFX::RetireD3D12ObjectForFrame(state.root, graphics, "DynamicGI.Root");
    GFX::RetireD3D12ObjectForFrame(state.focus, graphics, "DynamicGI.Focus");
    for (auto& readback : state.focusReadbacks) {
        GFX::RetireD3D12ObjectForFrame(readback.buffer, graphics, "DynamicGI.FocusReadback");
        readback.fence = 0;
    }
    state.focusFence = 0;
    state.nearPlacement.reset();
    state.device = nullptr;
    state.fence.Reset();
    state.lastFence = 0;
    state.sourceIdentity = nullptr;
    state.sequence = state.retryFrame = 0;
    state.stats = {};
}
} // namespace HIKARI::RENDER3D::DYNAMICGI
