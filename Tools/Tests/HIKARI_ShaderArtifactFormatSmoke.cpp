#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Formats/HIKARI_HshadercFormat.h"
#include "Assets/Shader/HIKARI_BuiltinShaderCatalog.h"
#include "Assets/Shader/HIKARI_ShaderParameterSerialization.h"
#include <json.hpp>
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

namespace {

namespace SHADER = HIKARI::ASSETS::SHADER;

HIKARI::HASH::Sha256Digest Hash(std::string_view text) {
    HIKARI::HASH::Sha256Digest digest{};
    assert(HIKARI::HASH::ComputeSha256(text, digest));
    return digest;
}

SHADER::ShaderCookedEntry MakeEntry(std::string id, SHADER::ShaderStage stage, std::string symbol,
                                    std::string profile) {
    SHADER::ShaderCookedEntry entry{};
    entry.entry = {std::move(id), stage, std::move(symbol), std::move(profile)};
    entry.compilerArguments = {"-E", entry.entry.entry, "-T", entry.entry.profile, "-HV", "2021", "-O3", "-WX"};
    entry.bindings = {
        {SHADER::ShaderResourceBindingKind::ConstantBuffer, 0u, 0u, 1u, 1u << static_cast<uint32_t>(stage)}};
    entry.dxil = {'D', 'X', 'B', 'C', static_cast<uint8_t>(stage), 1u, 2u, 3u};
    assert(SHADER::ComputeShaderArgumentsHash(entry.compilerArguments, entry.compilerArgumentsHash));
    assert(SHADER::ComputeShaderReflectionHash(entry.bindings, entry.reflectionHash));
    assert(HIKARI::HASH::ComputeSha256(std::as_bytes(std::span<const uint8_t>(entry.dxil)), entry.dxilHash));
    return entry;
}

SHADER::ShaderCookedArtifact MakeArtifact() {
    SHADER::ShaderCookedArtifact artifact{};
    artifact.sourceGuid.value = "0123456789abcdef0123456789abcdef";
    artifact.programId = "Sample.Lit";
    artifact.manifestHash = Hash("manifest");
    artifact.dependencies = {{"EngineSdk/Forward.hlsli", 16u, Hash("engine")},
                             {"Project/SampleLit.hlsl", 32u, Hash("project")}};
    assert(SHADER::ComputeShaderSourceClosureHash(artifact.manifestHash, artifact.dependencies,
                                                  artifact.sourceClosureHash));
    artifact.compiler = {"DXC", "1.8.2505.1", 1u, Hash("dxcompiler.dll"), Hash("dxil.dll")};
    artifact.engineInterfaceId = "HIKARI.RenderGraph.Sample";
    artifact.engineInterfaceVersion = 1u;
    artifact.engineInterfaceHash = Hash("interface");
    artifact.rootLayoutId = "HIKARI.Root.Sample";
    artifact.rootLayoutVersion = 1u;
    artifact.rootLayoutHash = Hash("root-layout");
    artifact.requiredCaps.minimumShaderModel = 65u;
    artifact.requiredCaps.minimumRootSignatureVersion = 11u;
    artifact.requiredCaps.requiredFeatureMask = SHADER::ShaderDeviceFeature::MeshShader;
    artifact.entries.push_back(MakeEntry("MeshMain", SHADER::ShaderStage::Mesh, "MSMain", "ms_6_5"));
    artifact.entries.push_back(MakeEntry("PixelMain", SHADER::ShaderStage::Pixel, "PSMain", "ps_6_0"));
    return artifact;
}

std::vector<SHADER::ShaderParameterDefinition> MakeParameters() {
    using HIKARI::BINDING::Value;
    using HIKARI::BINDING::ValueType;
    std::vector<SHADER::ShaderParameterDefinition> definitions{};

    SHADER::ShaderParameterDefinition mask{};
    mask.key = "useMask";
    mask.label = "遮罩";
    mask.group = "Treatment";
    mask.slot = 0u;
    mask.defaultValue = HIKARI::BINDING::MakeDefaultValue(ValueType::Bool);
    mask.defaultValue.boolean = true;
    definitions.push_back(mask);

    SHADER::ShaderParameterDefinition steps{};
    steps.key = "steps";
    steps.label = "Steps";
    steps.group = "Treatment";
    steps.slot = 1u;
    steps.defaultValue = HIKARI::BINDING::MakeDefaultValue(ValueType::Int);
    steps.defaultValue.integer = 4;
    steps.hasRange = true;
    steps.minimum = 2.0f;
    steps.maximum = 8.0f;
    steps.step = 1.0f;
    definitions.push_back(steps);

    SHADER::ShaderParameterDefinition offset{};
    offset.key = "offset.xy";
    offset.label = "Offset";
    offset.group = "";
    offset.slot = 2u;
    offset.defaultValue = HIKARI::BINDING::MakeDefaultValue(ValueType::Float2);
    offset.defaultValue.numeric = {0.25f, -0.25f, 0.0f, 0.0f};
    offset.hasRange = true;
    offset.minimum = -1.0f;
    offset.maximum = 1.0f;
    offset.step = 0.01f;
    definitions.push_back(offset);

    SHADER::ShaderParameterDefinition color{};
    color.key = "tint";
    color.label = "Tint";
    color.group = "Treatment";
    color.slot = 3u;
    color.defaultValue = HIKARI::BINDING::MakeDefaultValue(ValueType::Color);
    color.defaultValue.numeric = {1.0f, 0.5f, 0.25f, 0.8f};
    definitions.push_back(color);
    return definitions;
}

} // namespace

