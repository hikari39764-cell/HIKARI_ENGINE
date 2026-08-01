#include "HIKARI_MaterialCompilerValidation.h"

#include <cmath>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "Assets/Material/Compiler/HIKARI_MaterialRampRasterizer.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {

    namespace {

        void AddDiagnostic(
            MaterialCompileResult& result,
            MaterialCompileDiagnosticSeverity severity,
            std::string code,
            std::string message,
            std::string subjectId = {}) {

            result.diagnostics.push_back(MaterialCompileDiagnostic{
                severity,
                std::move(code),
                std::move(message),
                std::move(subjectId)
            });
        }

        bool ValidateStableValueList(
            const std::vector<MaterialGraphParameterData>& values,
            std::string_view listName,
            MaterialCompileResult& result) {

            bool valid = true;
            std::unordered_set<std::string> ids{};
            for (const MaterialGraphParameterData& value : values) {
                if (!IsValidMaterialStableId(value.id) ||
                    !ids.emplace(value.id).second) {
                    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                        "Material.InvalidFunctionPort",
                        std::string(listName) + " IDs must be stable and unique",
                        value.id);
                    valid = false;
                }
            }
            return valid;
        }

        bool AppendGraphDiagnostics(
            const MaterialGraphData& graph,
            bool requireSurfaceOutput,
            MaterialCompileResult& result) {

            std::vector<MaterialGraphDiagnostic> graphDiagnostics{};
            const bool graphValid = ValidateMaterialGraph(
                graph,
                MaterialGraphValidationOptions{ requireSurfaceOutput },
                graphDiagnostics);
            for (const MaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
                MaterialCompileDiagnosticSeverity severity =
                    MaterialCompileDiagnosticSeverity::Error;
                if (diagnostic.severity == MaterialGraphDiagnosticSeverity::Info) {
                    severity = MaterialCompileDiagnosticSeverity::Info;
                } else if (diagnostic.severity == MaterialGraphDiagnosticSeverity::Warning) {
                    severity = MaterialCompileDiagnosticSeverity::Warning;
                }
                AddDiagnostic(result, severity, diagnostic.code,
                    diagnostic.message, diagnostic.subjectId);
            }
            return graphValid;
        }

    } // namespace

    bool ValidateMaterialSource(
        const MaterialAssetData& source,
        MaterialCompileResult& result) {

        bool valid = true;
        if (source.version != kMaterialAssetSourceVersion) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                "Material.UnsupportedVersion",
                "compiler accepts only canonical Material v4 source data");
            valid = false;
        }
        if (source.materialName.empty()) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning,
                "Material.EmptyName", "material has no display name");
        }
        if (!std::isfinite(source.surface.alphaCutoff) ||
            source.surface.alphaCutoff < 0.0f ||
            source.surface.alphaCutoff > 1.0f) {
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                "Material.InvalidAlphaCutoff",
                "alpha cutoff must be finite and between zero and one");
            valid = false;
        }

        switch (source.kind) {
        case MaterialAssetKind::Material:
            valid = AppendGraphDiagnostics(source.graph, true, result) && valid;
            break;
        case MaterialAssetKind::Function:
            valid = ValidateStableValueList(
                source.function.inputs, "function input", result) && valid;
            valid = ValidateStableValueList(
                source.function.outputs, "function output", result) && valid;
            valid = AppendGraphDiagnostics(
                source.function.graph, false, result) && valid;
            break;
        case MaterialAssetKind::Instance: {
            if (!source.instance.parentMaterialGuid.IsValid()) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                    "Material.MissingParent",
                    "material instance requires a parent Material GUID");
                valid = false;
            }
            std::unordered_set<std::string> parameterIds{};
            for (const auto& value : source.instance.parameterOverrides) {
                if (!IsValidMaterialStableId(value.parameterId) ||
                    !parameterIds.emplace(value.parameterId).second) {
                    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                        "Material.InvalidInstanceParameter",
                        "instance parameter overrides must have stable unique IDs",
                        value.parameterId);
                    valid = false;
                }
            }
            std::unordered_set<std::string> resourceIds{};
            for (const auto& value : source.instance.resourceOverrides) {
                if (!IsValidMaterialStableId(value.resourceId) ||
                    !resourceIds.emplace(value.resourceId).second) {
                    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                        "Material.InvalidInstanceResource",
                        "instance resource overrides must have stable unique IDs",
                        value.resourceId);
                    valid = false;
                }
            }
            break;
        }
        case MaterialAssetKind::Profile:
            if (source.profile.functionAssetGuids.empty() &&
                source.profile.rampAssetGuids.empty() &&
                source.profile.featureRequests.empty()) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning,
                    "Material.EmptyProfile",
                    "material profile contains no reusable functions, ramps, or features");
            }
            break;
        case MaterialAssetKind::Ramp: {
            const bool oneDimensional =
                source.ramp.dimension == MaterialRampDimension::OneD;
            const uint32_t maximumResolution = oneDimensional
                ? ASSETS::MATERIAL::kMaximumMaterialRamp1DResolution
                : ASSETS::MATERIAL::kMaximumMaterialRamp2DResolution;
            if (source.ramp.width < 2u ||
                source.ramp.width > maximumResolution ||
                source.ramp.height < 1u ||
                source.ramp.height > maximumResolution ||
                (source.ramp.dimension == MaterialRampDimension::OneD &&
                    source.ramp.height != 1u) ||
                (source.ramp.dimension == MaterialRampDimension::TwoD &&
                    source.ramp.height < 2u) ||
                source.ramp.points.size() < 2u ||
                source.ramp.points.size() > 256u) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                    "Material.InvalidRampShape",
                    "ramp dimensions and point count are outside the bounded v1 contract");
                valid = false;
            }
            std::unordered_set<std::string> pointIds{};
            for (const MaterialRampPointData& point : source.ramp.points) {
                if (!IsValidMaterialStableId(point.id) ||
                    !pointIds.emplace(point.id).second ||
                    !std::isfinite(point.position.x) ||
                    !std::isfinite(point.position.y) ||
                    point.position.x < 0.0f || point.position.x > 1.0f ||
                    point.position.y < 0.0f || point.position.y > 1.0f) {
                    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error,
                        "Material.InvalidRampPoint",
                        "ramp point IDs and normalized positions must be valid",
                        point.id);
                    valid = false;
                }
            }
            break;
        }
        }
        return valid;
    }

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
