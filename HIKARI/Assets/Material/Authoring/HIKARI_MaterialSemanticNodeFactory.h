#pragma once

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::ASSETS::MATERIAL {

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
