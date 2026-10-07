#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/HIKARI_MaterialAssetTypes.h"
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {

struct MaterialValueData {
    MaterialValueType type = MaterialValueType::Float1;
    MATH::Vec4 numeric{0.0f, 0.0f, 0.0f, 0.0f};
    bool boolean = false;
};

struct MaterialGraphParameterData {
    std::string id{};
    std::string displayName{};
    MaterialValueData defaultValue{};
    bool exposed = true;
    std::string group{};
    bool useUiRange = false;
    float uiMinimum = 0.0f;
    float uiMaximum = 1.0f;
};

struct MaterialGraphResourceData {
    std::string id{};
    std::string displayName{};
    MaterialResourceType type = MaterialResourceType::Texture2D;
    MaterialResourceSemantic semantic = MaterialResourceSemantic::Custom;
    AssetGuid assetGuid{};
    bool enabled = false;
    uint32_t texCoord = 0u;
    MATH::Vec2 uvScale{1.0f, 1.0f};
    MATH::Vec2 uvOffset{0.0f, 0.0f};
    float uvRotation = 0.0f;
};

struct MaterialNodePropertyData {
    std::string id{};
    MaterialValueData value{};
};

struct MaterialGraphNodeData {
    std::string id{};
    MaterialNodeTypeId kind{MaterialNodeKind::Constant};
    std::string displayName{};
    MATH::Vec2 position{0.0f, 0.0f};
    MaterialValueType valueType = MaterialValueType::Float1;
    MaterialValueData value{};
    std::string referenceId{};
    AssetGuid assetReference{};
    std::vector<MaterialNodePropertyData> properties{};
    // Material Function calls cache their authored interface so the graph
    // remains editable while its dependency is temporarily unavailable.
    // Cook always resolves the authoritative Function asset before use.
    std::vector<MaterialGraphParameterData> functionInputs{};
    std::vector<MaterialGraphParameterData> functionOutputs{};
};

struct MaterialGraphEdgeData {
    std::string id{};
    std::string sourceNodeId{};
    std::string sourcePin{};
    std::string targetNodeId{};
    std::string targetPin{};
};

struct MaterialGraphGroupData {
    std::string id{};
    std::string name{};
    MATH::Vec2 position{};
    MATH::Vec2 size{220.0f, 160.0f};
    uint32_t color = 0x55334455u;
    std::vector<std::string> nodeIds{};
};

struct MaterialGraphData {
    std::string outputNodeId{};
    std::vector<MaterialGraphParameterData> parameters{};
    std::vector<MaterialGraphResourceData> resources{};
    std::vector<MaterialGraphNodeData> nodes{};
    std::vector<MaterialGraphEdgeData> edges{};
    std::vector<MaterialGraphGroupData> groups{};
};

} // namespace HIKARI
