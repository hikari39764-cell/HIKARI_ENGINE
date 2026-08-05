#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace HIKARI::RENDER3D::MATERIAL {

    namespace {
        MaterialProgramRegistry gRuntimeMaterialProgramRegistry{};
    }
    namespace {
        MaterialCompileDiagnostic MakeRegistryDiagnostic(
            MaterialCompileDiagnosticSeverity severity,
            std::string code,
            std::string message,
            std::string subjectId = {}) {
            return MaterialCompileDiagnostic{
                severity,
                std::move(code),
                std::move(message),
                std::move(subjectId)
            };
        }

        bool IsRuntimeProgramShapeValid(
            const MaterialProgramIR& program,
            std::vector<MaterialCompileDiagnostic>& diagnostics) {
            bool valid = true;
            if (program.version != kMaterialProgramIrVersion) {
                diagnostics.push_back(MakeRegistryDiagnostic(
                    MaterialCompileDiagnosticSeverity::Error,
                    "Material.RuntimeProgramVersionMismatch",
                    "Material program IR version is not supported by this runtime."));
                valid = false;
            }
            if (program.domain != MaterialDomain::Surface ||
                program.key.domain != program.domain) {
                diagnostics.push_back(MakeRegistryDiagnostic(
                    MaterialCompileDiagnosticSeverity::Error,
                    "Material.RuntimeProgramDomainMismatch",
                    "Material program and key domains do not describe one Surface program."));
                valid = false;
            }
            if (program.key.structureHash == 0u ||
                program.key.capabilityMask != program.capabilities) {
                diagnostics.push_back(MakeRegistryDiagnostic(
                    MaterialCompileDiagnosticSeverity::Error,
                    "Material.RuntimeProgramKeyInvalid",
                    "Material program key is empty or does not match its capability mask."));
                valid = false;
            }

            std::unordered_set<std::string> parameterIds{};
            for (const MaterialProgramParameterBinding& parameter : program.parameters) {
                if (parameter.parameterId.empty() ||
                    !parameterIds.insert(parameter.parameterId).second ||
                    parameter.byteSize == 0u ||
                    parameter.byteSize > 16u ||
                    (parameter.byteOffset % 16u) != 0u) {
                    diagnostics.push_back(MakeRegistryDiagnostic(
                        MaterialCompileDiagnosticSeverity::Error,
                        "Material.RuntimeParameterLayoutInvalid",
                        "Material parameter IDs must be unique and each value must occupy one aligned float4 slot.",
                        parameter.parameterId));
                    valid = false;
                }
            }

            std::unordered_set<std::string> resourceIds{};
            std::unordered_set<uint32_t> resourceIndices{};
            for (const MaterialProgramResourceBinding& resource : program.resources) {
                if (resource.resourceId.empty() ||
                    !resourceIds.insert(resource.resourceId).second ||
                    !resourceIndices.insert(resource.resourceIndex).second) {
                    diagnostics.push_back(MakeRegistryDiagnostic(
                        MaterialCompileDiagnosticSeverity::Error,
                        "Material.RuntimeResourceLayoutInvalid",
                        "Material resource IDs and resource indices must be unique.",
                        resource.resourceId));
                    valid = false;
                }
            }

            constexpr uint32_t kMaxMaterialProgramRegisters = 2048u;
            if (program.registerCount > kMaxMaterialProgramRegisters) {
                diagnostics.push_back(MakeRegistryDiagnostic(
                    MaterialCompileDiagnosticSeverity::Error,
                    "Material.RuntimeRegisterLimitExceeded",
                    "Material program exceeds the bounded SSA register limit."));
                valid = false;
            }
            std::unordered_set<std::string> instructionNodeIds{};
            std::unordered_set<uint32_t> producedRegisters{};
            for (const MaterialProgramInstruction& instruction :
                program.instructions) {
                if (instruction.sourceNodeId.empty() ||
                    !instructionNodeIds.insert(
                        instruction.sourceNodeId).second ||
                    instruction.inputPins.size() !=
                        instruction.inputRegisters.size() ||
                    instruction.outputPins.size() !=
                        instruction.outputRegisters.size() ||
                    static_cast<uint32_t>(instruction.opcode) >
                        static_cast<uint32_t>(
                            MaterialProgramOpcode::FilterWidth)) {
                    diagnostics.push_back(MakeRegistryDiagnostic(
                        MaterialCompileDiagnosticSeverity::Error,
                        "Material.RuntimeInstructionShapeInvalid",
                        "Material instructions require unique source nodes and matching typed pin/register arrays.",
                        instruction.sourceNodeId));
                    valid = false;
                    continue;
                }
                for (const uint32_t input : instruction.inputRegisters) {
                    if (input != kInvalidMaterialProgramRegister &&
                        !producedRegisters.contains(input)) {
                        diagnostics.push_back(MakeRegistryDiagnostic(
                            MaterialCompileDiagnosticSeverity::Error,
                            "Material.RuntimeInstructionOrderInvalid",
                            "Material instruction reads a register before it is produced.",
                            instruction.sourceNodeId));
                        valid = false;
                    }
                }
                for (const uint32_t output : instruction.outputRegisters) {
                    if (output >= program.registerCount ||
                        !producedRegisters.insert(output).second) {
                        diagnostics.push_back(MakeRegistryDiagnostic(
                            MaterialCompileDiagnosticSeverity::Error,
                            "Material.RuntimeInstructionRegisterInvalid",
                            "Material instruction output registers must be unique and in range.",
                            instruction.sourceNodeId));
                        valid = false;
                    }
                }
                if (instruction.opcode == MaterialProgramOpcode::Parameter &&
                    instruction.bindingIndex >= program.parameters.size()) {
                    diagnostics.push_back(MakeRegistryDiagnostic(
                        MaterialCompileDiagnosticSeverity::Error,
                        "Material.RuntimeParameterInstructionInvalid",
                        "Parameter instruction references an invalid parameter binding.",
                        instruction.sourceNodeId));
                    valid = false;
                }
                if (instruction.opcode == MaterialProgramOpcode::TextureSample &&
                    instruction.bindingIndex >= program.resources.size()) {
                    diagnostics.push_back(MakeRegistryDiagnostic(
                        MaterialCompileDiagnosticSeverity::Error,
                        "Material.RuntimeTextureInstructionInvalid",
                        "TextureSample instruction references an invalid resource binding.",
                        instruction.sourceNodeId));
                    valid = false;
                }
            }
            if (program.sourceKind == MaterialAssetKind::Material &&
                (program.instructions.empty() ||
                 program.surfaceOutputRegister ==
                    kInvalidMaterialProgramRegister ||
                 !producedRegisters.contains(
                    program.surfaceOutputRegister))) {
                diagnostics.push_back(MakeRegistryDiagnostic(
                    MaterialCompileDiagnosticSeverity::Error,
                    "Material.RuntimeSurfaceOutputInvalid",
                    "Surface MaterialProgram requires a compiled SSA output register."));
                valid = false;
            }
            MaterialExpressionRuntimeProgram expression{};
            std::string expressionError{};
            if (!BuildMaterialExpressionRuntimeProgram(
                    program, expression, expressionError)) {
                diagnostics.push_back(MakeRegistryDiagnostic(
                    MaterialCompileDiagnosticSeverity::Error,
                    "Material.RuntimeExpressionLimitExceeded",
                    expressionError));
                valid = false;
            }
            return valid;
        }
    }

    MaterialProgramRegistry::MaterialProgramRegistry() {
        Reset();
    }

    void MaterialProgramRegistry::Reset() {
        programs_.clear();
        programByKey_.clear();
        diagnosticsBySource_.clear();
        stats_ = {};

        MaterialProgramIR fallback = BuildStandardPbrMaterialProgramFallback();
        programs_.push_back(BuildRuntimeMaterialProgram(
            kStandardPbrMaterialProgramId,
            fallback,
            true));
        programByKey_.emplace(
            programs_.front().ir.key,
            kStandardPbrMaterialProgramId);
        RefreshStats();
    }

    MaterialProgramResolution MaterialProgramRegistry::ResolveMaterialAsset(
        std::string_view sourceIdentity,
        const MaterialAssetData& source) {
        const MaterialCompileResult compileResult =
            CompileMaterialAsset(source);
        return ResolveCompileResult(sourceIdentity, compileResult);
    }

    MaterialProgramResolution MaterialProgramRegistry::ResolveCompileResult(
        std::string_view sourceIdentity,
        const MaterialCompileResult& compileResult) {
        ++stats_.compileResolveCount;
        std::vector<MaterialCompileDiagnostic> diagnostics =
            compileResult.diagnostics;
        if (compileResult.usedFallback) {
            return ResolveFailure(sourceIdentity, std::move(diagnostics));
        }
        if (!compileResult.success ||
            !IsRuntimeProgramShapeValid(compileResult.program, diagnostics)) {
            return ResolveFailure(sourceIdentity, std::move(diagnostics));
        }

        const InternResult interned = Intern(compileResult.program);
        if (interned.keyCollision || !interned.id) {
            diagnostics.push_back(MakeRegistryDiagnostic(
                MaterialCompileDiagnosticSeverity::Error,
                "Material.RuntimeProgramKeyCollision",
                "A material program key resolved to a different runtime layout."));
            ++stats_.keyCollisionCount;
            return ResolveFailure(sourceIdentity, std::move(diagnostics));
        }

        StoreDiagnostics(sourceIdentity, diagnostics);
        if (interned.created) {
            ++stats_.compiledProgramCount;
        } else {
            ++stats_.reusedProgramCount;
        }
        RefreshStats();
        return MaterialProgramResolution{
            interned.id,
            interned.created
                ? MaterialProgramResolutionKind::Compiled
                : MaterialProgramResolutionKind::Reused,
            compileResult.program,
            std::move(diagnostics)
        };
    }

    const RuntimeMaterialProgram* MaterialProgramRegistry::Find(
        MaterialProgramId id) const noexcept {
        if (!id || id.value >= programs_.size()) {
            return nullptr;
        }
        return &programs_[id.value];
    }

    const RuntimeMaterialProgram&
    MaterialProgramRegistry::GetStandardPbrFallback() const noexcept {
        return programs_.front();
    }

    std::span<const MaterialCompileDiagnostic>
    MaterialProgramRegistry::GetDiagnostics(
        std::string_view sourceIdentity) const noexcept {
        const auto found = diagnosticsBySource_.find(std::string{ sourceIdentity });
        if (found == diagnosticsBySource_.end()) {
            return {};
        }
        return { found->second.data(), found->second.size() };
    }

    const MaterialProgramRegistryStats&
    MaterialProgramRegistry::GetStats() const noexcept {
        return stats_;
    }

    MaterialProgramRegistry::InternResult MaterialProgramRegistry::Intern(
        const MaterialProgramIR& program) {
        const auto found = programByKey_.find(program.key);
        if (found != programByKey_.end()) {
            const RuntimeMaterialProgram* resident = Find(found->second);
            return InternResult{
                found->second,
                false,
                resident == nullptr ||
                    !IsMaterialProgramLayoutCompatible(resident->ir, program)
            };
        }
        if (programs_.size() >=
            static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return {};
        }

        const MaterialProgramId id{
            static_cast<uint32_t>(programs_.size())
        };
        programs_.push_back(BuildRuntimeMaterialProgram(id, program));
        programByKey_.emplace(program.key, id);
        return InternResult{ id, true, false };
    }

    MaterialProgramResolution MaterialProgramRegistry::ResolveFailure(
        std::string_view sourceIdentity,
        std::vector<MaterialCompileDiagnostic> diagnostics) {
        const RuntimeMaterialProgram& fallback = GetStandardPbrFallback();
        StoreDiagnostics(sourceIdentity, diagnostics);
        ++stats_.fallbackResolveCount;
        RefreshStats();
        return MaterialProgramResolution{
            fallback.id,
            MaterialProgramResolutionKind::StandardPbrFallback,
            fallback.ir,
            std::move(diagnostics)
        };
    }

    void MaterialProgramRegistry::StoreDiagnostics(
        std::string_view sourceIdentity,
        const std::vector<MaterialCompileDiagnostic>& diagnostics) {
        if (sourceIdentity.empty()) {
            return;
        }
        diagnosticsBySource_[std::string{ sourceIdentity }] = diagnostics;
    }

    void MaterialProgramRegistry::RefreshStats() {
        stats_.programCount = static_cast<uint32_t>(programs_.size());
        stats_.sourceCount = static_cast<uint32_t>(diagnosticsBySource_.size());
    }

    std::string_view ToString(MaterialProgramResolutionKind kind) noexcept {
        switch (kind) {
        case MaterialProgramResolutionKind::Compiled: return "Compiled";
        case MaterialProgramResolutionKind::Reused: return "Reused";
        case MaterialProgramResolutionKind::StandardPbrFallback: return "StandardPBRFallback";
        default: return "Unknown";
        }
    }

    MaterialProgramRegistry& GetRuntimeMaterialProgramRegistry() {
        return gRuntimeMaterialProgramRegistry;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
