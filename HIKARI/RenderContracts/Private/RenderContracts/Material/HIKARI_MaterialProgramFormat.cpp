#include "RenderContracts/Material/HIKARI_MaterialProgramFormat.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "RenderContracts/Material/HIKARI_MaterialShellFields.h"

namespace HIKARI {

bool IsMaterialNumericType(MaterialValueType type) noexcept {
    return type == MaterialValueType::Float1 || type == MaterialValueType::Float2 ||
           type == MaterialValueType::Float3 || type == MaterialValueType::Float4;
}

uint32_t GetMaterialValueComponentCount(MaterialValueType type) noexcept {
    switch (type) {
    case MaterialValueType::Bool:
    case MaterialValueType::Float1: return 1u;
    case MaterialValueType::Float2: return 2u;
    case MaterialValueType::Float3: return 3u;
    case MaterialValueType::Float4: return 4u;
    default: return 0u;
    }
}

bool ResolveMaterialProgramShellSettings(const MaterialProgramIR& program, MaterialShellSettings& outSettings,
                                         std::string& outError,
                                         const std::function<const MaterialValueData*(uint32_t)>& parameterOverride) {
    outSettings = {};
    outError.clear();
    const auto producer = [&](uint32_t output) -> const MaterialProgramInstruction* {
        if (output == kInvalidMaterialProgramRegister)
            return nullptr;
        for (const auto& instruction : program.instructions)
            if (std::find(instruction.outputRegisters.begin(), instruction.outputRegisters.end(), output) !=
                instruction.outputRegisters.end())
                return &instruction;
        return nullptr;
    };
    const MaterialProgramInstruction* shell = nullptr;
    for (const auto& instruction : program.instructions) {
        if (instruction.opcode != MaterialProgramOpcode::PbrSurface)
            continue;
        const uint32_t shellRegister = FindMaterialProgramInputRegister(instruction, "Shell");
        if (shellRegister == kInvalidMaterialProgramRegister)
            continue;
        shell = producer(shellRegister);
        if (shell == nullptr || shell->opcode != MaterialProgramOpcode::Shell) {
            outError = "PBR Shell requires a Shell configuration node";
            return false;
        }
        break;
    }
    if (shell == nullptr)
        return true;
    if (program.domain != MaterialDomain::Surface || program.surface.alphaMode != MaterialAlphaMode::Mask) {
        outError = "Shell geometry requires the Surface domain and Mask alpha mode";
        return false;
    }
    bool valid = true;
    kMaterialShellFields([&](const char* pin, auto member, JSONFIELDS::Range range) {
        if (!valid)
            return;
        const uint32_t input = FindMaterialProgramInputRegister(*shell, pin);
        if (input == kInvalidMaterialProgramRegister)
            return;
        const auto* valueInstruction = producer(input);
        const MaterialValueData* value = nullptr;
        if (valueInstruction && valueInstruction->opcode == MaterialProgramOpcode::Constant) {
            value = &valueInstruction->immediate;
        } else if (valueInstruction && valueInstruction->opcode == MaterialProgramOpcode::Parameter &&
                   valueInstruction->bindingIndex < program.parameters.size()) {
            value = parameterOverride ? parameterOverride(valueInstruction->bindingIndex) : nullptr;
            if (value == nullptr)
                value = &program.parameters[valueInstruction->bindingIndex].defaultValue;
        }
        if (value == nullptr || value->type != MaterialValueType::Float1 || !MATH::IsFinite(value->numeric.x) ||
            value->numeric.x < range.min || value->numeric.x > range.max) {
            valid = false;
            outError = std::string("Shell ") + pin + " requires a finite in-range scalar constant or parameter";
            return;
        }
        using Field = std::remove_reference_t<decltype(outSettings.*member)>;
        if constexpr (std::is_integral_v<Field>) {
            if (value->numeric.x != std::floor(value->numeric.x)) {
                valid = false;
                outError = "Shell MaxLayers must be 0, 2, 4, or 8";
                return;
            }
        }
        outSettings.*member = static_cast<Field>(value->numeric.x);
    });
    if (valid && !IsValidMaterialShellSettings(outSettings)) {
        valid = false;
        outError = "Shell requires 0/2/4/8 layers and ordered projected-pixel LOD thresholds";
    }
    return valid;
}

uint32_t FindMaterialProgramInputRegister(const MaterialProgramInstruction& instruction,
                                          std::string_view pin) noexcept {
    const auto found = std::find(instruction.inputPins.begin(), instruction.inputPins.end(), pin);
    const size_t index = static_cast<size_t>(found - instruction.inputPins.begin());
    return found != instruction.inputPins.end() && index < instruction.inputRegisters.size()
               ? instruction.inputRegisters[index]
               : kInvalidMaterialProgramRegister;
}

} // namespace HIKARI
