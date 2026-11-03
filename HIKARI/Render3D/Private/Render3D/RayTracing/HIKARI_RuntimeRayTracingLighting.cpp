#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingLighting.h"
#include <algorithm>
#include <cstring>
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::RAYTRACING {
namespace {
constexpr size_t kUploadPageBytes = 256 * 1024;
static_assert(LIGHTING::kLightingProviderViewSlotCount *
                  (LIGHTING::kMaximumLocalLightCount * (sizeof(LIGHTING::LocalLightGpuData) + sizeof(MATH::Vec2)) +
                   512) <=
              kUploadPageBytes);
} // namespace
struct RuntimeRayTracingLighting::State {
    GFX::Context context{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads;
    RenderResourceView nullCube{};
    std::vector<LIGHTING::LocalLightGpuData> lights;
    std::vector<MATH::Vec2> cdf;
    std::vector<double> weights;
    RuntimeRayLightingFrame published{};
    uint64_t workFence = 0;
    uint32_t publications = 0;
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
    if (state.workFence != graphics.currentFrameRetireFenceValue) {
        state.workFence = graphics.currentFrameRetireFenceValue;
        state.publications = 0;
    }
    const auto& previous = state.published;
    if (previous.Matches(graphics, scene, {materials.dataVersion, materials.bindingVersion}) &&
        previous.contentHash == frame.contentHash && previous.droppedLights == frame.droppedLights &&
        previous.sky.ptr == (ibl.hasPrefiltered ? ibl.prefilteredSrv.ptr : state.nullCube.gpu.ptr))
        return previous;
    frame.status = RuntimeRayLightingStatus::ResourceFailure;
    // 2 view 分の immutable publication。無制限に upload page を増やさない。
    if (state.publications >= LIGHTING::kLightingProviderViewSlotCount)
        return frame;
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
    auto& upload = state.uploads[graphics.frameIndex];
    if (!upload.IsInitialized() &&
        !upload.Init(graphics.device, kUploadPageBytes, GFX::UPLOAD_TRAFFIC::Owner::RayTracingLights))
        return frame;
    upload.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const auto lights = upload.Allocate(state.lights.size() * sizeof(state.lights[0]));
    const auto cdf = upload.Allocate(state.cdf.size() * sizeof(state.cdf[0]));
    if (!lights || !cdf)
        return frame;
    std::memcpy(lights.cpuAddress, state.lights.data(), state.lights.size() * sizeof(state.lights[0]));
    std::memcpy(cdf.cpuAddress, state.cdf.data(), state.cdf.size() * sizeof(state.cdf[0]));
    frame.localLights = lights.gpuAddress;
    frame.lightCdf = cdf.gpuAddress;
    frame.status = RuntimeRayLightingStatus::Ready;
    ++state.publications;
    state.published = frame;
    return frame;
}

void RuntimeRayTracingLighting::Reset(const GFX::Context& graphics) {
    auto& state = *state_;
    for (auto& upload : state.uploads)
        upload.Finalize(&graphics);
    RetireRenderResourceDescriptor(state.nullCube, graphics, "RuntimeRayLighting.NullCube");
    state.published = {};
    state.workFence = 0;
    state.publications = 0;
    state.context = {};
    state.lights.clear();
    state.cdf.clear();
    state.weights.clear();
}
} // namespace HIKARI::RENDER3D::RAYTRACING
