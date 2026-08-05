#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI::RENDER3D::MATERIAL {
namespace {

constexpr uint8_t kInvalidRegister = 0xffu;

static_assert(
    kGpuMaterialExpressionHeaderBase ==
    kGpuMaterialProgramResponseSlotCount);
static_assert(
    kGpuMaterialExpressionValueBase ==
    kGpuMaterialExpressionHeaderBase +
        kGpuMaterialExpressionHeaderSlotCount);
static_assert(
    kGpuMaterialExpressionInstructionBase ==
    kGpuMaterialExpressionValueBase +
        kGpuMaterialExpressionValueSlotCount);

enum MaterialExpressionOutputBit : uint32_t {
    BaseColorOutput = 1u << 0u,
    OpacityOutput = 1u << 1u,
    NormalOutput = 1u << 2u,
    MetallicOutput = 1u << 3u,
    RoughnessOutput = 1u << 4u,
    OcclusionOutput = 1u << 5u,
    EmissiveOutput = 1u << 6u,
    SpecularOutput = 1u << 7u,
    SpecularColorOutput = 1u << 8u,
    AlphaClipOutput = 1u << 9u,
};

struct ProducerTable {
    std::unordered_map<uint32_t, size_t> byRegister{};
};

ProducerTable BuildProducerTable(const MaterialProgramIR& program) {
    ProducerTable result{};
    for (size_t index = 0u; index < program.instructions.size(); ++index) {
        for (const uint32_t output :
             program.instructions[index].outputRegisters) {
            result.byRegister.emplace(output, index);
        }
    }
    return result;
}

const MaterialProgramInstruction* FindProducer(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister) noexcept {
    const auto found = producers.byRegister.find(materialRegister);
    return found == producers.byRegister.end()
        ? nullptr
        : &program.instructions[found->second];
}

bool IsLegacyStaticExpression(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister,
    std::unordered_set<uint32_t>& visiting) {
    if (materialRegister == kInvalidMaterialProgramRegister ||
        !visiting.emplace(materialRegister).second) {
        return false;
    }
    const MaterialProgramInstruction* instruction =
        FindProducer(program, producers, materialRegister);
    if (instruction == nullptr) {
        visiting.erase(materialRegister);
        return false;
    }
    const auto allInputsStatic = [&]() {
        return !instruction->inputRegisters.empty() &&
            std::all_of(
                instruction->inputRegisters.begin(),
                instruction->inputRegisters.end(),
                [&](uint32_t input) {
                    return IsLegacyStaticExpression(
                        program, producers, input, visiting);
                });
    };
    bool result = false;
    switch (instruction->opcode) {
    case MaterialProgramOpcode::Constant:
    case MaterialProgramOpcode::Parameter:
        result = true;
        break;
    case MaterialProgramOpcode::Add:
    case MaterialProgramOpcode::Multiply:
    case MaterialProgramOpcode::Lerp:
    case MaterialProgramOpcode::Clamp:
    case MaterialProgramOpcode::Saturate:
    case MaterialProgramOpcode::OneMinus:
    case MaterialProgramOpcode::Power:
    case MaterialProgramOpcode::Dot:
        result = allInputsStatic();
        break;
    default:
        break;
    }
    visiting.erase(materialRegister);
    return result;
}

bool IsLegacyStaticExpression(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister) {
    std::unordered_set<uint32_t> visiting{};
    return IsLegacyStaticExpression(
        program, producers, materialRegister, visiting);
}

bool IsLegacyTextureExpression(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister,
    bool allowNormalUnpack) {
    const MaterialProgramInstruction* instruction =
        FindProducer(program, producers, materialRegister);
    if (instruction == nullptr) {
        return false;
    }
    if (instruction->opcode == MaterialProgramOpcode::TextureSample) {
        return instruction->inputRegisters.empty() ||
            instruction->inputRegisters.front() ==
                kInvalidMaterialProgramRegister;
    }
    if (allowNormalUnpack &&
        instruction->opcode == MaterialProgramOpcode::NormalUnpack &&
        !instruction->inputRegisters.empty()) {
        return IsLegacyTextureExpression(
            program, producers, instruction->inputRegisters.front(), false);
    }
    return false;
}

bool IsLegacyFactorTextureExpression(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister) {
    const MaterialProgramInstruction* instruction =
        FindProducer(program, producers, materialRegister);
    if (instruction == nullptr ||
        instruction->opcode != MaterialProgramOpcode::Multiply ||
        instruction->inputRegisters.size() != 2u) {
        return false;
    }
    const uint32_t a = instruction->inputRegisters[0];
    const uint32_t b = instruction->inputRegisters[1];
    return (IsLegacyStaticExpression(program, producers, a) &&
            IsLegacyTextureExpression(program, producers, b, false)) ||
        (IsLegacyStaticExpression(program, producers, b) &&
         IsLegacyTextureExpression(program, producers, a, false));
}

bool IsLegacyProjectedExpression(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister,
    std::string_view targetPin) {
    if (IsLegacyStaticExpression(program, producers, materialRegister)) {
        return true;
    }
    if (targetPin == "Normal") {
        return IsLegacyTextureExpression(
            program, producers, materialRegister, true);
    }
    return IsLegacyTextureExpression(
            program, producers, materialRegister, false) ||
        IsLegacyFactorTextureExpression(
            program, producers, materialRegister);
}

const MaterialProgramInstruction* FindPbrSurface(
    const MaterialProgramIR& program) noexcept {
    const auto found = std::find_if(
        program.instructions.begin(),
        program.instructions.end(),
        [](const MaterialProgramInstruction& instruction) {
            return instruction.opcode == MaterialProgramOpcode::PbrSurface;
        });
    return found == program.instructions.end() ? nullptr : &*found;
}

uint32_t FindInputRegister(
    const MaterialProgramInstruction& instruction,
    std::string_view pin) noexcept {
    const auto found = std::find(
        instruction.inputPins.begin(), instruction.inputPins.end(), pin);
    if (found == instruction.inputPins.end()) {
        return kInvalidMaterialProgramRegister;
    }
    return instruction.inputRegisters[
        static_cast<size_t>(found - instruction.inputPins.begin())];
}

struct OutputRoot {
    std::string_view pin{};
    uint32_t mask = 0u;
    uint32_t materialRegister = kInvalidMaterialProgramRegister;
};

std::vector<OutputRoot> CollectDynamicOutputs(
    const MaterialProgramIR& program,
    const ProducerTable& producers) {
    const MaterialProgramInstruction* surface = FindPbrSurface(program);
    if (surface == nullptr) {
        return {};
    }
    constexpr std::array outputs{
        std::pair{std::string_view{"BaseColor"}, BaseColorOutput},
        std::pair{std::string_view{"Opacity"}, OpacityOutput},
        std::pair{std::string_view{"Normal"}, NormalOutput},
        std::pair{std::string_view{"Metallic"}, MetallicOutput},
        std::pair{std::string_view{"Roughness"}, RoughnessOutput},
        std::pair{std::string_view{"Occlusion"}, OcclusionOutput},
        std::pair{std::string_view{"Emissive"}, EmissiveOutput},
        std::pair{std::string_view{"Specular"}, SpecularOutput},
        std::pair{std::string_view{"SpecularColor"}, SpecularColorOutput},
        std::pair{std::string_view{"AlphaClipThreshold"}, AlphaClipOutput},
    };
    std::vector<OutputRoot> roots{};
    for (const auto& [pin, mask] : outputs) {
        const uint32_t materialRegister = FindInputRegister(*surface, pin);
        if (materialRegister == kInvalidMaterialProgramRegister) {
            continue;
        }
        const bool legacyProjected = pin != "Opacity" &&
            pin != "AlphaClipThreshold" &&
            IsLegacyProjectedExpression(
                program, producers, materialRegister, pin);
        if (!legacyProjected) {
            roots.push_back({pin, mask, materialRegister});
        }
    }
    return roots;
}

bool CollectRequiredInstructions(
    const MaterialProgramIR& program,
    const ProducerTable& producers,
    uint32_t materialRegister,
    std::unordered_set<size_t>& required,
    std::string& outError) {
    const auto found = producers.byRegister.find(materialRegister);
    if (found == producers.byRegister.end()) {
        outError = "material expression reads a register without a producer";
        return false;
    }
    if (!required.emplace(found->second).second) {
        return true;
    }
    const MaterialProgramInstruction& instruction =
        program.instructions[found->second];
    for (const uint32_t input : instruction.inputRegisters) {
        if (input != kInvalidMaterialProgramRegister &&
            !CollectRequiredInstructions(
                program, producers, input, required, outError)) {
            return false;
        }
    }
    return true;
}

std::optional<MaterialExpressionRuntimeOpcode> ToRuntimeOpcode(
    MaterialProgramOpcode opcode) noexcept {
#define HIKARI_EXPRESSION_OPCODE(Node, Runtime) \
    case MaterialProgramOpcode::Node: \
        return MaterialExpressionRuntimeOpcode::Runtime
    switch (opcode) {
    HIKARI_EXPRESSION_OPCODE(Constant, Constant);
    HIKARI_EXPRESSION_OPCODE(Parameter, Parameter);
    HIKARI_EXPRESSION_OPCODE(TextureSample, TextureSample);
    HIKARI_EXPRESSION_OPCODE(Add, Add);
    HIKARI_EXPRESSION_OPCODE(Subtract, Subtract);
    HIKARI_EXPRESSION_OPCODE(Multiply, Multiply);
    HIKARI_EXPRESSION_OPCODE(Divide, Divide);
    HIKARI_EXPRESSION_OPCODE(Lerp, Lerp);
    HIKARI_EXPRESSION_OPCODE(Minimum, Minimum);
    HIKARI_EXPRESSION_OPCODE(Maximum, Maximum);
    HIKARI_EXPRESSION_OPCODE(Clamp, Clamp);
    HIKARI_EXPRESSION_OPCODE(Saturate, Saturate);
    HIKARI_EXPRESSION_OPCODE(OneMinus, OneMinus);
    HIKARI_EXPRESSION_OPCODE(Power, Power);
    HIKARI_EXPRESSION_OPCODE(Absolute, Absolute);
    HIKARI_EXPRESSION_OPCODE(Negate, Negate);
    HIKARI_EXPRESSION_OPCODE(Floor, Floor);
    HIKARI_EXPRESSION_OPCODE(Ceil, Ceil);
    HIKARI_EXPRESSION_OPCODE(Fraction, Fraction);
    HIKARI_EXPRESSION_OPCODE(Modulo, Modulo);
    HIKARI_EXPRESSION_OPCODE(SquareRoot, SquareRoot);
    HIKARI_EXPRESSION_OPCODE(Sine, Sine);
    HIKARI_EXPRESSION_OPCODE(Cosine, Cosine);
    HIKARI_EXPRESSION_OPCODE(Step, Step);
    HIKARI_EXPRESSION_OPCODE(SmoothStep, SmoothStep);
    HIKARI_EXPRESSION_OPCODE(Remap, Remap);
    HIKARI_EXPRESSION_OPCODE(Dot, Dot);
    HIKARI_EXPRESSION_OPCODE(Cross, Cross);
    HIKARI_EXPRESSION_OPCODE(Length, Length);
    HIKARI_EXPRESSION_OPCODE(Normalize, Normalize);
    HIKARI_EXPRESSION_OPCODE(Distance, Distance);
    HIKARI_EXPRESSION_OPCODE(Reflect, Reflect);
    HIKARI_EXPRESSION_OPCODE(MakeFloat2, MakeFloat2);
    HIKARI_EXPRESSION_OPCODE(MakeFloat3, MakeFloat3);
    HIKARI_EXPRESSION_OPCODE(MakeFloat4, MakeFloat4);
    HIKARI_EXPRESSION_OPCODE(Split, Split);
    HIKARI_EXPRESSION_OPCODE(Select, Select);
    HIKARI_EXPRESSION_OPCODE(NormalUnpack, NormalUnpack);
    HIKARI_EXPRESSION_OPCODE(TextureCoordinate, TextureCoordinate);
    HIKARI_EXPRESSION_OPCODE(Time, Time);
    HIKARI_EXPRESSION_OPCODE(WorldPosition, WorldPosition);
    HIKARI_EXPRESSION_OPCODE(WorldNormal, WorldNormal);
    HIKARI_EXPRESSION_OPCODE(WorldTangent, WorldTangent);
    HIKARI_EXPRESSION_OPCODE(ViewDirection, ViewDirection);
    HIKARI_EXPRESSION_OPCODE(CameraPosition, CameraPosition);
    HIKARI_EXPRESSION_OPCODE(ScreenPosition, ScreenPosition);
    HIKARI_EXPRESSION_OPCODE(DerivativeX, DerivativeX);
    HIKARI_EXPRESSION_OPCODE(DerivativeY, DerivativeY);
    HIKARI_EXPRESSION_OPCODE(FilterWidth, FilterWidth);
    default:
        return std::nullopt;
    }
#undef HIKARI_EXPRESSION_OPCODE
}

uint32_t PackBytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept {
    return static_cast<uint32_t>(a) |
        (static_cast<uint32_t>(b) << 8u) |
        (static_cast<uint32_t>(c) << 16u) |
        (static_cast<uint32_t>(d) << 24u);
}

float AsFloat(uint32_t value) noexcept {
    return std::bit_cast<float>(value);
}

uint8_t ComponentCount(MaterialValueType type) noexcept {
    return static_cast<uint8_t>((std::clamp)(
        GetMaterialValueComponentCount(type), 1u, 4u));
}

uint8_t AddValue(
    std::vector<MATH::Vec4>& values,
    const MATH::Vec4& value,
    std::string& outError) {
    for (size_t index = 0u; index < values.size(); ++index) {
        if (std::memcmp(&values[index], &value, sizeof(value)) == 0) {
            return static_cast<uint8_t>(index);
        }
    }
    if (values.size() >= kGpuMaterialExpressionValueSlotCount) {
        outError = "material expression exceeds the 48-value GPU budget";
        return kInvalidRegister;
    }
    values.push_back(value);
    return static_cast<uint8_t>(values.size() - 1u);
}

uint8_t CompactRegister(
    const std::unordered_map<uint32_t, uint8_t>& registers,
    uint32_t materialRegister) noexcept {
    if (materialRegister == kInvalidMaterialProgramRegister) {
        return kInvalidRegister;
    }
    const auto found = registers.find(materialRegister);
    return found == registers.end() ? kInvalidRegister : found->second;
}

} // namespace

