#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Lighting/HIKARI_DynamicDiffuseGi.h"
#include "Render3D/Lighting/HIKARI_DynamicGiPlacement.h"
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
// 光線クエリの共通根パラメーターの後に、probe atlas の UAV・空・相機 ray の UAV の順に並べる。
constexpr UINT kAtlasParameter = RAYTRACING::kRuntimeRayQueryRootParameterCount;
constexpr UINT kSkyParameter = kAtlasParameter + 1u;
// 近景の高さを決める相機の ray の結果（当たった点の高さ）を書く UAV。中央・上下左右・真下の 6 本。
constexpr UINT kFocusParameter = kSkyParameter + 1u;
using DETAIL::kFocusValueCount;
using DETAIL::NearFieldPlacement;
using DETAIL::PlaceNearField;
using DETAIL::ProbeGridOrigin;
// 描画側が読む probe atlas。probe ごとに放射照度と深度 moment の八面体、円盤、状態の tile を持つ
// （HIKARI_DynamicGiProbeAtlas.hlsli の HIKARI_DYNAMIC_GI_TILE_* と同じ寸法）。更新も前の組の atlas を読んで混ぜる。
constexpr auto kProbeAtlasFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr UINT kProbeAtlasTileWidth = 30, kProbeAtlasTileHeight = 18;
// probe 一回の更新の ray 数（Render3D_DynamicDiffuseGiCS.hlsl の kRayCount）。
constexpr uint32_t kProbeRayCount = 192;
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
    std::array<LIGHTPROBE::IrradianceVolumeGpuData, kFieldCount> previous{};
    std::array<uint32_t, 4> field{};
    MATH::Vec4 origin{0, 0, 0, 0}; // xyz 格子の原点
    MATH::Vec4 spacing{0, 0, 0, 0};
    std::array<uint32_t, 4> counts{}, work{};
    RAYTRACING::RuntimeRayLightingGpuData lighting{};
    // 相機の位置（w は近景の更新で 1、その時だけ ray を撃つ）と視線の向き。
    MATH::Vec4 focusOrigin{}, focusDirection{};
    std::array<uint32_t, 4> priorityOrigin{}, prioritySize{}; // xyz 格子範囲、w 予算/開始点
    // 履歴の重み・光照の変化の検出・収束の判定と、probe の再配置・分類の閾値（shader の gBlend / gRelocate）。
    MATH::Vec4 blend{}, relocate{};
};
static_assert(sizeof(Constants) == 576);

// 形状が動いた範囲を局所的に先行更新する。通常の巡回は残し、追加 ray は通常予算の二倍まで。
UINT SchedulePriority(Constants& constants, MATH::Vec4 minimum, MATH::Vec4 maximum, UINT budget) {
    const float lo[]{minimum.x, minimum.y, minimum.z}, hi[]{maximum.x, maximum.y, maximum.z};
    const float origin[]{constants.origin.x, constants.origin.y, constants.origin.z};
    const float spacing[]{constants.spacing.x, constants.spacing.y, constants.spacing.z};
    UINT count = minimum.w > 0 ? 1u : 0u;
    for (UINT axis = 0; axis < 3 && count > 0; ++axis) {
        const int first =
            std::clamp(int(std::floor((lo[axis] - origin[axis]) / spacing[axis])) - 2, 0, int(constants.counts[axis]));
        const int last =
            std::clamp(int(std::ceil((hi[axis] - origin[axis]) / spacing[axis])) + 3, 0, int(constants.counts[axis]));
        constants.priorityOrigin[axis] = UINT(first);
        constants.prioritySize[axis] = UINT(last - first);
        count *= UINT(last - first);
    }
    constants.priorityOrigin[3] = std::min(count, budget * 2u);
    constants.prioritySize[3] = count > 0 ? (constants.work[0] * constants.priorityOrigin[3]) % count : 0;
    const auto& previous = constants.previous[constants.field[0]];
    const float previousOrigin[]{previous.originSampling.x, previous.originSampling.y, previous.originSampling.z};
    const float previousSpacing[]{previous.spacingIntensity.x, previous.spacingIntensity.y,
                                  previous.spacingIntensity.z};
    const float previousCount[]{previous.countsProbeCount.x, previous.countsProbeCount.y, previous.countsProbeCount.z};
    bool sameGrid = true;
    for (UINT axis = 0; axis < 3; ++axis)
        sameGrid &= origin[axis] == previousOrigin[axis] && spacing[axis] == previousSpacing[axis] &&
                    float(constants.counts[axis]) == previousCount[axis];
    if (count == 0 && sameGrid)
        return 0;
    // GPU と同じ選択を数える。新規格子（相機の移動で露出した端）は追加予算とは別に即時初期化される。
    UINT extra = 0;
    for (UINT i = 0; i < constants.counts[3]; ++i) {
        const UINT cell[]{i % constants.counts[0], (i / constants.counts[0]) % constants.counts[1],
                          i / (constants.counts[0] * constants.counts[1])};
        UINT linear = 0, stride = 1, priorityRank = 0, priorityStride = 1;
        bool newProbe = false, affected = count > 0;
        for (UINT axis = 0; axis < 3; ++axis) {
            const int delta = int(cell[axis]) - int(constants.counts[axis] / 2u);
            linear += UINT(std::min(std::abs(delta) * 2 - int(delta > 0), int(constants.counts[axis]) - 1)) * stride;
            stride *= constants.counts[axis];
            const float oldCell = std::round((origin[axis] + float(cell[axis]) * spacing[axis] - previousOrigin[axis]) /
                                             previousSpacing[axis]);
            newProbe |= oldCell < 0 || oldCell >= previousCount[axis];
            const UINT offset = cell[axis] - constants.priorityOrigin[axis];
            affected &= cell[axis] >= constants.priorityOrigin[axis] && offset < constants.prioritySize[axis];
            priorityRank += offset * priorityStride;
            priorityStride *= constants.prioritySize[axis];
        }
        const bool scheduled =
            (linear + constants.counts[3] - constants.work[1]) % constants.counts[3] < constants.work[2];
        const bool priority =
            affected && (priorityRank + count - constants.prioritySize[3]) % count < constants.priorityOrigin[3];
        extra += !scheduled && (newProbe || priority);
    }
    return extra;
}

