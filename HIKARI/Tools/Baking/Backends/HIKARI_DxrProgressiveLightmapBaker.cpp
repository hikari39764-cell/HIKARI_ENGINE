#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapGpuData.h"

namespace HIKARI::TOOLS::BAKING {

    namespace TRANSPORT =
        RENDER3D::LIGHTING::TRANSPORT;

    namespace {

        constexpr uint32_t kRootConstants = 0u;
        constexpr uint32_t kRootScene = 1u;
        constexpr uint32_t kRootTriangles = 2u;
        constexpr uint32_t kRootTexels = 3u;
        constexpr uint32_t kRootLights = 4u;
        constexpr uint32_t kRootOutput = 5u;

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
            triangle.diffuse = {
                source.diffuseReflectance.x,
                source.diffuseReflectance.y,
                source.diffuseReflectance.z,
                1.0f
            };
            triangle.emissive = {
                source.emissiveRadiance.x,
                source.emissiveRadiance.y,
                source.emissiveRadiance.z,
                0.0f
            };
            for (uint32_t corner = 0u;
                corner < 3u;
                ++corner) {
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

    void DxrProgressiveLightmapBaker::ResetTarget() {
        readbackBuffer_.Reset();
        outputInitializationBuffer_.Reset();
        outputBuffer_.Reset();
        texelBuffer_.Reset();
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
        lightBuffer_.Reset();
        triangleBuffer_.Reset();
        pipelineState_.Reset();
        rootSignature_.Reset();
        accelerationStructure_.Reset();
        environment_ = {};
        triangleCount_ = 0u;
        lightCount_ = 0u;
        scenePrepared_ = false;
    }

    bool DxrProgressiveLightmapBaker::CreatePipeline(
        ID3D12Device* device,
        std::string* outMessage) {

        D3D12_ROOT_PARAMETER parameters[6]{};
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

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters =
            static_cast<UINT>(std::size(parameters));
        rootDescription.pParameters = parameters;

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
                outMessage);
        readbackBuffer_->Unmap(0u, nullptr);
        return saved;
    }

} // namespace HIKARI::TOOLS::BAKING
