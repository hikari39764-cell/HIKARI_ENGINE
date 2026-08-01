#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/Material/Graph/HIKARI_MaterialGraphData.h"

namespace HIKARI {

    enum class MaterialGraphDiagnosticSeverity : uint8_t {
        Info,
        Warning,
        Error,
    };

    struct MaterialGraphDiagnostic {
        MaterialGraphDiagnosticSeverity severity =
            MaterialGraphDiagnosticSeverity::Error;
        std::string code{};
        std::string message{};
        std::string subjectId{};
    };

    struct MaterialGraphValidationOptions {
        bool requireSurfaceOutput = true;
    };

    bool IsValidMaterialStableId(std::string_view id) noexcept;

    bool ValidateMaterialGraph(
        const MaterialGraphData& graph,
        const MaterialGraphValidationOptions& options,
        std::vector<MaterialGraphDiagnostic>& outDiagnostics);

} // namespace HIKARI
