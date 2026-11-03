#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingLighting.h"
#include <algorithm>
#include <cstring>
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_FencedUploadBuffers.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::RAYTRACING {
struct RuntimeRayTracingLighting::State {
    GFX::Context context{};
    // 光源表の後ろに CDF。中身（光源の hash）が変わった時だけ書き、変わらない間は同じ buffer を読ませる。
    GFX::FencedUploadBuffers table{GFX::UPLOAD_TRAFFIC::Owner::RayTracingLights};
    uint64_t tableHash = 0;
    uint64_t cdfOffset = 0;
    RenderResourceView nullCube{};
    std::vector<LIGHTING::LocalLightGpuData> lights;
    std::vector<MATH::Vec2> cdf;
    std::vector<double> weights;
    std::vector<std::byte> staging;
    RuntimeRayLightingFrame published{};
};

bool RuntimeRayLightingFrame::Matches(const GFX::Context& graphics, const RuntimeRayTracingFrame& rayScene,
                                      RuntimeRayTracingMaterialVersion materials) const noexcept {
    return status == RuntimeRayLightingStatus::Ready && localLights && lightCdf && sky.ptr &&
           IsRuntimeRayTracingFrameCurrent(scene, graphics, materials) &&
           IsRuntimeRayTracingFrameCurrent(rayScene, graphics, materials) &&
           scene.sceneIdentity == rayScene.sceneIdentity && scene.sceneLayoutVersion == rayScene.sceneLayoutVersion &&
           scene.version == rayScene.version && scene.accelerationStructure == rayScene.accelerationStructure &&
           scene.hitRecords == rayScene.hitRecords && scene.hitRecordCount == rayScene.hitRecordCount &&
           scene.empty == rayScene.empty && data.scene[0] == scene.hitRecordCount &&
           data.scene[2] <= LIGHTING::kMaximumLocalLightCount && data.scene[3] == UINT(scene.empty);
}

RuntimeRayTracingLighting::RuntimeRayTracingLighting() : state_(std::make_unique<State>()) {}
RuntimeRayTracingLighting::~RuntimeRayTracingLighting() = default;

RuntimeRayLightingFrame RuntimeRayTracingLighting::Prepare(const GFX::Context& graphics,
                                                           const RuntimeRayTracingFrame& scene,
                                                           const MESHRENDERER::MeshMaterialGpuFrameView& materials,
                                                           const SceneEnvironment& environment,
                                                           const LIGHTING::SceneLightingSet& lighting, MATH::Vec4 time,
                                                           const LIGHTING::CloudSolarVisibilityGpuData& clouds) {
    RuntimeRayLightingFrame frame;
    if (!materials || graphics.frameIndex >= GFX::kFrameResourceCount ||
        !IsRuntimeRayTracingFrameCurrent(scene, graphics, {materials.dataVersion, materials.bindingVersion}))
        return frame;
    auto& state = *state_;
    if (state.context.device && (state.context.device != graphics.device || state.context.srvHeap != graphics.srvHeap ||
                                 state.context.deviceGeneration != graphics.deviceGeneration ||
                                 state.context.frameFence != graphics.frameFence))
        Reset(state.context);
    state.context = graphics;
    const auto& sky = environment.sky;
    const SkyMode skyMode = ResolveSkyRenderMode(sky.source);
    const auto& ibl = IBL::GetEnvironmentData();
    if (!ibl.hasPrefiltered) {
        frame.status = RuntimeRayLightingStatus::MissingEnvironment;
        return frame;
    }
    auto& data = frame.data;
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    data.sunDirectionIntensity = {solar.sunDirection.x, solar.sunDirection.y, solar.sunDirection.z, solar.sunRadiance};
    data.sunColorRange = {solar.sunColor.x, solar.sunColor.y, solar.sunColor.z, solar.sunAngularRadius};
    data.skyTintExposure = {sky.tint.x, sky.tint.y, sky.tint.z, ResolveSkyTextureRadianceScale(sky)};
    data.skyParams = {1.0f, skyMode == SkyMode::Atmosphere ? 0.0f : sky.yaw, 0.0f, std::max(0.0f, sky.lightIntensity)};
    data.time = time;
    data.clouds = clouds;
    state.lights.clear();
    state.cdf.clear();
    state.weights.clear();
    double total = 0;
    for (const auto& light : lighting.lights) {
        if (!LIGHTING::IsLocalLightUploadable(light))
            continue;
        if (state.lights.size() >= LIGHTING::kMaximumLocalLightCount) {
            ++frame.droppedLights;
            continue;
        }
        auto gpu = LIGHTING::BuildLocalLightGpuData(light);
        if (light.castsShadows)
            gpu.kind |= 1u << 16;
        state.lights.push_back(gpu);
        const double power =
            std::max(1e-5, double(std::max({light.color.x, light.color.y, light.color.z})) * light.intensity);
        state.weights.push_back(power);
        total += power;
    }
    double accumulated = 0;
    for (const double weight : state.weights) {
        const double probability = weight / total;
        accumulated += probability;
        state.cdf.push_back({float(accumulated), float(probability)});
    }
    if (!state.cdf.empty())
        state.cdf.back().x = 1.0f;
    data.scene = {scene.hitRecordCount, materials.materialCapacity, UINT(state.lights.size()), scene.empty ? 1u : 0u};
    frame.scene = scene;
    frame.contentHash = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, &data, sizeof(data));
    frame.contentHash =
        HASH::Fnv1aBytes(frame.contentHash, state.lights.data(), state.lights.size() * sizeof(state.lights[0]));
    const auto& previous = state.published;
    if (previous.Matches(graphics, scene, {materials.dataVersion, materials.bindingVersion}) &&
        previous.contentHash == frame.contentHash && previous.droppedLights == frame.droppedLights &&
        previous.sky.ptr == (ibl.hasPrefiltered ? ibl.prefilteredSrv.ptr : state.nullCube.gpu.ptr))
        return previous;
    frame.status = RuntimeRayLightingStatus::ResourceFailure;
    if (!state.nullCube.IsValid()) {
        state.nullCube = AllocateRenderResourceDescriptor();
        if (!state.nullCube.IsValid())
            return frame;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        graphics.device->CreateShaderResourceView(nullptr, &srv, state.nullCube.cpu);
    }
    frame.sky = ibl.hasPrefiltered ? ibl.prefilteredSrv : state.nullCube.gpu;
    if (state.lights.empty()) {
        state.lights.emplace_back();
        state.cdf.push_back({1, 1});
    }
    const size_t lightBytes = state.lights.size() * sizeof(state.lights[0]);
    const size_t cdfBytes = state.cdf.size() * sizeof(state.cdf[0]);
    uint64_t tableHash = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, state.lights.data(), lightBytes);
    tableHash = HASH::Fnv1aBytes(tableHash, state.cdf.data(), cdfBytes);
    if (!state.table.Current() || tableHash != state.tableHash) {
        state.cdfOffset = GFX::AlignD3D12ConstantBufferByteSize(static_cast<UINT>(lightBytes));
        state.staging.resize(state.cdfOffset + cdfBytes);
        std::memcpy(state.staging.data(), state.lights.data(), lightBytes);
        std::memcpy(state.staging.data() + state.cdfOffset, state.cdf.data(), cdfBytes);
        if (!state.table.Write(graphics, state.staging.data(), state.staging.size(), "RuntimeRayLighting.Table"))
            return frame;
        state.tableHash = tableHash;
    }
    state.table.MarkRead(graphics);
    frame.localLights = state.table.Current()->GetGPUVirtualAddress();
    frame.lightCdf = frame.localLights + state.cdfOffset;
    frame.status = RuntimeRayLightingStatus::Ready;
    state.published = frame;
    return frame;
}

void RuntimeRayTracingLighting::Reset(const GFX::Context& graphics) {
    auto& state = *state_;
    state.table.Reset(graphics, "RuntimeRayLighting.Table");
    state.tableHash = 0;
    RetireRenderResourceDescriptor(state.nullCube, graphics, "RuntimeRayLighting.NullCube");
    state.published = {};
    state.context = {};
    state.lights.clear();
    state.cdf.clear();
    state.weights.clear();
}
} // namespace HIKARI::RENDER3D::RAYTRACING
