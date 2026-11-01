#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <vector>
#include <d3dx12.h>
#include "Gfx/HIKARI_ShaderCookCompiler.h"
#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/Compiler/HIKARI_MaterialProgramSerialization.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgram.h"
#include "RenderContracts/Model/Policies/HIKARI_ModelMaterialPolicy.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Internal/HIKARI_GpuContractDevice.h"
#include "Internal/HIKARI_ContractAssert.h"

namespace {
using Microsoft::WRL::ComPtr;
using Pixel = std::array<float, 4>;
using namespace HIKARI::RENDER3D::MATERIAL;
constexpr UINT kSize = 16;
constexpr UINT kTextureCount = HIKARI::GFX::DESCRIPTOR::kUserSrvCount;

uint32_t Pack(uint32_t a, uint32_t b = 255, uint32_t c = 255, uint32_t d = 255) {
    return a | (b << 8u) | (c << 16u) | (d << 24u);
}

std::array<Pixel, kGpuMaterialProgramParameterSlotCount> MakeProgram() {
    std::array<Pixel, kGpuMaterialProgramParameterSlotCount> values{};
    values[kGpuMaterialExpressionHeaderBase] = std::bit_cast<Pixel>(
        std::array<uint32_t, 4>{kGpuMaterialExpressionMagic, kGpuMaterialExpressionVersion, 8, 10});
    values[kGpuMaterialExpressionHeaderBase + 1] =
        std::bit_cast<Pixel>(std::array<uint32_t, 4>{0x3ff, Pack(8, 2, 4, 5), Pack(3, 5, 7, 9), Pack(8, 6)});
    values[kGpuMaterialExpressionHeaderBase + 2] = std::bit_cast<Pixel>(
        std::array<uint32_t, 4>{3, kGpuMaterialExpressionValueBase, kGpuMaterialExpressionInstructionBase, 0});
    values[kGpuMaterialExpressionValueBase] = {0.6f, 0, 0.8f, 0};
    values[kGpuMaterialExpressionValueBase + 1] = {0.75f, 0, 0, 0};
    values[kGpuMaterialExpressionValueBase + 2] = {0.5f, 0, 0, 0};
    size_t instruction = kGpuMaterialExpressionInstructionBase;
    const auto append = [&](MaterialExpressionRuntimeOpcode opcode, uint32_t components, uint32_t destination,
                            uint32_t count = 1, uint32_t source = 255, uint32_t valueIndex = 0) {
        values[instruction++] = std::bit_cast<Pixel>(
            std::array<uint32_t, 4>{Pack(static_cast<uint32_t>(opcode), components, destination, count), Pack(source),
                                    Pack(255, 0, valueIndex, 0), 0});
    };
    append(MaterialExpressionRuntimeOpcode::TextureCoordinate, 2, 0, 2);
    append(MaterialExpressionRuntimeOpcode::Split, 1, 2, 2, 0);
    append(MaterialExpressionRuntimeOpcode::Constant, 3, 4);
    append(MaterialExpressionRuntimeOpcode::Constant, 1, 5, 1, 255, 1);
    append(MaterialExpressionRuntimeOpcode::Constant, 1, 6, 1, 255, 2);
    append(MaterialExpressionRuntimeOpcode::WorldPosition, 3, 7);
    append(MaterialExpressionRuntimeOpcode::ScreenPosition, 4, 8);
    append(MaterialExpressionRuntimeOpcode::Time, 1, 9);
    return values;
}

struct MaterialCase {
    const char* name;
    std::array<uint32_t, 4> constants{9, 0, kGpuMaterialProgramParameterSlotCount, 0};
    decltype(MakeProgram()) program = MakeProgram();
    float fresnelExpected = -1;
    int expectedTarget = -1;
    Pixel expectedPixel{};
};

MaterialCase MakeFresnelCase(float noV, float f0, float power, bool legacy = false, unsigned invalidNormal = 0) {
    using namespace HIKARI;
    auto source = MakeStandardPbrMaterialAssetData(PbrMaterialAssetData{});
    const auto surface = std::find_if(source.graph.nodes.begin(), source.graph.nodes.end(),
                                      [](const auto& node) { return node.kind == MaterialNodeKind::PbrSurface; });
    assert(surface != source.graph.nodes.end());
    const auto surfaceId = surface->id;
    MaterialGraphNodeData fresnel;
    fresnel.id = "contract.fresnel";
    fresnel.kind = MaterialNodeKind::Fresnel;
    fresnel.valueType = fresnel.value.type = MaterialValueType::Float1;
    source.graph.nodes.push_back(fresnel);
    const auto constant = [&](const char* pin, MaterialValueType type, const MATH::Vec4& value) {
        MaterialGraphNodeData node;
        node.id = std::string("fresnel.") + pin;
        node.kind = MaterialNodeKind::Constant;
        node.valueType = node.value.type = type;
        node.value.numeric = value;
        source.graph.nodes.push_back(node);
        source.graph.edges.push_back({node.id + ".edge", node.id, "Value", fresnel.id, pin});
    };
    if (!legacy) {
        constant("Normal", MaterialValueType::Float3, {0, 0, invalidNormal ? 0.0f : 2.0f, 0});
        constant("ViewDirection", MaterialValueType::Float3, {std::sqrt(1 - noV * noV), 0, noV, 0});
        constant("F0", MaterialValueType::Float1, {f0, 0, 0, 0});
    }
    constant("Power", MaterialValueType::Float1, {power, 0, 0, 0});
    std::erase_if(source.graph.edges,
                  [&](const auto& edge) { return edge.targetNodeId == surfaceId && edge.targetPin == "Metallic"; });
    source.graph.edges.push_back({"fresnel.output", fresnel.id, "Result", surfaceId, "Metallic"});
    auto compiled = CompileMaterialAsset(source);
    for (const auto& diagnostic : compiled.diagnostics)
        std::printf("Fresnel: %s\n", diagnostic.message.c_str());
    assert(compiled.success && !compiled.usedFallback);
    auto& program = compiled.program;
    const auto instruction =
        std::find_if(program.instructions.begin(), program.instructions.end(),
                     [](const auto& value) { return value.opcode == MaterialProgramOpcode::Fresnel; });
    assert(instruction != program.instructions.end());
    if (invalidNormal == 2) {
        const auto normal = std::find_if(program.instructions.begin(), program.instructions.end(),
                                         [](const auto& value) { return value.sourceNodeId == "fresnel.Normal"; });
        assert(normal != program.instructions.end());
        normal->immediate.numeric.x = std::numeric_limits<float>::quiet_NaN();
    }
    const auto powerRegister = FindMaterialProgramInputRegister(*instruction, "Power");
    assert(powerRegister != kInvalidMaterialProgramRegister);
    if (legacy) {
        instruction->inputPins = {"Power", "Width", "Color", "Intensity", "LightAlignment"};
        instruction->inputRegisters = {powerRegister, kInvalidMaterialProgramRegister, kInvalidMaterialProgramRegister,
                                       kInvalidMaterialProgramRegister, kInvalidMaterialProgramRegister};
    }
    MaterialExpressionRuntimeProgram expression;
    std::string error;
    assert(BuildMaterialExpressionRuntimeProgram(program, expression, error) && expression.active);
    const auto expectedParameters = expression.parameters;
    // pin 配列の並びが変わっても名前による意味は変化しない。
    std::reverse(instruction->inputPins.begin(), instruction->inputPins.end());
    std::reverse(instruction->inputRegisters.begin(), instruction->inputRegisters.end());
    assert(BuildMaterialExpressionRuntimeProgram(program, expression, error));
    assert(expectedParameters.size() == expression.parameters.size());
    assert(std::memcmp(expectedParameters.data(), expression.parameters.data(),
                       expectedParameters.size() * sizeof(MATH::Vec4)) == 0);
    instruction->inputPins.push_back("Width");
    instruction->inputRegisters.push_back(powerRegister);
    assert(!BuildMaterialExpressionRuntimeProgram(program, expression, error) &&
           error.find("Width") != std::string::npos);
    MaterialCase test{"compiled Fresnel inputs"};
    test.constants[0] = legacy ? 12 : 11;
    test.program = {};
    assert(expectedParameters.size() <= test.program.size());
    std::memcpy(test.program.data(), expectedParameters.data(), expectedParameters.size() * sizeof(MATH::Vec4));
    const float boundedF0 = std::clamp(f0, 0.0f, 1.0f);
    test.fresnelExpected = power <= 0 || invalidNormal ? 1 : boundedF0 + (1 - boundedF0) * std::pow(1 - noV, power);
    return test;
}

MaterialCase MakeNormalCase(bool uvTransform, const HIKARI::MATH::Vec4& base, const HIKARI::MATH::Vec4& detail,
                            float baseStrength, float detailStrength, float rotation, HIKARI::MATH::Vec2 scale,
                            Pixel expected) {
    using namespace HIKARI;
    auto source = MakeStandardPbrMaterialAssetData();
    const auto surface = std::find_if(source.graph.nodes.begin(), source.graph.nodes.end(), [](const auto& node) {
                             return node.kind == MaterialNodeKind::PbrSurface;
                         })->id;
    MaterialGraphNodeData operation;
    operation.id = "contract.normal";
    operation.kind = uvTransform ? MaterialNodeKind::NormalUvTransform : MaterialNodeKind::NormalBlend;
    operation.valueType = operation.value.type = MaterialValueType::Float3;
    source.graph.nodes.push_back(operation);
    const auto constant = [&](const char* pin, MaterialValueType type, MATH::Vec4 value) {
        MaterialGraphNodeData node;
        node.id = std::string("normal.") + pin;
        node.kind = MaterialNodeKind::Constant;
        node.valueType = node.value.type = type;
        node.value.numeric = value;
        source.graph.nodes.push_back(node);
        source.graph.edges.push_back({node.id + ".edge", node.id, "Value", operation.id, pin});
    };
    if (uvTransform) {
        constant("Normal", MaterialValueType::Float3, base);
        constant("Rotation", MaterialValueType::Float1, {rotation, 0, 0, 0});
        constant("Scale", MaterialValueType::Float2, {scale.x, scale.y, 0, 0});
    } else {
        constant("Base", MaterialValueType::Float3, base);
        constant("Detail", MaterialValueType::Float3, detail);
        constant("BaseStrength", MaterialValueType::Float1, {baseStrength, 0, 0, 0});
        constant("DetailStrength", MaterialValueType::Float1, {detailStrength, 0, 0, 0});
    }
    std::erase_if(source.graph.edges,
                  [&](const auto& edge) { return edge.targetNodeId == surface && edge.targetPin == "Normal"; });
    source.graph.edges.push_back({"normal.output", operation.id, "Normal", surface, "Normal"});
    const auto compiled = CompileMaterialAsset(source);
    assert(compiled.success);
    MaterialExpressionRuntimeProgram expression;
    std::string error;
    assert(BuildMaterialExpressionRuntimeProgram(compiled.program, expression, error) && expression.active);
    MaterialCase test{uvTransform ? "normal follows rotated/mirrored UV" : "reoriented detail normal"};
    test.constants[0] = 13;
    test.program = {};
    assert(expression.parameters.size() <= test.program.size());
    std::memcpy(test.program.data(), expression.parameters.data(), expression.parameters.size() * sizeof(MATH::Vec4));
    test.expectedTarget = 2;
    test.expectedPixel = expected;
    return test;
}

MaterialCase MakeShellLayerCase(bool outerLayer) {
    using namespace HIKARI;
    auto source = MakeStandardPbrMaterialAssetData();
    const auto surface = std::find_if(source.graph.nodes.begin(), source.graph.nodes.end(), [](const auto& node) {
                             return node.kind == MaterialNodeKind::PbrSurface;
                         })->id;
    MaterialGraphNodeData layer;
    layer.id = "contract.layer";
    layer.kind = MaterialNodeKind::ShellLayer;
    layer.valueType = layer.value.type = MaterialValueType::Float4;
    source.graph.nodes.push_back(layer);
    std::erase_if(source.graph.edges, [&](const auto& edge) {
        return edge.targetNodeId == surface && (edge.targetPin == "BaseColor" || edge.targetPin == "Opacity");
    });
    source.graph.edges.push_back({"shell.output", layer.id, "Value", surface, "BaseColor"});
    const auto compiled = CompileMaterialAsset(source);
    assert(compiled.success);
    MaterialExpressionRuntimeProgram expression;
    std::string error;
    assert(BuildMaterialExpressionRuntimeProgram(compiled.program, expression, error) && expression.active);
    MaterialCase test{"base and outer shell context"};
    test.constants[0] = outerLayer ? 15 : 14;
    test.program = {};
    assert(expression.parameters.size() <= test.program.size());
    std::memcpy(test.program.data(), expression.parameters.data(), expression.parameters.size() * sizeof(MATH::Vec4));
    test.expectedTarget = 1;
    test.expectedPixel = outerLayer ? Pixel{.75f, 0, 6, .8f} : Pixel{0, 1, 0, 1};
    return test;
}

void CheckShellCompilation() {
    using namespace HIKARI;
    auto source = MakeStandardPbrMaterialAssetData();
    source.surface.alphaMode = MaterialAlphaMode::Mask;
    const auto surface = std::find_if(source.graph.nodes.begin(), source.graph.nodes.end(), [](const auto& node) {
                             return node.kind == MaterialNodeKind::PbrSurface;
                         })->id;
    MaterialGraphNodeData shell;
    shell.id = "contract.shell";
    shell.kind = MaterialNodeKind::Shell;
    shell.valueType = shell.value.type = MaterialValueType::ShellSettings;
    source.graph.nodes.push_back(shell);
    source.graph.edges.push_back({"shell.settings", shell.id, "Settings", surface, "Shell"});
    const auto parameter = [&](const char* pin, float value) {
        MaterialGraphNodeData node;
        node.id = std::string("shell.") + pin;
        node.kind = MaterialNodeKind::Parameter;
        node.referenceId = node.id;
        node.valueType = node.value.type = MaterialValueType::Float1;
        MaterialGraphParameterData data;
        data.id = node.referenceId;
        data.defaultValue.type = MaterialValueType::Float1;
        data.defaultValue.numeric = {value, 0, 0, 0};
        source.graph.parameters.push_back(data);
        source.graph.nodes.push_back(node);
        source.graph.edges.push_back({node.id + ".edge", node.id, "Value", shell.id, pin});
    };
    parameter("MaxLayers", 4);
    parameter("Thickness", .03f);
    const auto compiled = CompileMaterialAsset(source);
    assert(compiled.success && compiled.program.shell.maxLayers == 4 && compiled.program.shell.thickness == .03f);
    std::string serialized, error;
    MaterialCompileResult parsed;
    assert(MATERIAL_SERIALIZATION::SerializeMaterialCompileResult(compiled, serialized, error));
    assert(MATERIAL_SERIALIZATION::ParseMaterialCompileResult(serialized, parsed, error));
    assert(parsed.program.shell == compiled.program.shell);
    const auto binding = std::find_if(parsed.program.parameters.begin(), parsed.program.parameters.end(),
                                      [](const auto& item) { return item.parameterId == "shell.Thickness"; });
    assert(binding != parsed.program.parameters.end());
    const auto bindingIndex = static_cast<uint32_t>(binding - parsed.program.parameters.begin());
    MaterialValueData overrideValue{MaterialValueType::Float1, {.06f, 0, 0, 0}};
    MaterialShellSettings settings;
    const auto overrideParameter = [&](uint32_t index) -> const MaterialValueData* {
        return index == bindingIndex ? &overrideValue : nullptr;
    };
    assert(ResolveMaterialProgramShellSettings(parsed.program, settings, error, overrideParameter));
    assert(settings.thickness == .06f && settings.maxLayers == 4);
    overrideValue.numeric.x = std::numeric_limits<float>::infinity();
    assert(!ResolveMaterialProgramShellSettings(parsed.program, settings, error, overrideParameter));
    auto invalid = source;
    auto layers = std::find_if(invalid.graph.parameters.begin(), invalid.graph.parameters.end(),
                               [](const auto& item) { return item.id == "shell.MaxLayers"; });
    layers->defaultValue.numeric.x = 3;
    assert(!CompileMaterialAsset(invalid).success);
    invalid = source;
    auto thickness = std::find_if(invalid.graph.nodes.begin(), invalid.graph.nodes.end(),
                                  [](const auto& item) { return item.id == "shell.Thickness"; });
    thickness->kind = MaterialNodeKind::Time;
    thickness->referenceId.clear();
    for (auto& edge : invalid.graph.edges)
        if (edge.sourceNodeId == thickness->id)
            edge.sourcePin = "Time";
    assert(!CompileMaterialAsset(invalid).success);
    invalid = source;
    invalid.surface.alphaMode = MaterialAlphaMode::Blend;
    assert(!CompileMaterialAsset(invalid).success);
}

void AppendShellLodCases(std::vector<MaterialCase>& cases) {
    using namespace HIKARI::MATH;
    const auto append = [&](Vec4 settings, Vec4 bounds, const Mat4& viewProj, float height, float expectedAmount,
                            uint32_t expectedCount, float expectedFade = 1.0f) {
        MaterialCase test{"GPU shell projected LOD matches bounded fixtures"};
        test.constants[0] = 16;
        test.program = {};
        test.program[0] = {settings.x, settings.y, settings.z, settings.w};
        test.program[1] = {bounds.x, bounds.y, bounds.z, bounds.w};
        test.program[2] = {0, height, 0, 0};
        for (size_t row = 0; row < 4; ++row)
            test.program[3 + row] = {viewProj.m[0][row], viewProj.m[1][row], viewProj.m[2][row], viewProj.m[3][row]};
        test.expectedTarget = 0;
        test.expectedPixel = {expectedAmount, static_cast<float>(expectedCount), 0, 1};
        cases.push_back(test);
        test.name = "shell layers preserve fixed depth and nested identities";
        test.expectedTarget = 1;
        test.expectedPixel = {0, 1, 0, 1};
        cases.push_back(test);
        constexpr std::array lowIds{2u, 6u};
        constexpr std::array mediumIds{2u, 4u, 6u, 8u};
        for (uint32_t ordinal = 1; ordinal <= expectedCount; ++ordinal) {
            const uint32_t id = expectedCount == 2   ? lowIds[ordinal - 1]
                                : expectedCount == 4 ? mediumIds[ordinal - 1]
                                                     : ordinal;
            test.program[2][0] = static_cast<float>(ordinal);
            test.expectedPixel = {static_cast<float>(id) / 8.0f, 0, static_cast<float>(id), expectedFade};
            cases.push_back(test);
        }
    };
    const auto identity = Mat4::Identity();
    append({0, .04f, 24, 160}, {0, 0, 0, 1}, identity, 200, 0, 0);
    append({8, .04f, 24, 160}, {0, 0, 0, .1f}, identity, 200, 0, 0);
    append({8, .04f, 24, 160}, {0, 0, 0, .2f}, identity, 200, .117647059f, 2, .8805060f);
    append({8, .04f, 24, 160}, {0, 0, 0, .5f}, identity, 200, .558823529f, 4);
    append({8, .04f, 24, 160}, {0, 0, 0, 1}, identity, 200, 1, 8);
    append({2, .04f, 24, 160}, {0, 0, 0, 1}, identity, 200, 1, 2);
    append({4, .04f, 24, 160}, {0, 0, 0, 1}, identity, 200, 1, 4);
    append({8, .001f, 24, 160}, {0, 0, 0, 1}, identity, 200, 0, 0);
    append({8, .04f, 24, 160}, {0, 0, 0, 1}, identity, 0, 0, 0);
    auto perspective = identity;
    perspective.m[0][1] = .6f;
    perspective.m[1][1] = .8f;
    perspective.m[2][3] = 1;
    perspective.m[3][3] = 0;
    append({8, .04f, 24, 160}, {0, 0, 10, 1}, perspective, 200, 0, 0);
    append({8, .04f, 24, 160}, {0, 0, 2, 1}, perspective, 200, .558823529f, 4);
}

std::vector<MaterialCase> MakeCases() {
    std::vector<MaterialCase> cases;
    for (uint32_t mode = 0; mode < 9; ++mode) {
        MaterialCase test{"authored surface"};
        test.constants[0] = mode;
        cases.push_back(test);
    }
    const auto mutate = [&](const char* name, size_t slot, size_t channel, uint32_t bits) {
        MaterialCase test{name};
        test.program[slot][channel] = std::bit_cast<float>(bits);
        cases.push_back(test);
    };
    constexpr size_t header = kGpuMaterialExpressionHeaderBase;
    constexpr size_t first = kGpuMaterialExpressionInstructionBase;
    mutate("invalid opcode", first, 0, Pack(255, 2, 0, 2));
    mutate("zero components", first, 0, Pack(38, 0, 0, 2));
    mutate("oversized components", first, 0, Pack(38, 5, 0, 2));
    mutate("zero outputs", first, 0, Pack(38, 2, 0, 0));
    mutate("opcode output span", first, 0, Pack(38, 2, 0, 3));
    mutate("destination outside array", first, 0, Pack(38, 2, 255, 2));
    mutate("span outside declared registers", first, 0, Pack(38, 2, 9, 2));
    mutate("source outside declared registers", first + 1, 1, Pack(10));
    mutate("fifth source outside array", first + 7, 2, Pack(128, 0, 0, 0));
    mutate("late invalid destination", first + 7, 0, Pack(39, 1, 128, 1));
    mutate("value outside declared constants", first + 2, 2, Pack(255, 0, 3, 0));
    mutate("active output missing register", header + 1, 1, Pack(255, 2, 4, 5));
    mutate("unknown output bit", header + 1, 0, 0x7ff);
    mutate("too many instructions", header, 2, 97);
    mutate("too many registers", header, 3, 129);
    mutate("too many constants", header + 2, 0, 49);
    mutate("wrong value base", header + 2, 1, 35);
    mutate("wrong instruction base", header + 2, 2, 83);
    MaterialCase truncated{"truncated instruction block"};
    truncated.constants[2] = first + 7;
    cases.push_back(truncated);
    MaterialCase wrapped{"wrapped program-buffer offsets"};
    wrapped.constants = {10, 0u - 30u, kGpuMaterialProgramParameterSlotCount, 0u - 10u};
    wrapped.program[0] = {9, 9, 9, 9};
    cases.push_back(wrapped);
    MaterialCase lastRegister{"valid last register"};
    lastRegister.constants[0] = 1;
    lastRegister.program[header][3] = std::bit_cast<float>(128u);
    lastRegister.program[first + 7][0] = std::bit_cast<float>(Pack(39, 1, 127, 1));
    lastRegister.program[header + 1][2] = std::bit_cast<float>(Pack(3, 5, 7, 127));
    cases.push_back(lastRegister);
    MaterialCase textureEdge{"valid texture span ending at register 127"};
    textureEdge.constants[0] = 1;
    textureEdge.program[header][2] = std::bit_cast<float>(9u);
    textureEdge.program[header][3] = std::bit_cast<float>(128u);
    textureEdge.program[first + 8] =
        std::bit_cast<Pixel>(std::array<uint32_t, 4>{Pack(2, 4, 121, 7), Pack(255), Pack(255, 10, 0, 0), 0});
    cases.push_back(textureEdge);
    textureEdge.name = "texture span past register 127";
    textureEdge.constants[0] = 9;
    textureEdge.program[first + 8][0] = std::bit_cast<float>(Pack(2, 4, 122, 7));
    cases.push_back(textureEdge);
    mutate("texture resource outside table", first, 0, Pack(2, 4, 0, 1));
    cases.back().program[first][2] = std::bit_cast<float>(Pack(255, 32, 0, 0));
    mutate("texture invalid UV set", first, 0, Pack(2, 4, 0, 1));
    cases.back().program[first][2] = std::bit_cast<float>(Pack(255, 10, 0, 2));
    for (unsigned i = 0; i <= 20; ++i)
        cases.push_back(MakeFresnelCase(float(i) / 20, 0.25f, 2));
    cases.push_back(MakeFresnelCase(1, 0.04f, 0));
    cases.push_back(MakeFresnelCase(0.5f, -1, 5));
    cases.push_back(MakeFresnelCase(0.5f, 2, 5));
    cases.push_back(MakeFresnelCase(0.5f, 0.04f, -5));
    cases.push_back(MakeFresnelCase(0.5f, 0.04f, 2, true));
    cases.push_back(MakeFresnelCase(0.5f, 0.25f, 2, false, 1));
    cases.push_back(MakeFresnelCase(0.5f, 0.25f, 2, false, 2));
    const HIKARI::MATH::Vec4 flat{0, 0, 1, 0}, tilted{.6f, 0, .8f, 0};
    cases.push_back(MakeNormalCase(false, tilted, flat, 1, 1, 0, {1, 1}, {.6f, 0, .8f, 1}));
    cases.push_back(MakeNormalCase(false, flat, tilted, 1, 1, 0, {1, 1}, {.6f, 0, .8f, 1}));
    cases.push_back(MakeNormalCase(false, tilted, tilted, 1, 1, 0, {1, 1}, {.96f, 0, .28f, 1}));
    cases.push_back(MakeNormalCase(false, tilted, tilted, 1, 0, 0, {1, 1}, {.6f, 0, .8f, 1}));
    cases.push_back(MakeNormalCase(true, tilted, flat, 1, 1, 1.57079632679f, {1, 1}, {0, -.6f, .8f, 1}));
    cases.push_back(MakeNormalCase(true, tilted, flat, 1, 1, 0, {-1, 1}, {-.6f, 0, .8f, 1}));
    cases.push_back(MakeNormalCase(true, tilted, flat, 1, 1, 1.57079632679f, {-1, 1}, {0, .6f, .8f, 1}));
    cases.push_back(MakeShellLayerCase(false));
    cases.push_back(MakeShellLayerCase(true));
    AppendShellLodCases(cases);
    // 点・聚光・管・矩形、背面と異なる鏡面方向を実 PS の全画素で比較する。
    for (uint32_t kind = 0u; kind < 4u; ++kind)
        for (uint32_t orientation = 0u; orientation < 8u; ++orientation) {
            MaterialCase test{"Forward local lighting matches reference and skips zero-lobe shadows"};
            test.constants[0] = 17u;
            test.program[0] = {1.0f, 0.0f, 2.0f, 10.0f};
            test.program[1] = kind == 2u ? Pixel{1, 0, 0, 4} : Pixel{0, 0, -1, kind == 3u ? 4.0f : 0.98f};
            test.program[2] = {1, 0, 0, kind == 3u ? 3.0f : 0.0f};
            test.program[3] = {1.0f, 0.7f, 0.4f, orientation == 7u ? 0.0f : 2.0f};
            test.program[4] = {float(kind), 0.7f, 0.15f, 1.0f};
            const float angle = float(orientation) * 0.8f;
            test.program[5] = {std::sin(angle), 0.0f, std::cos(angle), orientation % 2u == 0u ? 0.15f : 0.8f};
            test.program[6] = {0.3f, 0.0f, 1.0f, orientation % 3u == 0u ? 1.0f : 0.0f};
            test.expectedTarget = 0;
            test.expectedPixel = {0, 0, 0, 1};
            cases.push_back(test);
        }
    HIKARI::MaterialProgramInstruction malformed;
    malformed.inputPins = {"Power"};
    assert(FindMaterialProgramInputRegister(malformed, "Power") == HIKARI::kInvalidMaterialProgramRegister);
    return cases;
}

HIKARI::GFX::ShaderCookOutput Compile() {
    using namespace HIKARI::GFX;
    const auto sourceRoot = std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT);
    ShaderCookRequest request;
    request.projectShaderRoot = sourceRoot / "Tools/Tests/Shaders";
    request.engineSdkIncludeRoot = sourceRoot / "HIKARI/Shaders";
    request.sourcePath = "HIKARI_MaterialSurfaceContracts.hlsl";
    request.includeRoots = {ShaderCookIncludeRoot::Project, ShaderCookIncludeRoot::EngineSdk};
    request.entries = {{"VS", ShaderStage::Vertex, "VSMain", "vs_6_0"}, {"PS", ShaderStage::Pixel, "PSMain", "ps_6_0"}};
    request.engineInterface.id = "HIKARI.MaterialSurfaceContracts";
    request.engineInterface.version = 1;
    assert(HIKARI::HASH::ComputeSha256("HIKARI.MaterialSurfaceContracts.v1", request.engineInterface.hash));
    request.rootLayout = request.engineInterface;
    request.requiredCaps = {60, 10, 0};
    ShaderCookOutput output;
    if (!CompileShaderProgramForCook(request, output))
        std::fprintf(stderr, "%s\n", output.diagnostics.c_str());
    assert(output.success && output.entries.size() == 2);
    return output;
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE heapType) {
    const auto heap = CD3DX12_HEAP_PROPERTIES(heapType);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ComPtr<ID3D12Resource> buffer;
    assert(SUCCEEDED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        heapType == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&buffer))));
    return buffer;
}

