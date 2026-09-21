#include "Gfx/Internal/HIKARI_ShaderCookDxil.h"

#include <algorithm>
#include <tuple>

#include <d3d12shader.h>
#include <wrl/client.h>

namespace HIKARI::GFX::INTERNAL {
    using Microsoft::WRL::ComPtr;
    std::string BlobToUtf8(
        IDxcUtils* utils,
        IDxcBlob* blob) {
        if (utils == nullptr || blob == nullptr) {
            return {};
        }
        ComPtr<IDxcBlobUtf8> utf8{};
        if (FAILED(utils->GetBlobAsUtf8(
                blob,
                utf8.GetAddressOf())) ||
            utf8 == nullptr || utf8->GetStringLength() == 0u) {
            return {};
        }
        return std::string(
            utf8->GetStringPointer(),
            utf8->GetStringLength());
    }
    namespace {

        using Microsoft::WRL::ComPtr;



        bool MapBindingKind(
            D3D_SHADER_INPUT_TYPE input,
            ShaderCookBindingKind& outKind) noexcept {
            switch (input) {
            case D3D_SIT_CBUFFER:
                outKind = ShaderCookBindingKind::ConstantBuffer;
                return true;
            case D3D_SIT_SAMPLER:
                outKind = ShaderCookBindingKind::Sampler;
                return true;
            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            case D3D_SIT_UAV_FEEDBACKTEXTURE:
                outKind = ShaderCookBindingKind::UnorderedAccess;
                return true;
            case D3D_SIT_TBUFFER:
            case D3D_SIT_TEXTURE:
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
            case D3D_SIT_RTACCELERATIONSTRUCTURE:
                outKind = ShaderCookBindingKind::ShaderResource;
                return true;
            default:
                return false;
            }
        }

    } // namespace

    bool ValidateShaderCookDxil(
        const DxcRuntimeSnapshot& runtime,
        IDxcBlob* shader,
        std::string& outError) {
        ComPtr<IDxcOperationResult> validation{};
        if (runtime.validator == nullptr || shader == nullptr ||
            FAILED(runtime.validator->Validate(
                shader,
                DxcValidatorFlags_Default,
                validation.GetAddressOf())) ||
            validation == nullptr) {
            outError = "DXIL validator invocation failed";
            return false;
        }
        HRESULT status = E_FAIL;
        if (FAILED(validation->GetStatus(&status)) || FAILED(status)) {
            ComPtr<IDxcBlobEncoding> errors{};
            validation->GetErrorBuffer(errors.GetAddressOf());
            outError = "DXIL validation failed: " +
                BlobToUtf8(runtime.utils.Get(), errors.Get());
            return false;
        }
        return true;
    }

    bool ExtractShaderCookBindings(
        const DxcRuntimeSnapshot& runtime,
        IDxcResult* result,
        ShaderStage stage,
        std::vector<ShaderCookBinding>& outBindings,
        std::string& outError) {
        ComPtr<IDxcBlob> reflectionBlob{};
        if (result == nullptr ||
            FAILED(result->GetOutput(
                DXC_OUT_REFLECTION,
                IID_PPV_ARGS(reflectionBlob.GetAddressOf()),
                nullptr)) ||
            reflectionBlob == nullptr ||
            reflectionBlob->GetBufferSize() == 0u) {
            outError = "DXC did not produce reflection output";
            return false;
        }
        const DxcBuffer reflectionBuffer{
            reflectionBlob->GetBufferPointer(),
            reflectionBlob->GetBufferSize(),
            DXC_CP_ACP
        };
        ComPtr<ID3D12ShaderReflection> reflection{};
        if (FAILED(runtime.utils->CreateReflection(
                &reflectionBuffer,
                IID_PPV_ARGS(reflection.GetAddressOf()))) ||
            reflection == nullptr) {
            outError = "DXC reflection interface creation failed";
            return false;
        }
        D3D12_SHADER_DESC description{};
        if (FAILED(reflection->GetDesc(&description)) ||
            description.BoundResources > kMaximumShaderCookBindings) {
            outError = "shader reflection resource count is invalid";
            return false;
        }
        std::vector<ShaderCookBinding> bindings{};
        bindings.reserve(description.BoundResources);
        for (UINT index = 0u;
            index < description.BoundResources;
            ++index) {
            D3D12_SHADER_INPUT_BIND_DESC binding{};
            ShaderCookBindingKind kind{};
            if (FAILED(reflection->GetResourceBindingDesc(
                    index,
                    &binding)) ||
                !MapBindingKind(binding.Type, kind) ||
                binding.BindCount == 0u) {
                outError = "shader reflection binding is unsupported";
                return false;
            }
            bindings.push_back({
                kind,
                binding.BindPoint,
                binding.Space,
                binding.BindCount,
                1u << static_cast<uint32_t>(stage)
            });
        }
        std::sort(
            bindings.begin(),
            bindings.end(),
            [](const ShaderCookBinding& lhs,
               const ShaderCookBinding& rhs) {
                return std::tie(
                    lhs.kind,
                    lhs.registerSpace,
                    lhs.registerIndex,
                    lhs.count,
                    lhs.visibilityMask) <
                    std::tie(
                        rhs.kind,
                        rhs.registerSpace,
                        rhs.registerIndex,
                        rhs.count,
                        rhs.visibilityMask);
            });
        outBindings = std::move(bindings);
        return true;
    }

} // namespace HIKARI::GFX::INTERNAL
