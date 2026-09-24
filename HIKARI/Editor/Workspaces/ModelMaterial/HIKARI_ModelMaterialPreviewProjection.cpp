#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <cstdio>
#include <filesystem>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "Assets/Material/Authoring/HIKARI_MaterialGraphInspection.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

namespace HIKARI::EDITOR {
bool ModelMaterialWorkspaceController::IsRenderableMaterialAsset(MaterialAssetKind kind) noexcept {
    return kind == MaterialAssetKind::Material || kind == MaterialAssetKind::Instance;
}
bool ModelMaterialWorkspaceController::RebuildAuthoredPreview(AssetDatabase& assetDatabase,
                                                              const AssetRegistry& assetRegistry,
                                                              std::string& outMessage) {
    if (!authoringDocument_.HasMaterial()) {
        return false;
    }
    if (!nodePreviewId_.empty()) {
        const auto* graph = MATERIAL_GRAPH_UI::EditableGraph(authoringDocument_.EditMaterial());
        if (!graph || !FindMaterialGraphNode(*graph, nodePreviewId_))
            nodePreviewId_.clear();
    }
    if (nodePreviewId_.empty() && !authoringDocument_.GetCompileResult().success) {
        outMessage = "current Material v6 graph is invalid; preview is paused until diagnostics are fixed";
        return false;
    }
    if (nodePreviewId_.empty() && !IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
        outMessage = std::string(ToString(authoringDocument_.GetMaterial().kind)) +
                     " saved; auxiliary assets do not replace slot preview";
        return true;
    }
    MaterialAssetData effective = authoringDocument_.GetMaterial();
    MaterialCompileResult effectiveCompile = authoringDocument_.GetCompileResult();
    if (effective.kind == MaterialAssetKind::Instance) {
        ASSETS::MATERIAL::MaterialAssetAuthoringService service(assetDatabase);
        const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved = service.ResolveInstance(effective);
        if (resolved.success) {
            effective = resolved.resolvedMaterial;
            effectiveCompile = resolved.compile;
        } else {
            outMessage = "material instance resolution failed; last preview kept";
            return false;
        }
    }

    if (!nodePreviewId_.empty()) {
        MaterialAssetData inspected;
        std::string error;
        if (!ASSETS::MATERIAL::BuildMaterialNodePreview(effective, nodePreviewId_, nodePreviewPin_, nodePreviewChannel_,
                                                        inspected, error)) {
            outMessage = TEXT::Localize(error);
            return false;
        }
        effective = std::move(inspected);
        effectiveCompile = CompileMaterialAsset(effective, ASSETS::MATERIAL::MakeMaterialCompileOptions(assetDatabase));
        if (!effectiveCompile.success) {
            outMessage = TEXT::Localize("material.inspect.failed");
            for (const auto& diagnostic : effectiveCompile.diagnostics)
                outMessage += "\n" + diagnostic.message;
            return false;
        }
    }

    if (effective.domain == MaterialDomain::Particle) {
        standalonePreviewMaterial_.reset();
        outMessage = "Particle material graph compiled; use the VFX workspace preview "
                     "to evaluate particle UV, time, position, and view inputs";
        return true;
    }

    auto previewMaterial = std::make_shared<Material>();
    MaterialRuntimeBuilder builder{};
    RENDER3D::MATERIAL::MaterialProgramResolution resolution{};
    const std::string identity = authoringDocument_.GetMaterialAssetGuid().IsValid()
                                     ? authoringDocument_.GetMaterialAssetGuid().value
                                     : std::string("ModelMaterial/Unsaved");
    if (!builder.BuildRuntimeMaterial(effective, effectiveCompile, assetRegistry, materialProgramRegistry_,
                                      *previewMaterial, identity, &resolution)) {
        outMessage = "material preview projection failed; last preview kept";
        return false;
    }
    RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram previewExpression{};
    std::string previewExpressionError{};
    (void)RENDER3D::MATERIAL::BuildMaterialExpressionRuntimeProgram(effectiveCompile.program, previewExpression,
                                                                    previewExpressionError);
    const bool hasExpressionBlock = previewExpression.active && previewExpression.outputMask != 0u &&
                                    previewMaterial->GetMaterialProgramId().value != 0u;
    const auto maskText = [](uint32_t mask) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%08X", mask);
        return std::string(buffer);
    };
    materialPreviewExpressionStatus_ = hasExpressionBlock
                                           ? "Expression active mask=0x" + maskText(previewExpression.outputMask)
                                           : "Expression inactive";
    previewMaterial->SetInheritModelTextures(authoringMode_ == AuthoringMode::ModelMaterials);
    if (authoringMode_ == AuthoringMode::MaterialAsset) {
        if (!materialSphereScene_.IsReady() ||
            !materialSphereScene_.SetPreviewMaterialOverride(0u, previewMaterial.get())) {
            outMessage = "material sphere rejected the authored material";
            return false;
        }
        standalonePreviewMaterial_ = std::move(previewMaterial);
        outMessage = std::string(resolution.UsedFallback() ? "material sphere uses the Standard PBR fallback"
                                                           : "standalone material preview updated") +
                     (hasExpressionBlock ? " | expression active mask=0x" + maskText(previewExpression.outputMask)
                                         : " | expression inactive");
        return true;
    }