void Near(float actual, float expected) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001f)
        std::fprintf(stderr, "material surface actual=%f expected=%f\n", actual, expected);
    assert(std::isfinite(actual) && std::abs(actual - expected) <= 0.0001f);
}

void CheckUvProjection() {
    using namespace HIKARI;
    MaterialAsset model;
    model.baseColorTexture.uvScale = {2.0f, 3.0f};
    model.baseColorTexture.uvOffset = {0.1f, 0.2f};
    model.baseColorTexture.uvRotation = 0.7f;
    model.baseColorTexture.texCoord = 1;
    Material runtime;
    runtime.SetInheritModelTextures(true);
    GpuMaterialTextureProjection texture;
    texture.backendHandle = 91;
    texture.descriptorIndex = 42;
    texture.enabled = true;
    ProjectMaterialTextureUv(&model, &runtime, MaterialTextureUsage::BaseColor, texture);
    Near(texture.uvTransform.x, 2.0f);
    Near(texture.uvTransform.y, 3.0f);
    Near(texture.uvTransform.z, 0.1f);
    Near(texture.uvTransform.w, 0.2f);
    Near(texture.uvRotation, 0.7f);
    assert(texture.uvSet == 1);
    assert(texture.backendHandle == 91 && texture.descriptorIndex == 42 && texture.enabled);
    RuntimeTextureSlot slot;
    slot.enabled = true;
    slot.uvScale = {4.0f, 5.0f};
    slot.uvOffset = {0.3f, 0.4f};
    slot.uvRotation = -0.2f;
    slot.texCoord = -2;
    runtime.SetTextureSlot(MaterialTextureUsage::BaseColor, slot);
    ProjectMaterialTextureUv(&model, &runtime, MaterialTextureUsage::BaseColor, texture);
    Near(texture.uvTransform.x, 4.0f);
    Near(texture.uvTransform.y, 5.0f);
    Near(texture.uvTransform.z, 0.3f);
    Near(texture.uvTransform.w, 0.4f);
    Near(texture.uvRotation, -0.2f);
    assert(texture.uvSet == 0);
    slot.enabled = false;
    runtime.SetTextureSlot(MaterialTextureUsage::BaseColor, slot);
    ProjectMaterialTextureUv(&model, &runtime, MaterialTextureUsage::BaseColor, texture);
    Near(texture.uvTransform.x, 2.0f);
    runtime.SetInheritModelTextures(false);
    ProjectMaterialTextureUv(&model, &runtime, MaterialTextureUsage::BaseColor, texture);
    Near(texture.uvTransform.x, 4.0f);
    ProjectMaterialTextureUv(nullptr, nullptr, MaterialTextureUsage::BaseColor, texture);
    Near(texture.uvTransform.x, 1.0f);
    Near(texture.uvTransform.y, 1.0f);
    Near(texture.uvTransform.z, 0.0f);
    Near(texture.uvTransform.w, 0.0f);
    Near(texture.uvRotation, 0.0f);
    assert(texture.uvSet == 0);

    TextureAsset3D assetTexture;
    assetTexture.sourcePath = "missing-original.png";
    assetTexture.resolvedPath = "Library/Textures/canonical.htex";
    assert(GetMaterialTextureRuntimePath(assetTexture) == assetTexture.resolvedPath);
    assetTexture.sourcePath.clear();
    assert(!GetMaterialTextureRuntimePath(assetTexture).empty());
    assetTexture.resolvedPath.clear();
    assetTexture.sourcePath = "authored.png";
    assert(GetMaterialTextureRuntimePath(assetTexture) == assetTexture.sourcePath);
    const std::array<const TextureSlot*, 7> slots{
        &model.baseColorTexture, &model.normalTexture,   &model.metallicRoughnessTexture, &model.occlusionTexture,
        &model.emissiveTexture,  &model.specularTexture, &model.specularColorTexture};
    runtime.SetInheritModelTextures(true);
    for (size_t i = 0; i < slots.size(); ++i) {
        const auto usage = static_cast<MaterialTextureUsage>(i);
        assert(GetModelMaterialTextureSlot(&model, usage) == slots[i]);
        assert(ResolveMaterialTextureOverride(&runtime, usage) == nullptr);
    }
    assert(GetModelMaterialTextureSlot(&model, MaterialTextureUsage::Unknown) == nullptr);
    runtime.SetInheritModelTextures(false);
    const auto* disabledOverride = ResolveMaterialTextureOverride(&runtime, MaterialTextureUsage::BaseColor);
    assert(disabledOverride != nullptr && !disabledOverride->enabled);
    slot.enabled = true;
    runtime.SetTextureSlot(MaterialTextureUsage::BaseColor, slot);
    runtime.SetInheritModelTextures(true);
    assert(ResolveMaterialTextureOverride(&runtime, MaterialTextureUsage::BaseColor)->enabled);
}
} // namespace

