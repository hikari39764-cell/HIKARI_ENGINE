#pragma once

#include <cstddef>

#include "Gfx/HIKARI_D3DBlobCompat.h"

#include <d3d12.h>

namespace HIKARI::GFX {

    enum class ShaderStage {
        Vertex,
        Pixel,
        Compute,
        Amplification,
        Mesh,
    };

    const wchar_t* ShaderModel6Profile(ShaderStage stage);
    const wchar_t* UpgradeToShaderModel6Profile(const char* legacyProfile);

    bool SupportsShaderModel6(ID3D12Device* device);
    bool SupportsShaderModel(ID3D12Device* device, D3D_SHADER_MODEL minimumModel);

    bool CompileShaderFileSm6(
        const wchar_t* path,
        const char* entry,
        const wchar_t* profile,
        ID3DBlob** outBlob);

    bool CompileShaderFileSm6(
        const wchar_t* path,
        const char* entry,
        ShaderStage stage,
        ID3DBlob** outBlob);

    bool CompileShaderSourceSm6(
        const char* source,
        size_t sourceBytes,
        const wchar_t* virtualName,
        const char* entry,
        const wchar_t* profile,
        ID3DBlob** outBlob);

    bool CompileShaderSourceSm6(
        const char* source,
        size_t sourceBytes,
        const wchar_t* virtualName,
        const char* entry,
        ShaderStage stage,
        ID3DBlob** outBlob);

} // namespace HIKARI::GFX
