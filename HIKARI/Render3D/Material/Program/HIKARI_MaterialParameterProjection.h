#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::RENDER3D::MATERIAL {

    struct MaterialProgramParameterValueOverride {
        uint32_t bindingIndex = kInvalidMaterialProgramRegister;
        MaterialValueData value{};
    };

    struct MaterialParameterProjectionBinding {
        uint32_t bindingIndex = kInvalidMaterialProgramRegister;
        std::string externalBindingId{};
        MaterialValueType type = MaterialValueType::Float1;
    };

    // Immutable, renderer-domain recipe. It intentionally contains no
    // Runtime/Binding type; composition adapters supply typed overrides.
    struct MaterialParameterProjectionRecipe {
        MaterialProgramIR program{};
        std::vector<MaterialParameterProjectionBinding> bindings{};
    };

    // Immutable cross-layer value snapshot. Runtime adapters translate their
    // own typed stores into this Material-owned contract; Render3D never reads
    // Runtime/Binding containers directly. sourceInstanceId prevents ABA when
    // independent stores reuse the same local revision sequence.
    struct MaterialExternalValueSnapshot {
        uint64_t sourceInstanceId = 0u;
        uint64_t revision = 0u;
        std::unordered_map<std::string, MaterialValueData> values{};
    };

    inline const MaterialValueData* FindMaterialParameterOverride(
        std::span<const MaterialProgramParameterValueOverride> overrides,
        uint32_t bindingIndex) noexcept {

        for (const MaterialProgramParameterValueOverride& overrideValue :
             overrides) {
            if (overrideValue.bindingIndex == bindingIndex) {
                return &overrideValue.value;
            }
        }
        return nullptr;
    }

    inline MATH::Vec4 MaterialValueToVector(
        const MaterialValueData& value) noexcept {

        if (value.type == MaterialValueType::Bool) {
            return {
                value.boolean ? 1.0f : 0.0f,
                0.0f,
                0.0f,
                0.0f
            };
        }
        return value.numeric;
    }

    std::shared_ptr<const MaterialParameterProjectionRecipe>
        BuildMaterialParameterProjectionRecipe(
            const MaterialProgramIR& program);

    // Rebuilds only the CPU-authored GPU parameter block. Texture/resource
    // resolution is deliberately outside this path so rapidly changing game
    // variables never recreate or rebind material resources.
    bool BuildMaterialProgramParameterBlock(
        const MaterialProgramIR& program,
        std::span<const MaterialProgramParameterValueOverride> overrides,
        std::vector<MATH::Vec4>& outParameters,
        std::string& outError);

    bool ApplyMaterialExternalValues(
        HIKARI::Material& material,
        const MaterialExternalValueSnapshot& snapshot,
        std::string& outError);

} // namespace HIKARI::RENDER3D::MATERIAL
