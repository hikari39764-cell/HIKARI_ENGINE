#pragma once

#include <span>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::ASSETS::MATERIAL {

    enum class MaterialNodeResourcePolicy : uint8_t {
        None,
        GeneratedDefault,
        OptionalAuthoredOverride,
        AuthoredRequired,
    };

    // This is the single authoring/runtime-facing explanation of a node. The
    // Editor renders these strings, while validation/compiler code consumes
    // the binding lists and resource policy below.
    struct MaterialNodeSemanticContract {
        std::string_view connectsTo{};
        std::string_view affectedBy{};
        std::string_view provides{};
        std::string_view defaultBehavior{};
        std::string_view failureBehavior{};
        MaterialNodeResourcePolicy resourcePolicy =
            MaterialNodeResourcePolicy::None;
        std::string_view primaryResourceId{};
    };

    const MaterialNodeSemanticContract& GetMaterialNodeSemanticContract(
        MaterialNodeKind kind) noexcept;

    // Canonical node-owned bindings. Parameter/Texture Sample nodes own the
    // binding named by node.referenceId instead.
    std::span<const std::string_view> GetMaterialNodeOwnedParameterIds(
        MaterialNodeKind kind) noexcept;
    std::span<const std::string_view> GetMaterialNodeOwnedResourceIds(
        MaterialNodeKind kind) noexcept;

    // Ready-made nodes are bounded, game-runtime material building blocks.
    // Their parameters and resources are canonical so template creation and
    // manual graph authoring compile to the same runtime contract.
    bool IsReadyMadeMaterialNode(MaterialNodeKind kind) noexcept;
    bool IsSingletonReadyMadeMaterialNode(MaterialNodeKind kind) noexcept;
    std::string_view ReadyMadeMaterialNodeDisplayName(
        MaterialNodeKind kind) noexcept;

    // Idempotently fills the node presentation and ensures every canonical
    // parameter/resource required by the node exists in the owning graph.
    void InitializeReadyMadeMaterialNode(
        MaterialAssetData& material,
        MaterialGraphNodeData& node);

} // namespace HIKARI::ASSETS::MATERIAL
