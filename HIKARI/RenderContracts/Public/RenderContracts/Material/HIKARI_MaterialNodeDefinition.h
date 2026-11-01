#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "RenderContracts/Material/HIKARI_MaterialGraphData.h"

namespace HIKARI {

enum class MaterialNodeSemanticClass : uint8_t {
    Input,
    Math,
    Surface,
    Shading,
    Function,
};

enum class MaterialNodePinTypeSource : uint8_t {
    Fixed,
    NodeValue,
    ImmediateValue,
    ReferencedParameter,
};

enum class MaterialNodeOutputRoute : uint8_t {
    Unrestricted,
    DiffuseBase,
    DiffuseAfterSkin,
    DiffuseFinal,
    Specular,
    Eye,
    Additive,
    Outline,
};

struct MaterialNodePinDefinition {
    std::string_view id{};
    MaterialValueType fixedType = MaterialValueType::Float1;
    MaterialNodePinTypeSource typeSource = MaterialNodePinTypeSource::Fixed;
    bool acceptsDynamicNumeric = false;
    bool runtimeGenerated = false;
    std::string_view runtimeResourceChannel{};
};

struct MaterialNodeDefinition {
    MaterialNodeKind kind = MaterialNodeKind::Constant;
    std::string_view stableName{};
    MaterialValueType defaultValueType = MaterialValueType::Float1;
    MaterialNodeSemanticClass semanticClass = MaterialNodeSemanticClass::Math;
    bool paletteVisible = true;
    std::span<const MaterialNodePinDefinition> inputs{};
    std::span<const MaterialNodePinDefinition> outputs{};
    bool staticallyEvaluable = false;
    MaterialNodeOutputRoute outputRoute = MaterialNodeOutputRoute::Unrestricted;
    // One-line semantic contract for AI and node catalogs. Palette-visible
    // nodes must provide a non-empty summary.
    std::string_view summary{};
};

std::span<const MaterialNodeDefinition> GetMaterialNodeDefinitions() noexcept;

const MaterialNodeDefinition* FindMaterialNodeDefinition(MaterialNodeKind kind) noexcept;
const MaterialNodeDefinition* FindMaterialNodeDefinition(const MaterialNodeTypeId& typeId) noexcept;
const MaterialNodeDefinition* FindMaterialNodeDefinition(std::string_view stableName) noexcept;

bool IsMaterialNodeAvailableForDomain(MaterialNodeKind kind, MaterialDomain domain) noexcept;
bool IsMaterialNodeAvailableForDomain(const MaterialNodeTypeId& typeId, MaterialDomain domain) noexcept;

const MaterialNodePinDefinition* FindMaterialNodeInputDefinition(MaterialNodeKind kind, std::string_view pin) noexcept;
const MaterialNodePinDefinition* FindMaterialNodeInputDefinition(const MaterialNodeTypeId& typeId,
                                                                 std::string_view pin) noexcept;
const MaterialNodePinDefinition* FindMaterialNodeOutputDefinition(MaterialNodeKind kind, std::string_view pin) noexcept;
const MaterialNodePinDefinition* FindMaterialNodeOutputDefinition(const MaterialNodeTypeId& typeId,
                                                                  std::string_view pin) noexcept;

MaterialValueType ResolveMaterialNodePinType(const MaterialNodePinDefinition& pin, const MaterialGraphNodeData& node,
                                             std::optional<MaterialValueType> referencedParameterType = {}) noexcept;

} // namespace HIKARI
