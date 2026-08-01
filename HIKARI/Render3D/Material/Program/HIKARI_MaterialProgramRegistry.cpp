#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

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
            return valid;
        }
    }

    MaterialProgramRegistry::MaterialProgramRegistry() {
        Reset();
    }

    void MaterialProgramRegistry::Reset() {
        programs_.clear();
        programByKey_.clear();
        lastGoodBySource_.clear();
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
        const MaterialProgramId lastGoodId = FindLastGood(sourceIdentity);
        const RuntimeMaterialProgram* lastGood = Find(lastGoodId);
        const MaterialCompileResult compileResult = CompileMaterialAsset(
            source,
            lastGood != nullptr ? &lastGood->ir : nullptr);
        return ResolveCompileResult(sourceIdentity, compileResult);
    }

    MaterialProgramResolution MaterialProgramRegistry::ResolveCompileResult(
        std::string_view sourceIdentity,
        const MaterialCompileResult& compileResult) {
        ++stats_.compileResolveCount;
        std::vector<MaterialCompileDiagnostic> diagnostics =
            compileResult.diagnostics;
        const MaterialProgramId currentLastGood = FindLastGood(sourceIdentity);

        if (compileResult.usedLastGood && currentLastGood) {
            const RuntimeMaterialProgram* program = Find(currentLastGood);
            if (program != nullptr) {
                StoreDiagnostics(sourceIdentity, diagnostics);
                ++stats_.lastGoodResolveCount;
                RefreshStats();
                return MaterialProgramResolution{
                    currentLastGood,
                    MaterialProgramResolutionKind::LastGood,
                    program->ir,
                    std::move(diagnostics)
                };
            }
        }
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

        if (!sourceIdentity.empty()) {
            lastGoodBySource_[std::string{ sourceIdentity }] = interned.id;
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

    MaterialProgramId MaterialProgramRegistry::FindLastGood(
        std::string_view sourceIdentity) const noexcept {
        if (sourceIdentity.empty()) {
            return {};
        }
        const auto found = lastGoodBySource_.find(std::string{ sourceIdentity });
        return found != lastGoodBySource_.end()
            ? found->second
            : kInvalidMaterialProgramId;
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
        const MaterialProgramId lastGoodId = FindLastGood(sourceIdentity);
        const RuntimeMaterialProgram* lastGood = Find(lastGoodId);
        if (lastGood != nullptr) {
            StoreDiagnostics(sourceIdentity, diagnostics);
            ++stats_.lastGoodResolveCount;
            RefreshStats();
            return MaterialProgramResolution{
                lastGoodId,
                MaterialProgramResolutionKind::LastGood,
                lastGood->ir,
                std::move(diagnostics)
            };
        }

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
        stats_.lastGoodSourceCount =
            static_cast<uint32_t>(lastGoodBySource_.size());
    }

    std::string_view ToString(MaterialProgramResolutionKind kind) noexcept {
        switch (kind) {
        case MaterialProgramResolutionKind::Compiled: return "Compiled";
        case MaterialProgramResolutionKind::Reused: return "Reused";
        case MaterialProgramResolutionKind::LastGood: return "LastGood";
        case MaterialProgramResolutionKind::StandardPbrFallback: return "StandardPBRFallback";
        default: return "Unknown";
        }
    }

    MaterialProgramRegistry& GetRuntimeMaterialProgramRegistry() {
        return gRuntimeMaterialProgramRegistry;
    }

    void ResetRuntimeMaterialProgramRegistry() {
        gRuntimeMaterialProgramRegistry.Reset();
    }

} // namespace HIKARI::RENDER3D::MATERIAL
