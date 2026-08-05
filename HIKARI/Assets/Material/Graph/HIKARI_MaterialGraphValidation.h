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

    // Compatibility views over the canonical material node definitions.
    // Validation, lowering and Editor presentation consume one socket schema.
    std::vector<std::string_view> GetMaterialGraphNodeInputPins(
        MaterialNodeKind kind);
    std::vector<std::string_view> GetMaterialGraphNodeInputPins(
        const MaterialNodeTypeId& typeId);
    std::vector<std::string_view> GetMaterialGraphNodeInputPins(
        const MaterialGraphNodeData& node);
    std::vector<std::string_view> GetMaterialGraphNodeOutputPins(
        MaterialNodeKind kind);
    std::vector<std::string_view> GetMaterialGraphNodeOutputPins(
        const MaterialNodeTypeId& typeId);
    std::vector<std::string_view> GetMaterialGraphNodeOutputPins(
        const MaterialGraphNodeData& node);

    // Runtime-generated shading context sockets are valid compiler inputs but
    // are not authorable graph sockets. Resource inputs such as FaceSdf.SDF
    // deliberately return false and remain visible in the Editor.
    bool IsMaterialGraphRuntimeGeneratedInput(
        MaterialNodeKind kind,
        std::string_view pin) noexcept;
    bool IsMaterialGraphRuntimeGeneratedInput(
        const MaterialNodeTypeId& typeId,
        std::string_view pin) noexcept;

    bool ValidateMaterialGraph(
        const MaterialGraphData& graph,
        const MaterialGraphValidationOptions& options,
        std::vector<MaterialGraphDiagnostic>& outDiagnostics);

    // Validates one editor connection independently from unrelated compile
    // errors in the graph. Existing input ownership is treated as replaceable.
    bool CanConnectMaterialGraphPins(
        const MaterialGraphData& graph,
        std::string_view sourceNodeId,
        std::string_view sourcePin,
        std::string_view targetNodeId,
        std::string_view targetPin,
        std::string& outReason);

} // namespace HIKARI