void CheckTraditionalShadowProjection(const HIKARI::RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& sourceRecord) {
    using namespace HIKARI;
    using namespace RENDER3D::GPUDRIVEN;
    using namespace RENDER3D::RUNTIME;
    std::vector<GpuSceneSurfaceRecord> records(3, sourceRecord);
    for (auto& record : records)
        record.castShadow = true;
    records[1].hasRuntimeAnimation = true;
    records[2].castShadow = false;

    GpuSceneRegistry::TraditionalIndirectStream shadowStream;
    for (uint32_t index = 0; index < 3u; ++index)
        shadowStream.Append(records, index, SurfaceDrawCommandPass::Shadow);
    assert(shadowStream.HasCommands() && shadowStream.instances.size() == 3u);
    assert(SHADOW::INTERNAL::IsPrimaryShadowCaster(shadowStream.instances[0]));
    assert(SHADOW::INTERNAL::IsPrimaryShadowCaster(shadowStream.instances[1]));
    assert(!SHADOW::INTERNAL::IsPrimaryShadowCaster(shadowStream.instances[2]));

    GpuDrivenSceneSource scene;
    shadowStream.Attach(scene.GetPass(GpuDrivenPassKind::Shadow), 0u);
    scene.sourceInstanceCount = shadowStream.instances.size();
    SHADOW::INTERNAL::ShadowRendererState state;
    state.gpuDrivenSceneSource = &scene;
    assert(SHADOW::INTERNAL::BuildShadowGpuDrivenSceneSources(state));
    assert(state.frameHasStaticShadowWork && state.frameHasDynamicShadowWork);
    assert(state.staticTraditionalIndirectStream.instances.size() == 1u);
    assert(state.dynamicTraditionalIndirectStream.instances.size() == 1u);

    GpuSceneRegistry::TraditionalIndirectStream forwardStream;
    forwardStream.Append(records, 0u, SurfaceDrawCommandPass::Forward);
    assert(forwardStream.instances.size() == 1u);
    assert(!SHADOW::INTERNAL::IsPrimaryShadowCaster(forwardStream.instances[0]));
}