bool BuildMaterialExpressionRuntimeProgram(
    const MaterialProgramIR& program,
    MaterialExpressionRuntimeProgram& outProgram,
    std::string& outError) {
    outProgram = {};
    outError.clear();
    const ProducerTable producers = BuildProducerTable(program);
    const std::vector<OutputRoot> roots =
        CollectDynamicOutputs(program, producers);
    if (roots.empty()) {
        return true;
    }

    std::unordered_set<size_t> required{};
    for (const OutputRoot& root : roots) {
        if (!CollectRequiredInstructions(
                program, producers, root.materialRegister,
                required, outError)) {
            return false;
        }
    }
    if (required.size() > kGpuMaterialExpressionInstructionCount) {
        outError = "material expression exceeds the 96-instruction GPU budget";
        return false;
    }

    std::unordered_map<uint32_t, uint8_t> registers{};
    uint32_t nextRegister = 0u;
    for (size_t index = 0u; index < program.instructions.size(); ++index) {
        if (!required.contains(index)) {
            continue;
        }
        for (const uint32_t output :
             program.instructions[index].outputRegisters) {
            if (nextRegister >= kGpuMaterialExpressionRegisterCount) {
                outError = "material expression exceeds the 128-register GPU budget";
                return false;
            }
            registers.emplace(output, static_cast<uint8_t>(nextRegister++));
        }
    }

    std::vector<MATH::Vec4> values{};
    std::vector<MATH::Vec4> instructions{};
    std::unordered_set<uint32_t> resourceBindings{};
    instructions.reserve(required.size());
    for (size_t index = 0u; index < program.instructions.size(); ++index) {
        if (!required.contains(index)) {
            continue;
        }
        const MaterialProgramInstruction& instruction =
            program.instructions[index];
        const auto runtimeOpcode = ToRuntimeOpcode(instruction.opcode);
        if (!runtimeOpcode.has_value() ||
            instruction.outputRegisters.empty()) {
            outError = "material expression contains an opcode without a pixel-runtime implementation";
            return false;
        }
        std::array<uint8_t, 5u> inputs{};
        inputs.fill(kInvalidRegister);
        for (size_t inputIndex = 0u;
            inputIndex < instruction.inputRegisters.size() &&
            inputIndex < inputs.size(); ++inputIndex) {
            inputs[inputIndex] = CompactRegister(
                registers, instruction.inputRegisters[inputIndex]);
        }
        uint8_t binding = kInvalidRegister;
        uint8_t valueIndex = kInvalidRegister;
        uint8_t auxiliary = 0u;
        float scalarData = 0.0f;
        if (instruction.opcode == MaterialProgramOpcode::Constant) {
            valueIndex = AddValue(
                values, instruction.immediate.numeric, outError);
        } else if (instruction.opcode == MaterialProgramOpcode::Parameter) {
            if (instruction.bindingIndex >= program.parameters.size()) {
                outError = "material expression parameter binding is out of range";
                return false;
            }
            valueIndex = AddValue(
                values,
                program.parameters[instruction.bindingIndex].defaultValue.numeric,
                outError);
        } else if (instruction.opcode == MaterialProgramOpcode::TextureSample) {
            if (instruction.bindingIndex >= program.resources.size() ||
                instruction.bindingIndex +
                    kGpuMaterialExpressionResourceBase >=
                    kGpuMaterialProgramResourceSlotCount) {
                outError = "material expression exceeds the generic texture resource budget";
                return false;
            }
            const MaterialProgramResourceBinding& resource =
                program.resources[instruction.bindingIndex];
            binding = static_cast<uint8_t>(
                kGpuMaterialExpressionResourceBase +
                instruction.bindingIndex);
            valueIndex = AddValue(
                values,
                {resource.uvScale.x, resource.uvScale.y,
                 resource.uvOffset.x, resource.uvOffset.y},
                outError);
            auxiliary = static_cast<uint8_t>(
                (std::min)(resource.texCoord, 1u));
            scalarData = resource.uvRotation;
            resourceBindings.emplace(instruction.bindingIndex);
        }
        if (valueIndex == kInvalidRegister && !outError.empty()) {
            return false;
        }
        const uint8_t output = CompactRegister(
            registers, instruction.outputRegisters.front());
        const uint32_t header = PackBytes(
            static_cast<uint8_t>(*runtimeOpcode),
            instruction.opcode == MaterialProgramOpcode::Dot
                ? 3u
                : ComponentCount(instruction.valueType),
            output,
            static_cast<uint8_t>(instruction.outputRegisters.size()));
        instructions.push_back({
            AsFloat(header),
            AsFloat(PackBytes(inputs[0], inputs[1], inputs[2], inputs[3])),
            AsFloat(PackBytes(inputs[4], binding, valueIndex, auxiliary)),
            scalarData,
        });
    }

    uint32_t outputMask = 0u;
    std::array<uint8_t, 10u> outputRegisters{};
    outputRegisters.fill(kInvalidRegister);
    for (const OutputRoot& root : roots) {
        outputMask |= root.mask;
        const uint32_t bit = std::countr_zero(root.mask);
        outputRegisters[bit] = CompactRegister(
            registers, root.materialRegister);
    }

    outProgram.parameters.assign(
        kGpuMaterialExpressionInstructionBase,
        MATH::Vec4{0.0f, 0.0f, 0.0f, 0.0f});
    outProgram.parameters[kGpuMaterialExpressionHeaderBase + 0u] = {
        AsFloat(kGpuMaterialExpressionMagic),
        AsFloat(kGpuMaterialExpressionVersion),
        AsFloat(static_cast<uint32_t>(instructions.size())),
        AsFloat(nextRegister),
    };
    outProgram.parameters[kGpuMaterialExpressionHeaderBase + 1u] = {
        AsFloat(outputMask),
        AsFloat(PackBytes(outputRegisters[0], outputRegisters[1],
            outputRegisters[2], outputRegisters[3])),
        AsFloat(PackBytes(outputRegisters[4], outputRegisters[5],
            outputRegisters[6], outputRegisters[7])),
        AsFloat(PackBytes(outputRegisters[8], outputRegisters[9],
            kInvalidRegister, kInvalidRegister)),
    };
    outProgram.parameters[kGpuMaterialExpressionHeaderBase + 2u] = {
        AsFloat(static_cast<uint32_t>(values.size())),
        AsFloat(kGpuMaterialExpressionValueBase),
        AsFloat(kGpuMaterialExpressionInstructionBase),
        0.0f,
    };
    for (size_t index = 0u; index < values.size(); ++index) {
        outProgram.parameters[kGpuMaterialExpressionValueBase + index] =
            values[index];
    }
    outProgram.parameters.insert(
        outProgram.parameters.end(), instructions.begin(), instructions.end());
    if (outProgram.parameters.size() >
        kGpuMaterialProgramParameterSlotCount) {
        outError = "material expression exceeds the GPU parameter slot budget";
        return false;
    }
    outProgram.resourceBindingIndices.assign(
        resourceBindings.begin(), resourceBindings.end());
    std::sort(
        outProgram.resourceBindingIndices.begin(),
        outProgram.resourceBindingIndices.end());
    outProgram.active = true;
    return true;
}

bool MaterialProgramHasRuntimeExpression(
    const MaterialProgramIR& program) {
    const ProducerTable producers = BuildProducerTable(program);
    return !CollectDynamicOutputs(program, producers).empty();
}

} // namespace HIKARI::RENDER3D::MATERIAL