int main() {
    SHADER::BuiltinShaderCatalog catalog;
    SHADER::ShaderSourceManifest manifest, parsed;
    std::string source, parseError;
    assert(SHADER::LoadBuiltinShaderCatalog(std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT) /
                                                "HIKARI/Shaders/BuiltinShaderCatalog.json",
                                            catalog, parseError));
    const auto& program = *std::find_if(catalog.programs.begin(), catalog.programs.end(), [](const auto& value) {
        return value.state == SHADER::BuiltinShaderProgramState::Cooked;
    });
    assert(SHADER::BuildBuiltinShaderSourceManifest(program, manifest, parseError));
    assert(SHADER::SerializeCanonicalShaderSourceManifest(manifest, source, parseError));
    assert(SHADER::ParseShaderSourceManifest(source, parsed, parseError));
    const HIKARI::HASH::Sha256Digest emptyParametersHash = [&] {
        HIKARI::HASH::Sha256Digest hash{};
        assert(SHADER::ComputeShaderSourceManifestHash(manifest, hash, parseError));
        auto emptyParametersNode = nlohmann::json::parse(source);
        emptyParametersNode["parameters"] = nlohmann::json::array();
        assert(SHADER::ParseShaderSourceManifest(emptyParametersNode.dump(), parsed, parseError));
        std::string recanonicalized{};
        assert(SHADER::SerializeCanonicalShaderSourceManifest(parsed, recanonicalized, parseError));
        assert(recanonicalized == source);
        return hash;
    }();
    manifest.parameters = MakeParameters();
    assert(SHADER::ValidateShaderParameterDefinitions(manifest.parameters, parseError));
    std::string parameterizedSource{};
    assert(SHADER::SerializeCanonicalShaderSourceManifest(manifest, parameterizedSource, parseError));
    assert(SHADER::ParseShaderSourceManifest(parameterizedSource, parsed, parseError));
    assert(parsed.parameters.size() == manifest.parameters.size());
    HIKARI::HASH::Sha256Digest parameterizedHash{};
    assert(SHADER::ComputeShaderSourceManifestHash(parsed, parameterizedHash, parseError));
    assert(parameterizedHash != emptyParametersHash);
    auto parameterizedNode = nlohmann::json::parse(parameterizedSource);
    parameterizedNode["parameters"][1]["default"] = 5;
    assert(SHADER::ParseShaderSourceManifest(parameterizedNode.dump(), parsed, parseError));
    HIKARI::HASH::Sha256Digest changedDefaultHash{};
    assert(SHADER::ComputeShaderSourceManifestHash(parsed, changedDefaultHash, parseError));
    assert(changedDefaultHash != parameterizedHash);

    auto node = nlohmann::json::parse(source);
    node.erase("defines");
    node.erase("includeRoots");
    assert(SHADER::ParseShaderSourceManifest(node.dump(), parsed, parseError));
    const auto validNode = node;
    node["entries"][0]["unexpected"] = true;
    assert(!SHADER::ParseShaderSourceManifest(node.dump(), parsed, parseError));
    node = validNode;
    node["entries"] = nlohmann::json::array();
    assert(!SHADER::ParseShaderSourceManifest(node.dump(), parsed, parseError));
    node = validNode;
    node["defines"] = {{{"name", "BAD"}, {"value", 123}}};
    assert(!SHADER::ParseShaderSourceManifest(node.dump(), parsed, parseError));
    node = validNode;
    node["includeRoots"] = {"Unknown"};
    assert(!SHADER::ParseShaderSourceManifest(node.dump(), parsed, parseError));

    SHADER::ShaderCookedArtifact artifact = MakeArtifact();
    std::string error{};
    artifact.parameters = MakeParameters();
    artifact.manifestHash = parameterizedHash;
    assert(SHADER::ComputeShaderSourceClosureHash(artifact.manifestHash, artifact.dependencies,
                                                  artifact.sourceClosureHash));
    assert(SHADER::ValidateShaderCookedArtifact(artifact, error));

    std::vector<uint8_t> bytes{};
    assert(SHADER::SerializeHshaderc(artifact, bytes, error));
    SHADER::ShaderCookedArtifact decoded{};
    assert(SHADER::DeserializeHshaderc(bytes, decoded, error));
    assert(decoded.sourceGuid == artifact.sourceGuid);
    assert(decoded.programId == artifact.programId);
    assert(decoded.sourceClosureHash == artifact.sourceClosureHash);
    assert(decoded.compiler.contractVersion == artifact.compiler.contractVersion);
    assert(decoded.compiler.validatorBinaryHash == artifact.compiler.validatorBinaryHash);
    assert(decoded.engineInterfaceHash == artifact.engineInterfaceHash);
    assert(decoded.rootLayoutVersion == artifact.rootLayoutVersion);
    assert(decoded.rootLayoutHash == artifact.rootLayoutHash);
    assert(decoded.entries.size() == artifact.entries.size());
    assert(decoded.entries[0].dxil == artifact.entries[0].dxil);
    assert(decoded.entries[1].dxil == artifact.entries[1].dxil);
    assert(decoded.manifestHash == parameterizedHash);
    assert(SHADER::WriteShaderParameterDefinitions(decoded.parameters) ==
           SHADER::WriteShaderParameterDefinitions(
               std::span<const SHADER::ShaderParameterDefinition>(artifact.parameters)));

    SHADER::ShaderCookedArtifact emptyArtifact = MakeArtifact();
    emptyArtifact.manifestHash = emptyParametersHash;
    assert(SHADER::ComputeShaderSourceClosureHash(emptyArtifact.manifestHash, emptyArtifact.dependencies,
                                                  emptyArtifact.sourceClosureHash));
    std::vector<uint8_t> emptyBytes{};
    assert(SHADER::SerializeHshaderc(emptyArtifact, emptyBytes, error));
    assert(bytes != emptyBytes);
    SHADER::ShaderCookedArtifact emptyDecoded{};
    assert(SHADER::DeserializeHshaderc(emptyBytes, emptyDecoded, error));
    assert(emptyDecoded.parameters.empty() && emptyDecoded.manifestHash == emptyParametersHash);

    SHADER::ShaderParameterSlots slots{};
    SHADER::ShaderParameterOverride inactiveNanOverride{};
    inactiveNanOverride.key = "offset.xy";
    inactiveNanOverride.value = HIKARI::BINDING::MakeDefaultValue(HIKARI::BINDING::ValueType::Float2);
    inactiveNanOverride.value.numeric = {0.5f, 0.25f, std::numeric_limits<float>::quiet_NaN(),
                                         std::numeric_limits<float>::infinity()};
    assert(SHADER::PackShaderParameterValues(
        artifact.parameters, std::span<const SHADER::ShaderParameterOverride>(&inactiveNanOverride, 1u), slots, error));
    assert(slots[2] == (SHADER::ShaderParameterSlot{0.5f, 0.25f, 0.0f, 0.0f}));
    assert(slots[0] == (SHADER::ShaderParameterSlot{1.0f, 0.0f, 0.0f, 0.0f}));

    SHADER::ShaderParameterSlot slotCanary{9.0f, 8.0f, 7.0f, 6.0f};
    SHADER::ShaderParameterOverride outOfRangeOverride = inactiveNanOverride;
    outOfRangeOverride.value.numeric.x = 2.0f;
    assert(!SHADER::PackShaderParameterValue(artifact.parameters[2], outOfRangeOverride.value, slotCanary, error));
    assert(slotCanary == (SHADER::ShaderParameterSlot{9.0f, 8.0f, 7.0f, 6.0f}));

    HIKARI::BINDING::Value integerCanary = HIKARI::BINDING::MakeDefaultValue(HIKARI::BINDING::ValueType::Int);
    integerCanary.integer = 123;
    const nlohmann::json overflowingInteger(std::numeric_limits<uint64_t>::max());
    assert(overflowingInteger.is_number_unsigned());
    assert(
        !SHADER::ReadShaderParameterValue(overflowingInteger, HIKARI::BINDING::ValueType::Int, integerCanary, error));
    assert(integerCanary.integer == 123);

    SHADER::ShaderParameterSlots slotsCanary{};
    slotsCanary[15] = {4.0f, 3.0f, 2.0f, 1.0f};
    SHADER::ShaderParameterOverride invalidTableOverride = inactiveNanOverride;
    invalidTableOverride.value.numeric.x = 2.0f;
    assert(!SHADER::PackShaderParameterValues(
        artifact.parameters, std::span<const SHADER::ShaderParameterOverride>(&invalidTableOverride, 1u), slotsCanary,
        error));
    assert(slotsCanary[15] == (SHADER::ShaderParameterSlot{4.0f, 3.0f, 2.0f, 1.0f}));

    std::vector<uint8_t> corrupted = bytes;
    corrupted[0] ^= 0xffu;
    assert(!SHADER::DeserializeHshaderc(corrupted, decoded, error));
    corrupted = bytes;
    corrupted.back() ^= 0xffu;
    assert(!SHADER::DeserializeHshaderc(corrupted, decoded, error));

    SHADER::ShaderCookedArtifact invalid = MakeArtifact();
    invalid.dependencies[1].logicalPath = "Project/../Escape.hlsl";
    assert(!SHADER::ValidateShaderCookedArtifact(invalid, error));
    invalid = MakeArtifact();
    invalid.entries[1].entry.id = invalid.entries[0].entry.id;
    assert(!SHADER::ValidateShaderCookedArtifact(invalid, error));
    invalid = MakeArtifact();
    invalid.dependencies[1].byteSize += 1u;
    assert(!SHADER::ValidateShaderCookedArtifact(invalid, error));
    invalid = MakeArtifact();
    invalid.dependencies[1].byteSize = SHADER::kMaximumShaderDependencyBytes + 1u;
    assert(
        !SHADER::ComputeShaderSourceClosureHash(invalid.manifestHash, invalid.dependencies, invalid.sourceClosureHash));
    invalid = MakeArtifact();
    invalid.entries[0].bindings.push_back({SHADER::ShaderResourceBindingKind::ConstantBuffer, 0u, 0u, 2u,
                                           1u << static_cast<uint32_t>(SHADER::ShaderStage::Mesh)});
    invalid.entries[0].bindings.push_back({SHADER::ShaderResourceBindingKind::ConstantBuffer, 1u, 0u, 1u,
                                           1u << static_cast<uint32_t>(SHADER::ShaderStage::Mesh)});
    assert(SHADER::ComputeShaderReflectionHash(invalid.entries[0].bindings, invalid.entries[0].reflectionHash));
    assert(!SHADER::ValidateShaderCookedArtifact(invalid, error));
    invalid = MakeArtifact();
    invalid.entries[0].compilerArguments.push_back("-Od");
    assert(SHADER::ComputeShaderArgumentsHash(invalid.entries[0].compilerArguments,
                                              invalid.entries[0].compilerArgumentsHash));
    assert(!SHADER::ValidateShaderCookedArtifact(invalid, error));
    invalid = MakeArtifact();
    invalid.compiler.contractVersion += 1u;
    assert(!SHADER::ValidateShaderCookedArtifact(invalid, error));
    return 0;
}