struct ProbeSet {
    ComPtr<ID3D12Resource> atlasTexture;
    RenderResourceView uav;
    TextureResourceHandle atlas;
    LIGHTPROBE::LightProbeVolumeRuntimeData data;
    uint64_t bytes = 0;
    void Reset(const GFX::Context& graphics) {
        RetireRenderResourceDescriptor(uav, graphics, "DynamicGI.UAV");
        uav = {};
        if (atlas)
            ReleaseTextureResource(atlas);
        atlas = {};
        GFX::RetireD3D12ObjectForFrame(atlasTexture, graphics, "DynamicGI.Probe");
        data = {};
        bytes = 0;
    }
    static D3D12_RESOURCE_DESC Description(UINT x, UINT y, UINT z) {
        return CD3DX12_RESOURCE_DESC::Tex2D(kProbeAtlasFormat, x * y * kProbeAtlasTileWidth, z * kProbeAtlasTileHeight,
                                            1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
    bool Create(const GFX::Context& graphics, const FieldLayout& layout) {
        const UINT x = layout.x, y = layout.y, z = layout.z;
        const auto desc = Description(x, y, z);
        const auto allocation = graphics.device->GetResourceAllocationInfo(0, 1, &desc);
        if (allocation.SizeInBytes > kProbeBudgetBytes / 2)
            return false;
        bytes = allocation.SizeInBytes;
        uav = AllocateRenderResourceDescriptor();
        if (!uav.IsValid())
            return false;
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(graphics.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, kReadState, nullptr,
                                                            IID_PPV_ARGS(&atlasTexture))))
            return false;
        atlasTexture->SetName(L"HIKARI.DynamicDiffuseGI.ProbeAtlas");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        graphics.device->CreateUnorderedAccessView(atlasTexture.Get(), nullptr, &uavDesc, uav.cpu);
        atlas = RegisterTextureResourceFromNative(atlasTexture.Get(), kProbeAtlasFormat);
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
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            atlasTexture.Get(), toWrite ? kReadState : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            toWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : kReadState);
        commands->ResourceBarrier(1, &barrier);
    }
};
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
    uint64_t lastFrame = 0, lastUseFence = 0, legacyRetireFence = 0;
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
        lastUseFence = legacyRetireFence = 0;
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
    MATH::Vec4 changedMinimum{0, 0, 0, 0}, changedMaximum{0, 0, 0, 0};
    uint32_t changeFrames = 0;

    bool EnsurePipeline(const GFX::Context& graphics) {
        if (pipeline)
            return true;
        // 共通の後に probe atlas の UAV・空・相機 ray の UAV。
        std::array<CD3DX12_ROOT_PARAMETER, kFocusParameter + 1u> parameters;
        std::array<CD3DX12_DESCRIPTOR_RANGE, 4> ranges;
        RAYTRACING::InitRuntimeRayQueryRootParameters(parameters, ranges, kSkyParameter);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        parameters[kAtlasParameter].InitAsDescriptorTable(1, &ranges[3]);
        parameters[kFocusParameter].InitAsUnorderedAccessView(1);
        const auto samplers = RAYTRACING::RuntimeRayQueryStaticSamplers();
        const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(parameters.size()), parameters.data(), 2, samplers.data());
        if (FAILED(GFX::CreateRootSignature(graphics.device, rootDesc, &root)))
            return false;
        namespace SHADER = ASSETS::SHADER;
        SHADER::ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = "HIKARI.DynamicDiffuseGi";
        contract.engineInterfaceVersion = 10;
        contract.rootLayoutId = "HIKARI.DynamicDiffuseGi.Root";
        contract.rootLayoutVersion = 3;
        if (!HASH::ComputeSha256(
                "DynamicDiffuseGiCB:576;previous volumes[3],field index;origin,axis spacing,focus origin/direction,priority origin/budget,size/start,blend hysteresis/light change/variability,relocate min front/backface/limit/reach;atlas RGBA16F tile30x18 irradiance^(1/5)+sky oct8 cosine accumulated,moments+active oct16 backface x0.2,fixed-ray triangle disk oct8 at(0,10),state(28,0) offset+active,(28,1) updates/variation/backface/fixed level;focus hit heights+horizontal distances u1 x12;shared HCMESH/material query;CloudSolar:64 signed intervals and cumulative T",
                contract.engineInterfaceHash) ||
            !HASH::ComputeSha256(
                "CBV(b0);AS(t0,s8);Hits(t1,s8);Materials(t16);Programs(t0,t1,s6);Lights(t1,t2,s7);Geometry(t0,s1,1024);Textures(t20);Deformed(t2,s8);ProbeAtlas(u0);Sky(t0,s7);Focus(u1);Sampler(s0,clamp);Sampler(s1,wrap)",
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
    case Status::MissingOccluders: return "Ray scene has unresolved surfaces";
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
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{kProbeAtlasFormat};
        if (FAILED(graphics.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
            !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) ||
            !(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) {
            state.stats.status = Status::Unsupported;
            return {};
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
    if (!sameSource || state.changeFrames == 0)
        state.changedMinimum = state.changedMaximum = {0, 0, 0, 0};
    if (sameSource && rt.changedMinimum.w > 0) {
        if (state.changedMinimum.w == 0) {
            state.changedMinimum = rt.changedMinimum;
            state.changedMaximum = rt.changedMaximum;
        } else {
            state.changedMinimum = {std::min(state.changedMinimum.x, rt.changedMinimum.x),
                                    std::min(state.changedMinimum.y, rt.changedMinimum.y),
                                    std::min(state.changedMinimum.z, rt.changedMinimum.z), 1};
            state.changedMaximum = {std::max(state.changedMaximum.x, rt.changedMaximum.x),
                                    std::max(state.changedMaximum.y, rt.changedMaximum.y),
                                    std::max(state.changedMaximum.z, rt.changedMaximum.z), 1};
        }
        state.changeFrames = 16;
    } else if (state.changeFrames > 0) {
        --state.changeFrames;
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
    const uint64_t completedFence = state.fence->GetCompletedValue();
    for (size_t index = 0; index < kFieldCount; ++index) {
        auto& field = state.fields[index];
        const std::string_view id =
            index == kRegionField && region != nullptr ? std::string_view{region->id} : std::string_view{};
        field.active = false;
        if (layouts[index] == nullptr)
            continue;
        // 引き継ぎに使った前の組は、それを読んだフレームの GPU が終わってから放す。
        if (!field.carry && field.legacy.bytes != 0 && completedFence >= field.legacyRetireFence)
            field.legacy.Reset(graphics);
        const bool changed = !(field.layout == *layouts[index]) || field.id != id ||
                             (index == kRegionField && !SameVec3(field.fixedOrigin, region->origin));
        if (changed && field.sets[0].bytes != 0) {
            // 配置換えは旧 field の fence を待ってから割り当て、世代数で予算を超えない。その間この field は休む。
            if (completedFence < field.lastUseFence) {
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
        if (!field.sets[0].atlasTexture) {
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
            state.nearPlacement =
                PlaceNearField(values, cameraPosition.y, nearLayout.y, nearLayout.spacing.y, state.nearPlacement);
        }
        state.focusFence = readback.fence;
    }
    const NearFieldPlacement nearPlacement = state.nearPlacement.value_or(
        NearFieldPlacement{ProbeGridOrigin(cameraPosition.y, cascades[0].y, cascades[0].spacing.y), 0.0f});
    state.stats.probes = state.stats.updatedProbes = 0;
    state.stats.probeBytes = 0;
    // 相機の cascade の中心は視線の水平方向へ幅の 3 割だけ前に置く。相機の後ろへ半分を割くと、見えている奥が近景から
    // 外れ、遠景（間隔 6m）が薄い壁の裏の明るさを部屋の中へ漏らす。
    MATH::Vec3 ahead{viewTarget.x - cameraPosition.x, 0.0f, viewTarget.z - cameraPosition.z};
    const float aheadLength = std::sqrt(ahead.x * ahead.x + ahead.z * ahead.z);
    ahead = aheadLength > 1e-4f && std::isfinite(aheadLength)
                ? MATH::Vec3{ahead.x / aheadLength, 0.0f, ahead.z / aheadLength}
                : MATH::Vec3{};
    // 全層を更新する前の snapshot。後の層が今フレームの途中結果を読まない。
    std::array<LIGHTPROBE::IrradianceVolumeGpuData, kFieldCount> previousVolumes{};
    for (size_t index = 0; index < kFieldCount; ++index) {
        const auto& field = state.fields[index];
        const bool compatible =
            field.active && sameSource && (field.carry || (field.history && frameSerial == field.lastFrame + 1));
        const auto& previous = field.carry ? field.legacy : field.Published();
        previousVolumes[index] = LIGHTPROBE::BuildIrradianceVolumeGpuData(previous.data, compatible ? 3.0f : 0.0f);
    }
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
            const float lead =
                (std::max)(0.3f * float(layout.x - 1) * spacing, index == kNearField ? nearPlacement.reach : 0.0f);
            target.data.origin = {ProbeGridOrigin(cameraPosition.x + ahead.x * lead, layout.x, spacing),
                                  index == kNearField ? nearPlacement.originY
                                                      : ProbeGridOrigin(cameraPosition.y, layout.y, spacing),
                                  ProbeGridOrigin(cameraPosition.z + ahead.z * lead, layout.z, spacing)};
        }
        Constants constants{};
        constants.previous = previousVolumes;
        constants.field[0] = static_cast<uint32_t>(index);
        constants.origin = {target.data.origin.x, target.data.origin.y, target.data.origin.z, 0.0f};
        constants.spacing = {layout.spacing.x, layout.spacing.y, layout.spacing.z, 0.0f};
        constants.counts = {layout.x, layout.y, layout.z, layout.x * layout.y * layout.z};
        constants.work = {UINT(state.sequence), field.firstProbe % constants.counts[3], budget, compatible ? 1u : 0u};
        constants.lighting = lighting.data;
        const MATH::Vec3 forward = viewTarget - cameraPosition;
        constants.focusOrigin = {cameraPosition.x, cameraPosition.y, cameraPosition.z,
                                 index == kNearField ? 1.0f : 0.0f};
        constants.focusDirection = {forward.x, forward.y, forward.z, 0.0f};
        const float minimumSpacing = (std::min)({layout.spacing.x, layout.spacing.y, layout.spacing.z});
        const float maximumSpacing = (std::max)({layout.spacing.x, layout.spacing.y, layout.spacing.z});
        // 収束後の履歴は 0.97（RTXGI）。固定 ray の明るさが 15% 変わり始めたら弱め、50% で 0.5 まで下げる。
        // texel の相対変化の移動平均が 2% 未満なら収束とみなし、4 回に 1 回だけ更新する。
        constants.blend = {0.97f, 0.15f, 0.5f, 0.02f};
        // 面から間隔の 2 割以上離す。固定 ray の 1/4 超が裏面なら形状の中。移動は間隔の 45% まで（RTXGI）。
        // 間隔の 1.75 倍以内に形状が無い probe は非活性（描画側は支持に数えず、下位の層へ渡す）。
        constants.relocate = {0.2f * minimumSpacing, 0.25f, 0.45f, 1.75f * maximumSpacing};
        const UINT extraUpdates =
            compatible ? SchedulePriority(constants, state.changedMinimum, state.changedMaximum, layout.budget) : 0;
        std::memcpy(constantBuffers[index].cpuAddress, &constants, sizeof(constants));
        target.Transition(commands, true);
        commands->SetComputeRootConstantBufferView(0, constantBuffers[index].gpuAddress);
        commands->SetComputeRootDescriptorTable(kAtlasParameter, target.uav.gpu);
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
        if (field.carry)
            field.legacyRetireFence = graphics.currentFrameRetireFenceValue;
        field.carry = false;
        field.lastUseFence = graphics.currentFrameRetireFenceValue;
        state.stats.probes += constants.counts[3];
        state.stats.updatedProbes += budget + extraUpdates;
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
    state.changedMinimum = state.changedMaximum = {0, 0, 0, 0};
    state.changeFrames = 0;
    state.device = nullptr;
    state.fence.Reset();
    state.lastFence = 0;
    state.sourceIdentity = nullptr;
    state.sequence = state.retryFrame = 0;
    state.stats = {};
}
} // namespace HIKARI::RENDER3D::DYNAMICGI
