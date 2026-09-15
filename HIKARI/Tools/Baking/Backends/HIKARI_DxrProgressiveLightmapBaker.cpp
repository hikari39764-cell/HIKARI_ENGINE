#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <d3dx12.h>

#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapGpuData.h"

namespace HIKARI::TOOLS::BAKING {

    namespace TRANSPORT =
        RENDER3D::LIGHTING::TRANSPORT;

    namespace {

        using enum INTERNAL::DxrLightmapRootParameter;

        void SetMessage(
            std::string* outMessage,
            std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        bool CreateBuffer(
            ID3D12Device* device,
            UINT64 size,
            D3D12_HEAP_TYPE heapType,
            D3D12_RESOURCE_FLAGS flags,
            D3D12_RESOURCE_STATES state,
            ID3D12Resource** outResource) {

            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heapType;
            heap.CreationNodeMask = 1u;
            heap.VisibleNodeMask = 1u;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = (std::max)(UINT64{ 1u }, size);
            desc.Height = 1u;
            desc.DepthOrArraySize = 1u;
            desc.MipLevels = 1u;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1u;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = flags;
            return SUCCEEDED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                state,
                nullptr,
                IID_PPV_ARGS(outResource)));
        }

        template<class T>
        bool CreateUploadBuffer(
            ID3D12Device* device,
            const std::vector<T>& values,
            Microsoft::WRL::ComPtr<ID3D12Resource>& outResource) {

            if (values.empty() ||
                !CreateBuffer(
                    device,
                    sizeof(T) * values.size(),
                    D3D12_HEAP_TYPE_UPLOAD,
                    D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    outResource.GetAddressOf())) {
                return false;
            }
            void* mapped = nullptr;
            if (FAILED(outResource->Map(0u, nullptr, &mapped))) {
                outResource.Reset();
                return false;
            }
            std::memcpy(
                mapped,
                values.data(),
                sizeof(T) * values.size());
            outResource->Unmap(0u, nullptr);
            return true;
        }

        MATH::Vec4 ToPosition4(const MATH::Vec3& value) {
            return { value.x, value.y, value.z, 1.0f };
        }

