#include "Render3D/Vfx/Fluid/HIKARI_FluidRender.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

#include <d3dx12.h>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

// 1 視図の流体の描画の記録：定数を詰め、光照体積・相機体積・步进・合成を順に記録する。

namespace HIKARI::RENDER3D::FLUID::INTERNAL {
namespace {
struct alignas(16) RenderDomainGpu {
    float toGrid[3][4];
    float cells[4];
    float look[4];
    float emission[4];
    float emissionHot[4];
    float optics[4];
    float detail[4];
    float sun[4];
};
static_assert(sizeof(RenderDomainGpu) == 160u);

struct alignas(16) RenderConstants {
    MATH::Mat4 invRelativeViewProj{};
    float camera[4];
    float forward[4];
    float size[4];
    float sun[4];
    float sunRadiance[4];
    float ambient[4];
    float cameraVolume[4];
    MATH::Mat4 lightViewProj[4]{};
    float shadowSplits[4];
    float shadowParams[4];
    float shadowSampling[4];
    float shadowBias[4];
    LIGHTING::CloudSolarVisibilityGpuData solarTable{};
    RenderDomainGpu domains[kMaximumRenderedFluidDomains];
};
static_assert(sizeof(RenderConstants) == 1200u);

void Set4(float (&out)[4], float x, float y, float z, float w) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

// 世界から格子（格、角が原点）への行と、外観の値を詰める。
// 粒子が乗る流体の域 1 つ（Render3D_ParticleSimulation.hlsl の FluidFlowDomain と同じ並び）。motion は域が世界で動く速さ、
// injection は注入の緩衝の半分の分辨率の格の数と、受け取るかの印。
struct alignas(16) FlowDomainGpu {
    float toGrid[3][4];
    float cells[4];
    float motion[4];
    uint32_t injection[4];
};
static_assert(sizeof(FlowDomainGpu) == 96u);

// 世界から格子（格、角が原点）への行と、格の数・大きさ。
void PackToGrid(const FluidRenderDomain& domain, float (&toGrid)[3][4], float (&cells)[4]) {
    const float h = domain.layout.cellSize;
    const float n[3] = {static_cast<float>(domain.layout.cells[0]), static_cast<float>(domain.layout.cells[1]),
                        static_cast<float>(domain.layout.cells[2])};
    for (int row = 0; row < 3; ++row) {
        const MATH::Vec3 axis = MATH::RotateVector(
            domain.rotation, {row == 0 ? 1.0f : 0.0f, row == 1 ? 1.0f : 0.0f, row == 2 ? 1.0f : 0.0f});
        Set4(toGrid[row], axis.x / h, axis.y / h, axis.z / h, -MATH::Dot(axis, domain.position) / h + 0.5f * n[row]);
    }
    Set4(cells, n[0], n[1], n[2], h);
}

RenderDomainGpu PackDomain(const FluidRenderDomain& domain, const FluidRenderView& view) {
    RenderDomainGpu gpu{};
    const FluidDomainSettings& look = domain.settings;
    PackToGrid(domain, gpu.toGrid, gpu.cells);
    const MATH::Vec3 axes[3] = {MATH::RotateVector(domain.rotation, {1, 0, 0}),
                                MATH::RotateVector(domain.rotation, {0, 1, 0}),
                                MATH::RotateVector(domain.rotation, {0, 0, 1})};
    const MATH::Vec4& tint = domain.colorMultiplier;
    Set4(gpu.look, look.scatteringColor.x * tint.x, look.scatteringColor.y * tint.y, look.scatteringColor.z * tint.z,
         look.extinction * (std::max)(tint.w, 0.0f));
    const float emission = look.emissionIntensity * view.preExposure;
    Set4(gpu.emission, look.emissionColor.x * emission, look.emissionColor.y * emission,
         look.emissionColor.z * emission, look.emissionTemperature);
    Set4(gpu.emissionHot, look.emissionHotColor.x * emission, look.emissionHotColor.y * emission,
         look.emissionHotColor.z * emission, look.emissionHotTemperature);
    Set4(gpu.optics, look.anisotropy, look.multipleScattering, look.ambientStrength, look.detailStrength);
    // 開いた面だけ面から 4 格で密度を消す（閉じた面は本当の壁なので消さない）。z は開いた面の印。
    // detailScale は細部の模様の大きさ。ノイズの 1 周期は基本の模様 4 つ分。w は最後の子步の後に貯まった時間 [s]
    // （子步 1 つ以下）：步进と烟の影の表がこの分だけ流れに沿って遡って密度を読む（ExtrapolatedSmoke）。
    Set4(gpu.detail, 1.0f / (4.0f * (std::max)(look.detailScale, 0.01f)), domain.simulationSeconds,
         static_cast<float>(kFluidAllFaces & ~FluidClosedFaces(look.boundary)), domain.pendingSeconds);
    MATH::Vec3 sun{MATH::Dot(axes[0], view.sunDirection), MATH::Dot(axes[1], view.sunDirection),
                   MATH::Dot(axes[2], view.sunDirection)};
    const float length = MATH::Length(sun);
    sun = length > 1.0e-6f ? sun * (1.0f / length) : MATH::Vec3{0.0f, 1.0f, 0.0f};
    Set4(gpu.sun, sun.x, sun.y, sun.z, domain.layout.cellColor ? 1.0f : 0.0f);
    return gpu;
}

// 域の箱の角までの最も遠い距離（相機体積の奥の端）。
float FarthestCorner(std::span<const FluidRenderDomain> domains, const MATH::Vec3& camera, float nearest) {
    float farthest = nearest * 2.0f;
    for (const FluidRenderDomain& domain : domains) {
        for (uint32_t corner = 0u; corner < 8u; ++corner) {
            MATH::Vec3 local{};
            for (int axis = 0; axis < 3; ++axis)
                (&local.x)[axis] = ((corner >> axis) & 1u ? 0.5f : -0.5f) *
                                   static_cast<float>(domain.layout.cells[axis]) * domain.layout.cellSize;
            const MATH::Vec3 world = domain.position + MATH::RotateVector(domain.rotation, local);
            farthest = (std::max)(farthest, MATH::Length(world - camera));
        }
    }
    return (std::min)(farthest, 5000.0f);
}
} // namespace

bool FluidRenderer::Render(const GFX::Context& context, const FluidRenderView& view,
                           std::span<FluidRenderDomain> domains, const FluidQualityPolicy& policy,
                           const std::function<bool()>& bindSceneTarget) {
    const size_t count = (std::min)(domains.size(), static_cast<size_t>(kMaximumRenderedFluidDomains));
    if (count == 0u || view.renderWidth == 0u || view.renderHeight == 0u || view.sceneDepthSrv.ptr == 0u)
        return true;
    if (!EnsurePipelines(context.device, view.sceneColorFormat))
        return false;
    ViewState& state = views_[view.viewKey];
    if (!EnsureView(context, state, view.renderWidth, view.renderHeight))
        return false;
    auto& upload = uploads_[context.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() && !upload.Init(context.device, 16u * 1024u, GFX::UPLOAD_TRAFFIC::Owner::Fluids))
        return false;
    upload.ResetForFrame(context.currentFrameRetireFenceValue);

    RenderConstants constants{};
    constants.invRelativeViewProj = view.invRelativeViewProj;
    Set4(constants.camera, view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z, policy.marchStepCells);
    Set4(constants.forward, view.cameraForward.x, view.cameraForward.y, view.cameraForward.z,
         static_cast<float>(policy.marchSteps));
    const uint32_t halfWidth = (view.renderWidth + 1u) / 2u, halfHeight = (view.renderHeight + 1u) / 2u;
    Set4(constants.size, static_cast<float>(halfWidth), static_cast<float>(halfHeight),
         static_cast<float>(view.renderWidth), static_cast<float>(view.renderHeight));
    const bool shadowed =
        view.sceneShadows && SHADOW::IsDirectionalShadowEnabled() && SHADOW::GetDirectionalShadowSrv().ptr != 0u;
    Set4(constants.sun, view.sunDirection.x, view.sunDirection.y, view.sunDirection.z, shadowed ? 1.0f : 0.0f);
    Set4(constants.sunRadiance, view.sunIlluminance.x * view.preExposure, view.sunIlluminance.y * view.preExposure,
         view.sunIlluminance.z * view.preExposure, 0.0f);
    Set4(constants.ambient, view.ambientRadiance.x * view.preExposure, view.ambientRadiance.y * view.preExposure,
         view.ambientRadiance.z * view.preExposure, static_cast<float>(count));
    const float farthest = FarthestCorner(domains.first(count), view.cameraPosition, kCameraVolumeNear);
    Set4(constants.cameraVolume, kCameraVolumeNear, std::log(farthest / kCameraVolumeNear),
         static_cast<float>(kCameraVolumeSlices), kCameraVolumeSamples);
    const auto& cascades = SHADOW::GetDirectionalCascadeLightViewProj();
    for (uint32_t index = 0u; index < 4u && index < cascades.size(); ++index)
        constants.lightViewProj[index] = cascades[index];
    const MATH::Vec4 splits = SHADOW::GetDirectionalCascadeSplitDistances();
    Set4(constants.shadowSplits, splits.x, splits.y, splits.z, splits.w);
    Set4(constants.shadowParams, static_cast<float>(SHADOW::GetDirectionalCascadeCount()), 0.0f, 0.5f, 0.5f);
    const float texel = SHADOW::GetShadowAtlasTexelSize();
    Set4(constants.shadowSampling, (std::max)(GetRenderQualitySettings().directionalShadow.depthBias, 0.0f), texel,
         texel, 0.0f);
    const MATH::Vec4 bias = SHADOW::GetDirectionalCascadeDepthBiasScales();
    Set4(constants.shadowBias, bias.x, bias.y, bias.z, bias.w);
    for (size_t index = 0u; index < count; ++index)
        constants.domains[index] = PackDomain(domains[index], view);
    const auto allocation = upload.Allocate(sizeof(constants));
    if (!allocation)
        return false;
    std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
    const D3D12_GPU_VIRTUAL_ADDRESS address = allocation.gpuAddress;

    ID3D12GraphicsCommandList* commands = context.cmdList;
    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorPost, "FluidSmoke.Render");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::FluidRender);
    ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    if (!EnsureNoise(context, address))
        return false;
    for (size_t index = 0u; index < count; ++index) {
        TransitionVolume(commands, *domains[index].density, kRead);
        TransitionVolume(commands, *domains[index].velocity, kRead);
    }
    // 視図ごとの入力：空いた域の枠は 1 texel の体積で埋める（shader は域の数より先を読まない）。
    const auto volumeAt = [&](size_t index, FluidVolume* FluidRenderDomain::* member) {
        return index < count ? (domains[index].*member)->srv.gpu : fallbackVolume_.srv.gpu;
    };
    // 影を使わない時の t16 は場面の深度（読める状態の 2D）で埋める。
    const D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = shadowed ? SHADOW::GetDirectionalShadowSrv() : view.sceneDepthSrv;
    // 全ての入力を束ねる（計算と全画面で同じ並び）。步进の結果（13、14）はまだ無いので場面の深度で埋め（読まない）、
    // 合成の前に上書きする。
    const auto bindInputs = [&](auto setTable) {
        for (size_t slot = 0u; slot < kMaximumRenderedFluidDomains; ++slot) {
            setTable(kFirstInput + kDensity + static_cast<UINT>(slot), volumeAt(slot, &FluidRenderDomain::density));
            setTable(kFirstInput + kVelocity + static_cast<UINT>(slot), volumeAt(slot, &FluidRenderDomain::velocity));
            setTable(kFirstInput + kLight + static_cast<UINT>(slot), volumeAt(slot, &FluidRenderDomain::light));
        }
        setTable(kFirstInput + kSceneDepth, view.sceneDepthSrv);
        for (UINT input : {kInputColor, kInputDepth})
            setTable(kFirstInput + input, view.sceneDepthSrv);
        setTable(kFirstInput + kNoise, noiseVolume_.srv.gpu);
        setTable(kFirstInput + kShadow, shadowSrv);
    };
    const auto setCompute = [&](UINT index, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
        commands->SetComputeRootDescriptorTable(index, handle);
    };
    const auto activity = PrepareActivity(commands, domains, count);

    // 光照体積（域ごと、t0 に自分の密度）。密度・太陽の域の局所での向き・消光・格の大きさが前に書いた時と同じなら
    // 書き直さない（子步の無いフレームや、同じフレームの 2 つ目の視図）。
    commands->SetComputeRootSignature(root_.Get());
    commands->SetComputeRootConstantBufferView(kConstants, address);
    bindInputs(setCompute);
    for (UINT slot = 0u; slot < kMaximumRenderedFluidDomains; ++slot)
        commands->SetComputeRootShaderResourceView(kActivity + slot, activity[slot]);
    commands->SetPipelineState(light_.Get());
    std::optional<GFX::GPU_PROFILE::ScopedGpuTimer> lightTimer{std::in_place, commands,
                                                               GFX::GPU_PROFILE::Pass::FluidRenderLight};
    for (size_t index = 0u; index < count; ++index) {
        const RenderDomainGpu& gpu = constants.domains[index];
        const FluidLightKey key{
            domains[index].densityVersion, {gpu.sun[0], gpu.sun[1], gpu.sun[2]}, gpu.look[3], gpu.cells[3]};
        if (domains[index].lightKey != nullptr && *domains[index].lightKey == key)
            continue;
        if (domains[index].lightKey != nullptr)
            *domains[index].lightKey = key;
        FluidVolume& light = *domains[index].light;
        TransitionVolume(commands, light, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const uint32_t pass[4] = {static_cast<uint32_t>(index), 0u, 0u, 0u};
        commands->SetComputeRoot32BitConstants(kPassConstants, 4, pass, 0);
        commands->SetComputeRootDescriptorTable(kFirstInput + kDensity, domains[index].density->srv.gpu);
        commands->SetComputeRootDescriptorTable(kOutput, light.uav.gpu);
        commands->Dispatch((light.size[0] + 3u) / 4u, (light.size[1] + 3u) / 4u, (light.size[2] + 3u) / 4u);
        TransitionVolume(commands, light, kRead);
    }
    lightTimer.reset();
    // 透明物体のための相機体積。
    std::optional<GFX::GPU_PROFILE::ScopedGpuTimer> cameraVolumeTimer{std::in_place, commands,
                                                                      GFX::GPU_PROFILE::Pass::FluidRenderCameraVolume};
    commands->SetPipelineState(cameraVolume_.Get());
    commands->SetComputeRootDescriptorTable(kFirstInput + kDensity, volumeAt(0u, &FluidRenderDomain::density));
    TransitionVolume(commands, state.cameraVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetComputeRootDescriptorTable(kOutput, state.cameraVolume.uav.gpu);
    commands->Dispatch((state.cameraVolume.size[0] + 7u) / 8u, (state.cameraVolume.size[1] + 7u) / 8u, 1u);
    TransitionVolume(commands, state.cameraVolume, kRead);
    cameraVolumeTimer.reset();

    // 半分辨率の步进（合成まで同じ区間で計る）。
    GFX::GPU_PROFILE::ScopedGpuTimer marchTimer(commands, GFX::GPU_PROFILE::Pass::FluidRenderMarch);
    const auto setGraphics = [&](UINT index, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
        commands->SetGraphicsRootDescriptorTable(index, handle);
    };
    const auto bindActivity = [&] {
        for (UINT slot = 0u; slot < kMaximumRenderedFluidDomains; ++slot)
            commands->SetGraphicsRootShaderResourceView(kActivity + slot, activity[slot]);
    };
    commands->SetGraphicsRootSignature(root_.Get());
    commands->SetGraphicsRootConstantBufferView(kConstants, address);
    bindInputs(setGraphics);
    bindActivity();
    const auto viewport = [&](uint32_t width, uint32_t height) {
        const D3D12_VIEWPORT box{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        commands->RSSetViewports(1, &box);
        commands->RSSetScissorRects(1, &scissor);
    };
    const auto draw = [&] {
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(3u, 1u, 0u, 0u);
    };
    TransitionTarget(commands, state.marchColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionTarget(commands, state.marchDepth, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE marchTargets[2] = {state.marchColor.rtv, state.marchDepth.rtv};
    commands->OMSetRenderTargets(2, marchTargets, FALSE, nullptr);
    viewport(halfWidth, halfHeight);
    commands->SetPipelineState(march_.Get());
    draw();
    TransitionTarget(commands, state.marchColor, kRead);
    TransitionTarget(commands, state.marchDepth, kRead);

    // 場面へ重ねる。
    if (!bindSceneTarget())
        return false;
    commands->SetGraphicsRootSignature(root_.Get());
    commands->SetGraphicsRootConstantBufferView(kConstants, address);
    bindInputs(setGraphics);
    bindActivity();
    commands->SetGraphicsRootDescriptorTable(kFirstInput + kInputColor, state.marchColor.srv.gpu);
    commands->SetGraphicsRootDescriptorTable(kFirstInput + kInputDepth, state.marchDepth.srv.gpu);
    viewport(view.renderWidth, view.renderHeight);
    commands->SetPipelineState(composite_.Get());
    draw();

    // 烟の覆い（烟の平均距離と不透明度）を全解像度で書き、体積霧の合成へ渡す（場面の描画先は呼び出し側が戻す）。
    TransitionTarget(commands, state.coverage, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->OMSetRenderTargets(1, &state.coverage.rtv, FALSE, nullptr);
    commands->SetPipelineState(coverage_.Get());
    draw();
    TransitionTarget(commands, state.coverage, kRead);
    VOLUMETRIC::SetVolumetricFluidCoverage(state.coverage.srv.gpu, view.renderWidth, view.renderHeight);

    state.lastFrame = context.currentFrameRetireFenceValue;
    cameraVolumeView_ = {
        state.cameraVolume.srv.gpu,
        {kCameraVolumeNear, std::log(farthest / kCameraVolumeNear), static_cast<float>(kCameraVolumeSlices), 1.0f}};
    return true;
}

FluidFlowView FluidRenderer::PrepareFlow(const GFX::Context& context, std::span<FluidRenderDomain> domains) {
    const size_t count = (std::min)(domains.size(), static_cast<size_t>(kMaximumFluidFlowDomains));
    if (count == 0u || context.device == nullptr || context.cmdList == nullptr)
        return {};
    auto& upload = uploads_[context.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() && !upload.Init(context.device, 16u * 1024u, GFX::UPLOAD_TRAFFIC::Owner::Fluids))
        return {};
    upload.ResetForFrame(context.currentFrameRetireFenceValue);
    std::array<FlowDomainGpu, kMaximumFluidFlowDomains> table{};
    FluidFlowView view{};
    for (size_t index = 0u; index < count; ++index) {
        PackToGrid(domains[index], table[index].toGrid, table[index].cells);
        const MATH::Vec3& motion = domains[index].frameVelocity;
        Set4(table[index].motion, motion.x, motion.y, motion.z, 0.0f);
        TransitionVolume(context.cmdList, *domains[index].velocity, kRead);
        view.velocity[index] = domains[index].velocity->srv.gpu;
        if (domains[index].injection != 0u) {
            std::copy(std::begin(domains[index].injectionCells), std::end(domains[index].injectionCells),
                      table[index].injection);
            table[index].injection[3] = 1u;
            view.injection[index] = domains[index].injection;
        }
    }
    const auto allocation = upload.Allocate(sizeof(table));
    if (!allocation)
        return {};
    std::memcpy(allocation.cpuAddress, table.data(), sizeof(table));
    view.count = static_cast<uint32_t>(count);
    view.domains = allocation.gpuAddress;
    return view;
}

LIGHTING::CloudSolarVisibilityGpuData FluidRenderer::RenderSolarVisibility(const GFX::Context& context,
                                                                           std::span<const FluidRenderDomain> domains,
                                                                           const MATH::Vec3& toSun) {
    const size_t count = (std::min)(domains.size(), static_cast<size_t>(kMaximumRenderedFluidDomains));
    if (count == 0u || context.device == nullptr || context.cmdList == nullptr || !MATH::IsFinite(toSun) ||
        MATH::Length(toSun) < 0.5f || !EnsurePipelines(context.device, DXGI_FORMAT_UNKNOWN))
        return {};
    if (!solarAtlas_.resource) {
        if (!CreateFluidSliceTexture(context.device, DXGI_FORMAT_R32G32_FLOAT, kSolarResolution * 8u,
                                     ((kSolarSteps + 8u) / 8u) * kSolarResolution, solarAtlas_))
            return {};
        solarTexture_ = RegisterTextureResourceFromNative(solarAtlas_.resource.Get(), DXGI_FORMAT_R32G32_FLOAT);
        if (!IsTextureResourceValid(solarTexture_) ||
            GetTextureResourceSrvDescriptorIndex(solarTexture_) >= GFX::DESCRIPTOR::kUserSrvCount) {
            if (solarTexture_)
                ReleaseTextureResource(solarTexture_);
            solarTexture_ = {};
            RetireVolume(context, solarAtlas_);
            return {};
        }
    }
    auto& upload = uploads_[context.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() && !upload.Init(context.device, 16u * 1024u, GFX::UPLOAD_TRAFFIC::Owner::Fluids))
        return {};
    upload.ResetForFrame(context.currentFrameRetireFenceValue);

    // 太陽に垂直な面の格子：域の箱の角をこの面へ写した範囲を覆う正方形（1 列ぶんの余白、中心は列の幅に吸着）。
    const MATH::Vec3 direction = MATH::Normalize(toSun);
    const MATH::Vec3 right = MATH::Normalize(
        MATH::Cross(std::abs(direction.y) < 0.99f ? MATH::Vec3{0, 1, 0} : MATH::Vec3{0, 0, 1}, direction));
    const MATH::Vec3 up = MATH::Cross(direction, right);
    float low[2] = {1.0e30f, 1.0e30f}, high[2] = {-1.0e30f, -1.0e30f};
    for (size_t index = 0u; index < count; ++index) {
        const FluidRenderDomain& domain = domains[index];
        for (uint32_t corner = 0u; corner < 8u; ++corner) {
            MATH::Vec3 local{};
            for (int axis = 0; axis < 3; ++axis)
                (&local.x)[axis] = ((corner >> axis) & 1u ? 0.5f : -0.5f) *
                                   static_cast<float>(domain.layout.cells[axis]) * domain.layout.cellSize;
            const MATH::Vec3 world = domain.position + MATH::RotateVector(domain.rotation, local);
            const float planar[2] = {MATH::Dot(world, right), MATH::Dot(world, up)};
            for (int axis = 0; axis < 2; ++axis) {
                low[axis] = (std::min)(low[axis], planar[axis]);
                high[axis] = (std::max)(high[axis], planar[axis]);
            }
        }
    }
    const float span = (std::max)(high[0] - low[0], high[1] - low[1]);
    const float extent = 0.5f * span * static_cast<float>(kSolarResolution) / static_cast<float>(kSolarResolution - 2u);
    const float column = 2.0f * extent / static_cast<float>(kSolarResolution);
    const float center[2] = {std::round(0.5f * (low[0] + high[0]) / column) * column,
                             std::round(0.5f * (low[1] + high[1]) / column) * column};
    const MATH::Vec3 origin = right * center[0] + up * center[1];

    LIGHTING::CloudSolarVisibilityGpuData table{};
    table.originDescriptor = {origin.x, origin.y, origin.z,
                              static_cast<float>(GetTextureResourceSrvDescriptorIndex(solarTexture_) + 1u)};
    table.rightExtent = {right.x, right.y, right.z, extent};
    table.upResolution = {up.x, up.y, up.z, static_cast<float>(kSolarResolution)};
    table.directionSteps = {direction.x, direction.y, direction.z, static_cast<float>(kSolarSteps)};

    RenderConstants constants{};
    FluidRenderView view{};
    view.sunDirection = direction;
    view.preExposure = 1.0f;
    constants.ambient[3] = static_cast<float>(count);
    constants.solarTable = table;
    for (size_t index = 0u; index < count; ++index)
        constants.domains[index] = PackDomain(domains[index], view);
    const auto allocation = upload.Allocate(sizeof(constants));
    if (!allocation)
        return {};
    std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));

    ID3D12GraphicsCommandList* commands = context.cmdList;
    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorPost, "FluidSmoke.SolarVisibility");
    ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(root_.Get());
    commands->SetComputeRootConstantBufferView(kConstants, allocation.gpuAddress);
    // 根の入力はすべて束ねる（使わない枠は空の体積）。
    for (UINT input = 0u; input < kInputCount; ++input)
        commands->SetComputeRootDescriptorTable(kFirstInput + input, fallbackVolume_.srv.gpu);
    for (UINT slot = 0u; slot < kMaximumRenderedFluidDomains; ++slot)
        commands->SetComputeRootShaderResourceView(kActivity + slot, fallbackBuffer_->GetGPUVirtualAddress());
    // 密度と、外推に使う速度（步进と同じ ExtrapolatedRho で読む）。
    for (size_t index = 0u; index < count; ++index) {
        TransitionVolume(commands, *domains[index].density, kRead);
        TransitionVolume(commands, *domains[index].velocity, kRead);
        commands->SetComputeRootDescriptorTable(kFirstInput + kDensity + static_cast<UINT>(index),
                                                domains[index].density->srv.gpu);
        commands->SetComputeRootDescriptorTable(kFirstInput + kVelocity + static_cast<UINT>(index),
                                                domains[index].velocity->srv.gpu);
    }
    commands->SetComputeRootDescriptorTable(kOutput, fallbackVolume_.uav.gpu);
    TransitionVolume(commands, solarAtlas_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetComputeRootDescriptorTable(kSolarOutput, solarAtlas_.uav.gpu);
    commands->SetPipelineState(solar_.Get());
    commands->Dispatch((kSolarResolution + 7u) / 8u, (kSolarResolution + 7u) / 8u, 1u);
    TransitionVolume(commands, solarAtlas_, kRead);
    return table;
}

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL

namespace HIKARI::RENDER3D::FLUID {

MATH::Mat4 MakeCameraRelativeViewProj(const MATH::Mat4& view, const MATH::Mat4& projection, float clipJitterX,
                                      float clipJitterY) noexcept {
    MATH::Mat4 rotation = view;
    rotation.m[3][0] = 0.0f;
    rotation.m[3][1] = 0.0f;
    rotation.m[3][2] = 0.0f;
    return TEMPORAL::ApplyClipSpaceJitter(projection * rotation, clipJitterX, clipJitterY);
}

FluidRenderView MakeFluidRenderView(const SceneEnvironment& environment, uint64_t viewKey,
                                    const MATH::Mat4& relativeViewProj, const MATH::Vec3& cameraPosition) {
    FluidRenderView view{};
    view.viewKey = viewKey;
    view.invRelativeViewProj = MATH::Inverse(relativeViewProj);
    view.cameraPosition = cameraPosition;
    // 前方は近い面と遠い面の画面中心の差（透視と直交のどちらでも同じ式）。
    const MATH::Vec3 nearCenter = MATH::TransformPoint(view.invRelativeViewProj, {0.0f, 0.0f, 0.0f});
    const MATH::Vec3 farCenter = MATH::TransformPoint(view.invRelativeViewProj, {0.0f, 0.0f, 1.0f});
    view.cameraForward = MATH::Normalize(farCenter - nearCenter);
    const ATMOSPHERE::AtmosphereSolarState solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    view.sunDirection = solar.sunDirection;
    view.sunIlluminance = solar.sunColor * solar.sunRadiance;
    view.ambientRadiance = SKYRENDERER::EstimateSkyAmbientRadiance(environment);
    const MATH::Vec4& exposure = COLOR::GetActiveViewExposure();
    view.preExposure = exposure.x;
    view.sceneShadows = true;
    view.quality = GetRenderQualitySettings().fluidSmokeQuality;
    return view;
}

} // namespace HIKARI::RENDER3D::FLUID
