// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

namespace {
uint32_t DrawSegmentCount(IInspectorBuilder& builder, std::string_view id, std::string_view label, uint32_t value,
                          int minimum, int maximum) {
    int edited = static_cast<int>(value);
    if (builder.Int(AuthoringFieldDescriptor{id, label}, edited)) {
        return static_cast<uint32_t>(std::clamp(edited, minimum, maximum));
    }
    return value;
}

void DrawDimension(IInspectorBuilder& builder, std::string_view id, std::string_view label, float& value) {
    (void)builder.FloatRange(AuthoringFieldDescriptor{id, label}, value, 0.001f, 100000.0f, 0.05f);
}
} // namespace

void ProceduralMeshComponent::BuildInspector(IInspectorBuilder& builder) {
    const ProceduralMeshSettings before = settings_;
    std::array<std::string_view, std::size(kProceduralMeshKindNames)> kindLabels{};
    for (size_t index = 0; index < kindLabels.size(); ++index) {
        kindLabels[index] = GetProceduralMeshDisplayName(kProceduralMeshKindNames[index].value);
    }
    (void)EnumChoice(builder, AuthoringFieldDescriptor{"kind", "Shape"}, settings_.kind, kProceduralMeshKindNames,
                     &kindLabels);

    switch (settings_.kind) {
    case ProceduralMeshKind::Plane:
        DrawDimension(builder, "width", "Width", settings_.width);
        DrawDimension(builder, "depth", "Depth", settings_.depth);
        break;
    case ProceduralMeshKind::GridPlane:
        DrawDimension(builder, "width", "Width", settings_.width);
        DrawDimension(builder, "depth", "Depth", settings_.depth);
        settings_.segmentsX = DrawSegmentCount(builder, "segmentsX", "Columns", settings_.segmentsX, 1, 512);
        settings_.segmentsY = DrawSegmentCount(builder, "segmentsY", "Rows", settings_.segmentsY, 1, 512);
        break;
    case ProceduralMeshKind::Box:
        DrawDimension(builder, "width", "Width", settings_.width);
        DrawDimension(builder, "height", "Height", settings_.height);
        DrawDimension(builder, "depth", "Depth", settings_.depth);
        break;
    case ProceduralMeshKind::Sphere:
        DrawDimension(builder, "width", "Diameter", settings_.width);
        settings_.sphereSlices =
            DrawSegmentCount(builder, "sphereSlices", "Radial Segments", settings_.sphereSlices, 3, 512);
        settings_.sphereStacks =
            DrawSegmentCount(builder, "sphereStacks", "Vertical Segments", settings_.sphereStacks, 2, 256);
        break;
    case ProceduralMeshKind::Cylinder:
        DrawDimension(builder, "width", "Diameter", settings_.width);
        DrawDimension(builder, "height", "Height", settings_.height);
        settings_.sphereSlices =
            DrawSegmentCount(builder, "sphereSlices", "Radial Segments", settings_.sphereSlices, 3, 512);
        settings_.segmentsY = DrawSegmentCount(builder, "segmentsY", "Height Segments", settings_.segmentsY, 1, 512);
        break;
    case ProceduralMeshKind::Ramp:
        DrawDimension(builder, "width", "Width", settings_.width);
        DrawDimension(builder, "height", "Height", settings_.height);
        DrawDimension(builder, "depth", "Depth", settings_.depth);
        break;
    case ProceduralMeshKind::Stairs:
        DrawDimension(builder, "width", "Width", settings_.width);
        DrawDimension(builder, "height", "Height", settings_.height);
        DrawDimension(builder, "depth", "Depth", settings_.depth);
        settings_.segmentsY = DrawSegmentCount(builder, "segmentsY", "Steps", settings_.segmentsY, 1, 64);
        break;
    case ProceduralMeshKind::Capsule:
        DrawDimension(builder, "width", "Diameter", settings_.width);
        DrawDimension(builder, "height", "Total Height", settings_.height);
        settings_.sphereSlices =
            DrawSegmentCount(builder, "sphereSlices", "Radial Segments", settings_.sphereSlices, 3, 512);
        settings_.sphereStacks =
            DrawSegmentCount(builder, "sphereStacks", "Hemisphere Segments", settings_.sphereStacks, 2, 256);
        break;
    }

    builder.Bool(AuthoringFieldDescriptor{"doubleSided", "Double Sided"}, settings_.doubleSided);
    builder.Bool(AuthoringFieldDescriptor{"generateTangents", "Generate Tangents"}, settings_.generateTangents);

    // マテリアル選択: None(デフォルト白) / Checker(デバッグ) /
    // プロジェクト内のマテリアルアセット。
    std::vector<std::string> materialLabels{TEXT::Localize("ui.inspector.procedural.materialNone"),
                                            TEXT::Localize("ui.inspector.procedural.materialChecker")};
    std::vector<std::string> materialIds{std::string{}, std::string(ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId)};
    const InspectorContext& context = builder.GetContext();
    if (const AssetDatabase* assetDatabase = context.assetDatabase) {
        for (const AssetRecord* record : assetDatabase->CollectByType(AssetType::Material)) {
            if (record == nullptr || !record->guid.IsValid()) {
                continue;
            }
            materialIds.push_back(record->guid.value);
            materialLabels.push_back(record->displayName.empty() ? record->sourcePath.stem().string()
                                                                 : record->displayName);
        }
    }

    int selectedMaterial = 0;
    for (size_t index = 0; index < materialIds.size(); ++index) {
        if (materialIds[index] == materialSelection_) {
            selectedMaterial = static_cast<int>(index);
            break;
        }
    }
    if (selectedMaterial == 0 && !materialSelection_.empty()) {
        materialIds.push_back(materialSelection_);
        materialLabels.push_back(TEXT::Localize("ui.inspector.procedural.materialMissing"));
        selectedMaterial = static_cast<int>(materialIds.size() - 1u);
    }

    std::vector<AuthoringChoice> materialChoices{};
    materialChoices.reserve(materialLabels.size());
    for (size_t index = 0u; index < materialLabels.size(); ++index) {
        materialChoices.push_back({materialIds[index], materialLabels[index]});
    }
    if (builder.Choice(AuthoringFieldDescriptor{"materialSelection", TEXT::Localize("ui.inspector.procedural.material"),
                                                "Material asset GUID assigned to the procedural surface; "
                                                "empty selects the default material."},
                       selectedMaterial, materialChoices)) {
        if (selectedMaterial >= 0 && selectedMaterial < static_cast<int>(materialIds.size())) {
            SetMaterialSelection(materialIds[selectedMaterial]);
        }
    }
    settings_ = SanitizeProceduralMeshSettings(settings_);
    if (!(settings_ == before)) {
        NotifyRenderStateDirty();
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
