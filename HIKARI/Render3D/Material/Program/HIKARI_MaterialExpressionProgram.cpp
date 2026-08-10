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

const MaterialProgramInstruction* FindMaterialExpressionOutput(
    const MaterialProgramIR& program) noexcept {
    const MaterialProgramOpcode outputOpcode =
        program.domain == MaterialDomain::Particle
            ? MaterialProgramOpcode::ParticleOutput
            : MaterialProgramOpcode::PbrSurface;
    const auto found = std::find_if(
        program.instructions.begin(),
        program.instructions.end(),
        [outputOpcode](const MaterialProgramInstruction& instruction) {
            return instruction.opcode == outputOpcode;
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
    const MaterialProgramIR& program) {
    const MaterialProgramInstruction* output =
        FindMaterialExpressionOutput(program);
    if (output == nullptr) {
        return {};
    }
    constexpr std::array surfaceOutputs{
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
    constexpr std::array particleOutputs{
        std::pair{std::string_view{"Color"}, BaseColorOutput},
        std::pair{std::string_view{"Opacity"}, OpacityOutput},
        std::pair{std::string_view{"Emissive"}, EmissiveOutput},
        std::pair{std::string_view{"AlphaClipThreshold"}, AlphaClipOutput},
    };
    std::vector<OutputRoot> roots{};
    const auto append = [&roots, output](const auto& outputs) {
      for (const auto& [pin, mask] : outputs) {
        const uint32_t materialRegister = FindInputRegister(*output, pin);
        if (materialRegister == kInvalidMaterialProgramRegister) {
            continue;
        }
        roots.push_back({pin, mask, materialRegister});
      }
    };
    if (program.domain == MaterialDomain::Particle) {
        append(particleOutputs);
    } else {
        append(surfaceOutputs);
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
HIKARI_EXPRESSION_OPCODE(Atan2, Atan2);
HIKARI_EXPRESSION_OPCODE(Exp, Exp);
HIKARI_EXPRESSION_OPCODE(Log2, Log2);
HIKARI_EXPRESSION_OPCODE(Sign, Sign);
HIKARI_EXPRESSION_OPCODE(Round, Round);
HIKARI_EXPRESSION_OPCODE(Trunc, Trunc);
HIKARI_EXPRESSION_OPCODE(UvTransform, UvTransform);
HIKARI_EXPRESSION_OPCODE(ValueNoise, ValueNoise);
HIKARI_EXPRESSION_OPCODE(Voronoi, Voronoi);
HIKARI_EXPRESSION_OPCODE(Fresnel, Fresnel);
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

uint8_t AppendValue(
    std::vector<MATH::Vec4>& values,
    const MATH::Vec4& value,
    std::string& outError) {
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
    std::string& outError,
    std::span<const MaterialProgramParameterValueOverride> overrides) {
    outProgram = {};
    outError.clear();
    const ProducerTable producers = BuildProducerTable(program);
    const std::vector<OutputRoot> roots =
        CollectDynamicOutputs(program);
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
    std::unordered_map<std::string, uint8_t> externalValueIndices{};
    std::unordered_map<std::string, uint32_t> nodeCounts{};
    std::vector<std::pair<std::string, uint32_t>> nodeOrder{};
    instructions.reserve(required.size());
    for (size_t index = 0u; index < program.instructions.size(); ++index) {
        if (!required.contains(index)) {
            continue;
        }
        const MaterialProgramInstruction& instruction =
            program.instructions[index];
        const auto [nodeIt, nodeInserted] = nodeCounts.emplace(
            instruction.sourceNodeId, 1u);
        if (nodeInserted) {
            nodeOrder.emplace_back(instruction.sourceNodeId, 0u);
        } else {
            ++nodeIt->second;
        }
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
            const MaterialProgramParameterBinding& parameter =
                program.parameters[instruction.bindingIndex];
            const MaterialValueData* overrideValue =
                FindMaterialParameterOverride(
                    overrides, instruction.bindingIndex);
            const MaterialValueData& value = overrideValue != nullptr
                ? *overrideValue
                : parameter.defaultValue;
            if (!parameter.externalBindingId.empty()) {
                const auto existing = externalValueIndices.find(
                    parameter.externalBindingId);
                if (existing != externalValueIndices.end()) {
                    valueIndex = existing->second;
                } else {
                    valueIndex = AppendValue(
                        values, MaterialValueToVector(value), outError);
                    if (valueIndex != kInvalidRegister) {
                        externalValueIndices.emplace(
                            parameter.externalBindingId, valueIndex);
                    }
                }
            } else {
                valueIndex = AddValue(
                    values, MaterialValueToVector(value), outError);
            }
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
    outProgram.outputMask = outputMask;
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
    outProgram.instructionCount =
        static_cast<uint32_t>(instructions.size());
    outProgram.registerCount = nextRegister;
    outProgram.valueCount = static_cast<uint32_t>(values.size());
    outProgram.nodeInstructionCounts.reserve(nodeOrder.size());
    for (const auto& [nodeId, unused] : nodeOrder) {
        (void)unused;
        outProgram.nodeInstructionCounts.emplace_back(
            nodeId, nodeCounts[nodeId]);
    }
    outProgram.active = true;
    return true;
}

bool ResolveMaterialExpressionBudget(
    const MaterialProgramIR& program,
    MaterialExpressionBudget& outBudget,
    std::string& outError) {
    outBudget = {};
    outError.clear();
    MaterialExpressionRuntimeProgram expression{};
    if (!BuildMaterialExpressionRuntimeProgram(
            program, expression, outError)) {
        return false;
    }
    if (!expression.active) {
        return true;
    }
    outBudget.active = true;
    outBudget.instructionCount = expression.instructionCount;
    outBudget.registerCount = expression.registerCount;
    outBudget.valueCount = expression.valueCount;
    outBudget.parameterSlotCount =
        static_cast<uint32_t>(expression.parameters.size());
    outBudget.resourceCount =
        static_cast<uint32_t>(expression.resourceBindingIndices.size());
    outBudget.nodeInstructionCounts =
        std::move(expression.nodeInstructionCounts);
    return true;
}

bool MaterialProgramHasRuntimeExpression(
    const MaterialProgramIR& program) {
    return !CollectDynamicOutputs(program).empty();
}

} // namespace HIKARI::RENDER3D::MATERIAL