        MATH::Vec4 ToDirection4(const MATH::Vec3& value) {
            return { value.x, value.y, value.z, 0.0f };
        }

    } // namespace

    TRANSPORT::LightingTransportBackendCapabilities
        DxrProgressiveLightmapBaker::GetCapabilities(
            const GFX::Context& graphics) const noexcept {

        TRANSPORT::LightingTransportBackendCapabilities
            capabilities{};
        capabilities.backend =
            TRANSPORT::LightingTransportBackendKind::
                DxrPathTracing;
#if !defined(HIKARI_WITH_EDITOR)
        (void)graphics;
        return capabilities;
#else
        const GFX::RAYTRACING::DxrCapabilities dxr =
            GFX::RAYTRACING::QueryDxrCapabilities(
                graphics.device);
        capabilities.available = dxr.SupportsInlineRayQuery();
        capabilities.supportsDiffuseIrradiance =
            capabilities.available;
        capabilities.supportsSurfaceLightmaps =
            capabilities.available;
        capabilities.supportsProgressiveSampling =
            capabilities.available;
        capabilities.supportsMultipleBounces =
            capabilities.available;
        capabilities.maximumBounceCount =
            capabilities.available ? 4u : 0u;
        return capabilities;
#endif
    }

    bool DxrProgressiveLightmapBaker::PrepareScene(
        const GFX::Context& graphics,
        const TRANSPORT::LightingTransportScene& scene,
        std::string* outMessage) {

        Reset();
        if (!scene.environment.error.empty()) {
            SetMessage(outMessage, scene.environment.error);
            return false;
        }
        if ((scene.environment.skyMode == SkyMode::Cubemap || scene.environment.skyMode == SkyMode::Texture2D) &&
            (!scene.environment.skyTexture.enabled || scene.environment.skyTexture.dimension !=
                (scene.environment.skyMode == SkyMode::Cubemap ? RENDER3D::TextureResourceDimension::TextureCube :
                    RENDER3D::TextureResourceDimension::Texture2D))) {
            SetMessage(outMessage, "DXR sky texture is missing or has the wrong dimension.");
            return false;
        }
        if (!GetCapabilities(graphics).available ||
            scene.triangles.empty()) {
            SetMessage(
                outMessage,
                "DXR lightmap backend is unavailable or the transport scene is empty.");
            return false;
        }
        if (!CreatePipeline(graphics.device, outMessage)) {
            return false;
        }
        for (size_t i = 0; i < scene.materials.size(); ++i) {
            if (!scene.materials[i].supportsStaticEvaluation) {
                SetMessage(outMessage, "DXR lightmap material " + std::to_string(i) +
                    " uses time, view, screen or derivative nodes; static surface evaluation is required.");
                return false;
            }
        }
        std::string materialError;
        auto textureSources = RENDER3D::MATERIAL::CollectMaterialSnapshotTextures(scene.materials);
        if (scene.environment.skyTexture.enabled) textureSources.push_back(scene.environment.skyTexture);
        if (!textureSnapshot_.Initialize(graphics.device, textureSources, materialError) ||
            !materialSnapshot_.Initialize(graphics.device, scene.materials, textureSnapshot_, materialError)) {
            SetMessage(outMessage, "DXR lightmap: " + materialError);
            Reset();
            return false;
        }
        skyDescriptorIndex_ = textureSnapshot_.GetDescriptorIndex(scene.environment.skyTexture);

        std::vector<GFX::RAYTRACING::DxrPosition>
            positions{};
        std::vector<uint32_t> indices{};
        std::vector<GpuTriangle> triangles{};
        positions.reserve(scene.triangles.size() * 3u);
        indices.reserve(scene.triangles.size() * 3u);
        triangles.reserve(scene.triangles.size());
        for (const TRANSPORT::LightingTransportTriangle&
             source : scene.triangles) {
            GpuTriangle triangle{};
            triangle.position0 = ToPosition4(source.position[0]);
            triangle.position1 = ToPosition4(source.position[1]);
            triangle.position2 = ToPosition4(source.position[2]);
            triangle.normal0 = ToDirection4(source.normal[0]);
            triangle.normal1 = ToDirection4(source.normal[1]);
            triangle.normal2 = ToDirection4(source.normal[2]);
            if (source.materialIndex >= scene.materials.size()) {
                SetMessage(outMessage, "DXR transport triangle has no material snapshot.");
                Reset();
                return false;
            }
            triangle.materialIndex = source.materialIndex;
            for (uint32_t corner = 0u;
                corner < 3u;
                ++corner) {
                triangle.tangent[corner] = source.tangent[corner];
                triangle.materialUv[corner] = {source.materialUv0[corner].x, source.materialUv0[corner].y,
                    source.materialUv1[corner].x, source.materialUv1[corner].y};
                positions.push_back({
                    source.position[corner].x,
                    source.position[corner].y,
                    source.position[corner].z
                });
                indices.push_back(
                    static_cast<uint32_t>(
                        indices.size()));
            }
            triangles.push_back(triangle);
        }

        std::string accelerationMessage{};
        if (!accelerationStructure_.Build(
                graphics,
                positions,
                indices,
                &accelerationMessage) ||
            !CreateUploadBuffer(
                graphics.device,
                triangles,
                triangleBuffer_)) {
            SetMessage(
                outMessage,
                accelerationMessage.empty()
                    ? "Failed to prepare DXR transport geometry."
                    : accelerationMessage);
            Reset();
            return false;
        }

        std::vector<GpuLight> lights{};
        lights.reserve(scene.lights.size());
        for (const TRANSPORT::LightingTransportLight& source :
             scene.lights) {
            GpuLight light{};
            light.positionRange = {
                source.position.x,
                source.position.y,
                source.position.z,
                source.range
            };
            light.directionOuterCone = {
                source.direction.x,
                source.direction.y,
                source.direction.z,
                source.outerConeCos
            };
            light.radianceInnerCone = {
                source.radiance.x,
                source.radiance.y,
                source.radiance.z,
                source.innerConeCos
            };
            light.type = source.type;
            light.angularFalloff = source.angularFalloff;
            light.distanceFalloff = static_cast<uint32_t>(source.distanceFalloff);
            light.sourceRadius = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(source.sourceRadius);
            lights.push_back(light);
        }
        if (lights.empty()) {
            lights.push_back({});
        }
        if (!CreateUploadBuffer(
                graphics.device,
                lights,
                lightBuffer_)) {
            SetMessage(
                outMessage,
                "Failed to upload lighting transport lights.");
            Reset();
            return false;
        }

        if (!CreateUploadBuffer(graphics.device,
            std::vector{scene.environment.atmosphere}, atmosphereBuffer_)) {
            SetMessage(outMessage, "Failed to upload atmosphere transport parameters.");
            Reset();
            return false;
        }
        environment_ = scene.environment;
        triangleCount_ =
            static_cast<uint32_t>(scene.triangles.size());
        lightCount_ =
            static_cast<uint32_t>(scene.lights.size());
        scenePrepared_ = true;
        SetMessage(
            outMessage,
            "DXR path-tracing scene prepared.");
        return true;
    }

    DxrProgressiveLightmapBaker::~DxrProgressiveLightmapBaker() { Reset(); }

    void DxrProgressiveLightmapBaker::ResetTarget() {
        GFX::RetireD3D12ObjectForCurrentFrame(readbackBuffer_, "Lightmap.Readback");
        GFX::RetireD3D12ObjectForCurrentFrame(outputInitializationBuffer_, "Lightmap.Initialization");
        GFX::RetireD3D12ObjectForCurrentFrame(outputBuffer_, "Lightmap.Output");
        GFX::RetireD3D12ObjectForCurrentFrame(texelBuffer_, "Lightmap.Texels");
        validTexels_.clear();
        texelOwnerIndices_.clear();
        atlasAllocations_.clear();
        status_ = {};
        width_ = 0u;
        height_ = 0u;
        bounceCount_ = 0u;
        readbackFenceValue_ = 0u;
    }

    void DxrProgressiveLightmapBaker::Reset() {
        ResetTarget();
        materialSnapshot_.Reset();
        textureSnapshot_.Reset();
        GFX::RetireD3D12ObjectForCurrentFrame(lightBuffer_, "Lightmap.Lights");
        GFX::RetireD3D12ObjectForCurrentFrame(atmosphereBuffer_, "Lightmap.Atmosphere");
        GFX::RetireD3D12ObjectForCurrentFrame(triangleBuffer_, "Lightmap.Triangles");
        GFX::RetireD3D12ObjectForCurrentFrame(pipelineState_, "Lightmap.Pipeline");
        GFX::RetireD3D12ObjectForCurrentFrame(rootSignature_, "Lightmap.RootSignature");
        accelerationStructure_.Reset();
        environment_ = {};
        triangleCount_ = 0u;
        lightCount_ = 0u;
        skyDescriptorIndex_ = UINT32_MAX;
        scenePrepared_ = false;
    }

    bool DxrProgressiveLightmapBaker::CreatePipeline(
        ID3D12Device* device,
        std::string* outMessage) {

        D3D12_ROOT_PARAMETER parameters[kRootParameterCount]{};
        parameters[kRootConstants].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[kRootConstants].Constants.ShaderRegister = 0u;
        parameters[kRootConstants].Constants.Num32BitValues =
            sizeof(DispatchConstants) / sizeof(uint32_t);

        for (uint32_t i = kRootScene;
            i <= kRootLights;
            ++i) {
            parameters[i].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[i].Descriptor.ShaderRegister =
                i - kRootScene;
        }
        parameters[kRootOutput].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[kRootOutput].Descriptor.ShaderRegister = 0u;
        parameters[kRootMaterials].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[kRootMaterials].Descriptor.ShaderRegister = 16;
        parameters[kRootParameters].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[kRootParameters].Descriptor = {0, 6};
        parameters[kRootResources].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[kRootResources].Descriptor = {1, 6};
        const CD3DX12_DESCRIPTOR_RANGE textures(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            GFX::DESCRIPTOR::kUserSrvCount, 20);
        parameters[kRootTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kRootTextures].DescriptorTable = {1, &textures};
        const CD3DX12_DESCRIPTOR_RANGE environmentTexture(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
        parameters[kRootEnvironment].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kRootEnvironment].DescriptorTable = {1, &environmentTexture};
        parameters[kRootAtmosphere].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[kRootAtmosphere].Descriptor.ShaderRegister = 1;
        const CD3DX12_STATIC_SAMPLER_DESC samplers[]{
            {0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP},
            {1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP}};

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters =
            static_cast<UINT>(std::size(parameters));
        rootDescription.pParameters = parameters;
        rootDescription.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
        rootDescription.pStaticSamplers = samplers;

        Microsoft::WRL::ComPtr<ID3DBlob> signature{};
        Microsoft::WRL::ComPtr<ID3DBlob> errors{};
        if (FAILED(D3D12SerializeRootSignature(
                &rootDescription,
                D3D_ROOT_SIGNATURE_VERSION_1,
                signature.GetAddressOf(),
                errors.GetAddressOf())) ||
            FAILED(device->CreateRootSignature(
                0u,
                signature->GetBufferPointer(),
                signature->GetBufferSize(),
                IID_PPV_ARGS(rootSignature_.GetAddressOf())))) {
            SetMessage(
                outMessage,
                "Failed to create DXR lightmap root signature.");
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> shader{};
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Baking/HIKARI_DxrLightmapCS.hlsl",
                "main",
                L"cs_6_5",
                shader.GetAddressOf())) {
            SetMessage(
                outMessage,
                "Failed to compile DXR lightmap shader.");
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = rootSignature_.Get();
        pipeline.CS = {
            shader->GetBufferPointer(),
            shader->GetBufferSize()
        };
        if (FAILED(device->CreateComputePipelineState(
                &pipeline,
                IID_PPV_ARGS(pipelineState_.GetAddressOf())))) {
            SetMessage(
                outMessage,
                "Failed to create DXR lightmap pipeline state.");
            return false;
        }
        return true;
    }

    bool DxrProgressiveLightmapBaker::CreateTargetResources(
        ID3D12Device* device,
        const void* texelData,
        size_t texelByteSize,
        std::string* outMessage) {

        if (texelData == nullptr ||
            texelByteSize == 0u ||
            !CreateBuffer(
                device,
                texelByteSize,
                D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                texelBuffer_.GetAddressOf())) {
            SetMessage(
                outMessage,
                "Failed to upload lightmap surface texels.");
            return false;
        }
        void* texelMapped = nullptr;
        if (FAILED(texelBuffer_->Map(
                0u,
                nullptr,
                &texelMapped)) ||
            texelMapped == nullptr) {
            texelBuffer_.Reset();
            SetMessage(
                outMessage,
                "Failed to map lightmap surface texels.");
            return false;
        }
        std::memcpy(
            texelMapped,
            texelData,
            texelByteSize);
        texelBuffer_->Unmap(0u, nullptr);

        const UINT64 byteSize =
            static_cast<UINT64>(width_) *
            height_ *
            sizeof(MATH::Vec4);
        if (!CreateBuffer(
                device,
                byteSize,
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_DEST,
                outputBuffer_.GetAddressOf()) ||
            !CreateBuffer(
                device,
                byteSize,
                D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                outputInitializationBuffer_.GetAddressOf()) ||
            !CreateBuffer(
                device,
                byteSize,
                D3D12_HEAP_TYPE_READBACK,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_COPY_DEST,
                readbackBuffer_.GetAddressOf())) {
            SetMessage(
                outMessage,
                "Failed to allocate lightmap accumulation resources.");
            return false;
        }
        void* mapped = nullptr;
        if (FAILED(outputInitializationBuffer_->Map(
                0u,
                nullptr,
                &mapped))) {
            SetMessage(
                outMessage,
                "Failed to initialize lightmap accumulation buffer.");
            return false;
        }
        std::memset(
            mapped,
            0,
            static_cast<size_t>(byteSize));
        outputInitializationBuffer_->Unmap(0u, nullptr);
        return true;
    }

    bool DxrProgressiveLightmapBaker::SaveReadbackDds(
        const std::filesystem::path& outputPath,
        std::string* outMessage) {

        void* mapped = nullptr;
        if (FAILED(readbackBuffer_->Map(
                0u,
                nullptr,
                &mapped)) ||
            mapped == nullptr) {
            SetMessage(
                outMessage,
                "Failed to map DXR lightmap readback.");
            return false;
        }

        const MATH::Vec4* accumulation =
            static_cast<const MATH::Vec4*>(mapped);
        void* surfaceMapped = nullptr;
        if (FAILED(texelBuffer_->Map(0u,nullptr,&surfaceMapped)) || surfaceMapped == nullptr) {
            readbackBuffer_->Unmap(0u,nullptr);
            SetMessage(outMessage,"Failed to map lightmap filtering guides.");
            return false;
        }
        const bool saved =
            INTERNAL::WriteProgressiveLightmapDds(
                outputPath,
                width_,
                height_,
                status_.requestedSamples,
                accumulation,
                validTexels_,
                texelOwnerIndices_,
                atlasAllocations_,
                outMessage,
                {static_cast<const INTERNAL::DxrLightmapSurfaceTexel*>(surfaceMapped),
                    static_cast<size_t>(width_)*height_});
        const D3D12_RANGE noWrites{0,0};
        texelBuffer_->Unmap(0u,&noWrites);
        readbackBuffer_->Unmap(0u, nullptr);
        return saved;
    }

} // namespace HIKARI::TOOLS::BAKING