    const ModelMaterialSlotView* selected = GetSelectedSlot();
    if (selected == nullptr) {
        outMessage = "select a model part to preview this material";
        return false;
    }
    SlotState* slot = FindSlot(selected->slotId);
    if (slot == nullptr) {
        outMessage = "selected model material slot is no longer available";
        return false;
    }
    if (!previewScene_.SetPreviewMaterialOverride(slot->view.materialIndex, previewMaterial.get())) {
        outMessage = "preview rejected the authored material slot";
        return false;
    }
    slot->previewMaterial = std::move(previewMaterial);
    const bool savedAsset = authoringDocument_.GetMaterialAssetGuid().IsValid();
    slot->view.source =
        savedAsset ? ModelMaterialBindingSource::AuthoredOverride : ModelMaterialBindingSource::ImportedFallback;
    slot->view.diagnostic = savedAsset ? std::string{} : "unsaved editable copy of imported material";
    UpdatePublicSlotViews();
    RefreshSelectedSlotPresentation();
    outMessage = std::string(resolution.UsedFallback() ? "preview uses the Standard PBR fallback"
                                                       : "authored material preview updated") +
                 (hasExpressionBlock ? " | expression active mask=0x" + maskText(previewExpression.outputMask)
                                     : " | expression inactive");
    return true;
}

bool ModelMaterialWorkspaceController::RefreshMaterialPreviews(DocumentSceneBase& scene, std::string& outMessage) {
    (void)authoringDocument_.ConsumeMaterialPreviewChange();
    nodePreviewRefreshPending_ = false;
    if (authoringDocument_.HasMaterial() && !authoringDocument_.GetCompileResult().success && nodePreviewId_.empty()) {
        outMessage = "current Material v6 graph is invalid; preview and runtime hot update are paused";
        return false;
    }
    const bool authoredPreviewUpdated =
        RebuildAuthoredPreview(scene.GetAssetDatabase(), scene.GetAssetRegistry(), outMessage);
    if (!authoringDocument_.HasMaterial() || !authoringDocument_.GetCompileResult().success ||
        !authoringDocument_.GetMaterialAssetGuid().IsValid() ||
        !IsRenderableMaterialAsset(authoringDocument_.GetMaterial().kind)) {
        return authoredPreviewUpdated;
    }

    const int runtimeBindingCount = scene.ApplyRuntimeMaterialOverridePreview(authoringDocument_.GetMaterialAssetGuid(),
                                                                              authoringDocument_.GetMaterial());
    if (runtimeBindingCount > 0) {
        outMessage += "; runtime hot preview updated " + std::to_string(runtimeBindingCount) + " binding(s)";
    }
    return authoredPreviewUpdated;
}
} // namespace HIKARI::EDITOR