void CheckMaterialFactorProjection() {
    using namespace HIKARI;
    MaterialAsset model;
    model.baseColorFactor = {0.2f, 0.4f, 0.6f, 0.1f};
    model.metallicFactor = 1.0f;
    model.roughnessFactor = 0.3f;
    model.emissiveFactor = {0.1f, 0.2f, 0.3f};
    model.emissiveStrength = 8.0f;
    model.specularFactor = 0.25f;
    model.normalTexture.scale = 0.7f;
    auto values = BuildMaterialFactorProjection(&model, nullptr);
    Near(values.baseColor.y, 0.4f);
    Near(values.pbrParams.x, 1.0f);
    Near(values.pbrParams.y, 0.3f);
    Near(values.emissiveFactor.w, 8.0f);
    Near(values.specularParams.w, 0.25f);
    Near(values.normalScale, 0.7f);
    assert(ResolveMaterialAlphaMode(&model, nullptr) == AlphaMode::Opaque);
    model.alphaMode = AlphaMode::Mask;
    model.alphaCutoff = 0.37f;
    MESHRENDERER::ObjectCB object{};
    MESHRENDERER::FillMaterialFactorValues(object, &model, nullptr);
    Near(object.metallicFactor, 1.0f);
    Near(object.emissiveFactor.w, 8.0f);
    Near(object.alphaCutoff, 0.37f);
    assert((object.materialFlags & MATERIAL_FEATURES::AlphaMask) != 0);

    Material runtime;
    runtime.SetBaseColor({0.8f, 0.7f, 0.6f, 0.2f});
    runtime.SetMetallicFactor(0.15f);
    runtime.SetRoughnessFactor(0.65f);
    runtime.SetEmissiveFactor({0.6f, 0.3f, 0.1f});
    runtime.SetEmissiveStrength(4.0f);
    runtime.SetFeatureBits(MATERIAL_FEATURES::Unlit | MATERIAL_FEATURES::Emissive);
    // 清漆・透過は surfaceLayers（coat, coatRoughness, transmission, ior）として実行時材質からそのまま写す。
    runtime.SetSurfaceLayers({0.75f, 0.2f, 0.5f, 1.33f});
    runtime.SetSubsurface({0.6f, 0.05f});
    values = BuildMaterialFactorProjection(&model, &runtime);
    Near(values.subsurface.x, 0.6f);
    Near(values.subsurface.y, 0.05f);
    Near(values.surfaceLayers.x, 0.75f);
    Near(values.surfaceLayers.y, 0.2f);
    Near(values.surfaceLayers.z, 0.5f);
    Near(values.surfaceLayers.w, 1.33f);
    Near(values.baseColor.x, 0.8f);
    Near(values.pbrParams.x, 0.15f);
    Near(values.pbrParams.y, 0.65f);
    Near(values.emissiveFactor.x, 0.6f);
    Near(values.emissiveFactor.w, 4.0f);
    assert(ResolveMaterialAlphaMode(&model, &runtime) == AlphaMode::Opaque);
    assert((values.materialFlags & MATERIAL_FEATURES::AlphaMask) == 0);
    // typed alpha mode と旧 feature bits の両方が描画/影/投影で同じ規則を使う。
    runtime.SetMaterialAlphaMode(RuntimeMaterialAlphaMode::Mask);
    const auto runtimeProjection = BuildRuntimeMaterialGpuProjection(runtime);
    Near(runtimeProjection.surfaceLayers.z, 0.5f);
    assert((runtimeProjection.materialFlags & MATERIAL_FEATURES::AlphaMask) != 0);
    assert((runtimeProjection.materialFlags & MATERIAL_FEATURES::Unlit) != 0);
    assert(RENDER3D::RUNTIME::ResolveSurfaceDrawShaderRoute(&runtime, &model, {}).alphaMode == AlphaMode::Mask);
    MESHRENDERER::FillMaterialFactorValues(object, &model, &runtime);
    assert(object.materialFlags == runtimeProjection.materialFlags);
    Near(object.emissiveFactor.w, runtimeProjection.emissiveFactor.w);
    runtime.SetMaterialAlphaMode(RuntimeMaterialAlphaMode::Opaque);
    runtime.SetFeatureBits(MATERIAL_FEATURES::ThinTransparentSurface);
    assert(ResolveMaterialAlphaMode(&model, &runtime) == AlphaMode::Blend);
    MESHRENDERER::DrawItem drawItem{};
    drawItem.materialOverride = &runtime;
    drawItem.variant.depthWrite = true;
    assert(!MESHRENDERER::ResolvePrimitiveVariant(drawItem, &model).depthWrite);
    runtime.SetMaterialAlphaMode(RuntimeMaterialAlphaMode::Mask);
    values = BuildMaterialFactorProjection(&model, &runtime);
    assert((values.materialFlags & MATERIAL_FEATURES::ThinTransparentSurface) == 0);
    assert((values.materialFlags & MATERIAL_FEATURES::AlphaMask) != 0);

    // public API で alpha mode を変更した時も、Shell geometry と bounds が同じ資格を使う。
    MaterialShellSettings shell;
    shell.maxLayers = 4;
    shell.thickness = .125f;
    runtime.SetShellSettings(shell);
    ModelAsset sourceModel;
    sourceModel.materials.push_back(model);
    sourceModel.meshes.resize(1);
    sourceModel.meshes[0].primitives.resize(1);
    sourceModel.meshes[0].primitives[0].staticVertices.resize(3);
    sourceModel.meshes[0].primitives[0].indices = {0, 1, 2};
    RENDER3D::RUNTIME::RenderModelAsset renderModel;
    renderModel.valid = true;
    RENDER3D::RUNTIME::RenderSurfaceRecord surface;
    surface.meshIndex = surface.primitiveIndex = 0;
    RENDER3D::RUNTIME::SceneSurfaceInstance source;
    source.model = &sourceModel;
    source.renderModel = &renderModel;
    source.surface = &surface;
    source.meshIndex = source.primitiveIndex = 0;
    source.materialOverride = &runtime;
    source.hasDrawWorldMatrix = true;
    source.drawWorldMatrix = source.clusterWorldMatrix = MATH::Mat4::Identity();
    source.worldBounds = {{-1, -1, -1}, {1, 1, 1}};
    for (const auto mode :
         {RuntimeMaterialAlphaMode::Mask, RuntimeMaterialAlphaMode::Blend, RuntimeMaterialAlphaMode::Opaque}) {
        runtime.SetMaterialAlphaMode(mode);
        runtime.SetFeatureBits(0);
        const auto record = RENDER3D::GPUDRIVEN::BuildGpuSceneSurfaceRecord(source, 0);
        assert(record.valid);
        const bool active = mode == RuntimeMaterialAlphaMode::Mask;
        // 殻の包囲は押し出しの揺らぎ（kMaterialShellPushVariation）まで含める。
        Near(record.worldBounds.max.x, active ? 1.0f + .125f * (1.0f + kMaterialShellPushVariation) : 1.0f);
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> instances;
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> materials;
        RENDER3D::GPUDRIVEN::BuildGpuSceneInstanceList({record}, {0}, instances, materials);
        assert(instances.size() == 1);
        Near(instances[0].shellSettings.x, active ? 4.0f : 0.0f);
        Near(instances[0].shellSettings.y, active ? .125f : 0.0f);
    }
    CheckTraditionalShadowProjection(RENDER3D::GPUDRIVEN::BuildGpuSceneSurfaceRecord(source, 0));
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    CheckMaterialFactorProjection();
    CheckUvProjection();
    CheckShellCompilation();
    HIKARI::TESTS::INTERNAL::GpuContractDevice gpu;
    const auto shaders = Compile();
    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kTextureCount, 20);
    std::array<CD3DX12_ROOT_PARAMETER, 4> parameters;
    parameters[0].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    parameters[1].InitAsShaderResourceView(0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
    parameters[2].InitAsShaderResourceView(1, 6, D3D12_SHADER_VISIBILITY_PIXEL);
    parameters[3].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_PIXEL);
    const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    const D3D12_ROOT_SIGNATURE_DESC signatureDesc{static_cast<UINT>(parameters.size()), parameters.data(), 1, &sampler};
    ComPtr<ID3DBlob> signature, errors;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    auto program = MakeProgram();
    const auto upload = Buffer(gpu.device.Get(), sizeof(program), D3D12_HEAP_TYPE_UPLOAD);
    const auto resources =
        Buffer(gpu.device.Get(), kGpuMaterialProgramResourceSlotCount * sizeof(uint32_t), D3D12_HEAP_TYPE_UPLOAD);
    void* mapped = nullptr;
    const D3D12_RANGE empty{0, 0};
    assert(SUCCEEDED(upload->Map(0, &empty, &mapped)));
    std::memcpy(mapped, program.data(), sizeof(program));
    upload->Unmap(0, nullptr);
    assert(SUCCEEDED(resources->Map(0, &empty, &mapped)));
    std::memset(mapped, 0xff, kGpuMaterialProgramResourceSlotCount * sizeof(uint32_t));
    static_cast<uint32_t*>(mapped)[0] = 0;
    resources->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kTextureCount,
                                           D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&srvHeap))));
    auto handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT srvStride = gpu.device->GetDescriptorHandleIncrementSize(descriptors.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    for (UINT index = 0; index < kTextureCount; ++index, handle.ptr += srvStride)
        gpu.device->CreateShaderResourceView(nullptr, &nullSrv, handle);

    // Alpha 勾配を実テクスチャに転送し、係数・UV1・回転と無効 slot を検証する。
    const auto sourceTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, kSize, 1, 1, 1);
    const auto sourceHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> sourceTexture;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&sourceHeap, D3D12_HEAP_FLAG_NONE, &sourceTextureDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&sourceTexture))));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT sourceFootprint;
    UINT64 sourceBytes = 0;
    gpu.device->GetCopyableFootprints(&sourceTextureDesc, 0, 1, 0, &sourceFootprint, nullptr, nullptr, &sourceBytes);
    const auto textureUpload = Buffer(gpu.device.Get(), sourceBytes, D3D12_HEAP_TYPE_UPLOAD);
    assert(SUCCEEDED(textureUpload->Map(0, &empty, &mapped)));
    auto* sourcePixels = reinterpret_cast<Pixel*>(static_cast<std::byte*>(mapped) + sourceFootprint.Offset);
    for (UINT x = 0; x < kSize; ++x)
        sourcePixels[x] = {1, 1, 1, (x + 0.5f) / kSize};
    textureUpload->Unmap(0, nullptr);
    gpu.ResetCommands();
    const CD3DX12_TEXTURE_COPY_LOCATION copySource(textureUpload.Get(), sourceFootprint);
    const CD3DX12_TEXTURE_COPY_LOCATION copyDestination(sourceTexture.Get(), 0);
    gpu.commands->CopyTextureRegion(&copyDestination, 0, 0, 0, &copySource, nullptr);
    const auto sourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(sourceTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.commands->ResourceBarrier(1, &sourceBarrier);
    gpu.SubmitAndWait(1);
    nullSrv.Format = sourceTextureDesc.Format;
    gpu.device->CreateShaderResourceView(sourceTexture.Get(), &nullSrv, srvHeap->GetCPUDescriptorHandleForHeapStart());

    const auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, kSize, kSize, 1, 1, 1, 0,
                                                          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    std::array<ComPtr<ID3D12Resource>, 4> targets, readbacks;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvs;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 4> footprints;
    descriptors = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4};
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&rtvHeap))));
    const UINT rtvStride = gpu.device->GetDescriptorHandleIncrementSize(descriptors.Type);
    for (size_t i = 0; i < targets.size(); ++i) {
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                             D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                             IID_PPV_ARGS(&targets[i]))));
        UINT64 bytes = 0;
        gpu.device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprints[i], nullptr, nullptr, &bytes);
        readbacks[i] = Buffer(gpu.device.Get(), bytes, D3D12_HEAP_TYPE_READBACK);
        rtvs[i] = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvs[i].ptr += i * rtvStride;
        gpu.device->CreateRenderTargetView(targets[i].Get(), nullptr, rtvs[i]);
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    for (const auto& entry : shaders.entries) {
        if (entry.id == "VS")
            desc.VS = {entry.dxil.data(), entry.dxil.size()};
        if (entry.id == "PS")
            desc.PS = {entry.dxil.data(), entry.dxil.size()};
    }
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 4;
    for (UINT i = 0; i < 4; ++i)
        desc.RTVFormats[i] = textureDesc.Format;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))));
    UINT64 fenceValue = 2;
    for (const auto& test : MakeCases()) {
        const UINT mode = test.constants[0];
        std::printf("Material case: %s (mode %u)\n", test.name, mode);
        // 前回の fence 完了後にだけ、次のプログラムを書き込む。
        assert(SUCCEEDED(upload->Map(0, &empty, &mapped)));
        std::memcpy(mapped, test.program.data(), sizeof(test.program));
        upload->Unmap(0, nullptr);
        gpu.ResetCommands();
        gpu.commands->SetGraphicsRootSignature(root.Get());
        gpu.commands->SetGraphicsRoot32BitConstants(0, 4, test.constants.data(), 0);
        gpu.commands->SetGraphicsRootShaderResourceView(1, upload->GetGPUVirtualAddress());
        gpu.commands->SetGraphicsRootShaderResourceView(2, resources->GetGPUVirtualAddress());
        ID3D12DescriptorHeap* heaps[]{srvHeap.Get()};
        gpu.commands->SetDescriptorHeaps(1, heaps);
        gpu.commands->SetGraphicsRootDescriptorTable(3, srvHeap->GetGPUDescriptorHandleForHeapStart());
        gpu.commands->SetPipelineState(pipeline.Get());
        const D3D12_VIEWPORT viewport{0, 0, kSize, kSize, 0, 1};
        const D3D12_RECT scissor{0, 0, kSize, kSize};
        gpu.commands->RSSetViewports(1, &viewport);
        gpu.commands->RSSetScissorRects(1, &scissor);
        gpu.commands->OMSetRenderTargets(4, rtvs.data(), FALSE, nullptr);
        const Pixel clear{-1, -1, -1, -1};
        for (const auto rtv : rtvs)
            gpu.commands->ClearRenderTargetView(rtv, clear.data(), 0, nullptr);
        gpu.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu.commands->DrawInstanced(3, 1, 0, 0);
        for (size_t i = 0; i < targets.size(); ++i) {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                targets[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            gpu.commands->ResourceBarrier(1, &barrier);
            const CD3DX12_TEXTURE_COPY_LOCATION destination(readbacks[i].Get(), footprints[i]);
            const CD3DX12_TEXTURE_COPY_LOCATION source(targets[i].Get(), 0);
            gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            gpu.commands->ResourceBarrier(1, &barrier);
        }
        gpu.SubmitAndWait(fenceValue++);
        for (size_t i = 0; i < targets.size(); ++i) {
            const D3D12_RANGE range{0, static_cast<SIZE_T>(readbacks[i]->GetDesc().Width)};
            assert(SUCCEEDED(readbacks[i]->Map(0, &range, &mapped)));
            for (UINT y = 0; y < kSize; ++y) {
                const auto* row =
                    reinterpret_cast<const Pixel*>(static_cast<const std::byte*>(mapped) + footprints[i].Offset +
                                                   y * footprints[i].Footprint.RowPitch);
                for (UINT x = 0; x < kSize; ++x) {
                    if (test.expectedTarget >= 0) {
                        if (i == static_cast<size_t>(test.expectedTarget))
                            for (size_t channel = 0; channel < 4; ++channel)
                                Near(row[x][channel], test.expectedPixel[channel]);
                        continue;
                    }
                    const float u = (x + 0.5f) / kSize, v = (y + 0.5f) / kSize;
                    const bool nodeMaterial = mode > 0 && mode < 4;
                    const bool discarded =
                        (nodeMaterial && x < 8) || mode == 4 || (mode == 6 && y < 8) || (mode == 8 && x >= 8);
                    Pixel expected{};
                    if (discarded)
                        expected = clear;
                    else if (i == 0) {
                        const float nx = !nodeMaterial ? 0.0f : mode == 2 ? 1.0f : 0.6f;
                        const float nz = !nodeMaterial ? 1.0f : mode == 2 ? 0.0f : 0.8f;
                        const float octX = mode == 3 ? 1.0f : nx / (nx + nz);
                        const float octY = mode == 3 ? (1.0f - nx / (nx + nz)) : 0.0f;
                        expected = {octX * 0.5f + 0.5f, octY * 0.5f + 0.5f, !nodeMaterial ? 0.8f : (std::max)(v, 0.04f),
                                    !nodeMaterial ? 0.2f : 0.75f};
                    } else if (i == 1) {
                        const float alpha = mode == 6   ? v * 0.5f
                                            : mode == 7 ? 0.5f
                                            : mode == 8 ? (1.0f - u) * 0.5f
                                                        : 1.0f;
                        expected = !nodeMaterial ? Pixel{0.2f, 0.3f, 0.4f, alpha} : Pixel{u, v, 0.25f, u};
                    } else if (i == 2)
                        expected = !nodeMaterial ? Pixel{0, 0, 1, 1}
                                   : mode == 2   ? Pixel{1, 0, 0, 0.75f}
                                                 : Pixel{0.6f, 0, mode == 3 ? -0.8f : 0.8f, 0.75f};
                    else
                        expected = !nodeMaterial ? Pixel{1, 1, 1, 1} : Pixel{u, v, 0.25f, 1.75f};
                    if (mode >= 11 && i == 0) {
                        expected[3] = test.fresnelExpected;
                        if (mode == 12) {
                            const float vx = 8 - (x + 0.5f), vy = 8 - (y + 0.5f);
                            const float noV = 8 / std::sqrt(vx * vx + vy * vy + 64);
                            expected[3] = 0.04f + 0.96f * std::pow(1 - noV, 2.0f);
                        }
                    }
                    for (size_t channel = 0; channel < 4; ++channel)
                        Near(row[x][channel], expected[channel]);
                }
            }
            readbacks[i]->Unmap(0, &empty);
        }
    }
    std::puts("Material coverage, context, PBR, normals and malformed-program fallback contracts passed.");
}
