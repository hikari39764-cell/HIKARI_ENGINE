#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

ProceduralMeshComponentSettings NormalizeProceduralMeshSettings(ProceduralMeshComponentSettings settings) {
    static_cast<ProceduralMeshSettings&>(settings) = SanitizeProceduralMeshSettings(settings);
    return settings;
}

bool DrawProceduralMaterialSelection(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field,
                                     std::string& value, const ProceduralMeshComponentSettings&) {
    std::vector<std::string> labels{TEXT::Localize("ui.inspector.procedural.materialNone"),
                                    TEXT::Localize("ui.inspector.procedural.materialChecker")};
    std::vector<std::string> ids{std::string{}, std::string(ASSETS::MATERIAL::kBuiltInCheckerMaterialAssetId)};
    if (const AssetDatabase* assetDatabase = builder.GetContext().assetDatabase) {
        for (const AssetRecord* record : assetDatabase->CollectByType(AssetType::Material)) {
            if (record == nullptr || !record->guid.IsValid()) {
                continue;
            }
            ids.push_back(record->guid.value);
            labels.push_back(record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName);
        }
    }

    int selected = 0;
    for (size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == value) {
            selected = static_cast<int>(index);
            break;
        }
    }
    if (selected == 0 && !value.empty()) {
        ids.push_back(value);
        labels.push_back(TEXT::Localize("ui.inspector.procedural.materialMissing"));
        selected = static_cast<int>(ids.size() - 1u);
    }

    std::vector<AuthoringChoice> choices{};
    choices.reserve(labels.size());
    for (size_t index = 0u; index < labels.size(); ++index) {
        choices.push_back({ids[index], labels[index]});
    }
    if (!builder.Choice(field, selected, choices) || selected < 0 || selected >= static_cast<int>(ids.size())) {
        return false;
    }
    value = ids[static_cast<size_t>(selected)];
    return true;
}

void ProceduralMeshComponent::Serialize(nlohmann::json& out) const {
    // 丸い切り抜きは丸めを 1 回かけただけでは値が落ち着かない（2 回目で固定点になる）ので、保存は丸め直した値を書く。
    const ProceduralMeshComponentSettings settings = NormalizeProceduralMeshSettings(settings_);
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings, kProceduralMeshFields);
    out["cuts"] = WriteProceduralMeshCuts(settings.cuts);
}

void ProceduralMeshComponent::Deserialize(const nlohmann::json& in) {
    ProceduralMeshComponentSettings decoded{};
    JSONFIELDS::Read(in, decoded, kProceduralMeshFields);
    if (const nlohmann::json* cuts = JSONREAD::FindMember(in, "cuts"); cuts != nullptr && !cuts->is_null()) {
        if (!cuts->is_array()) {
            throw std::runtime_error("cuts: expects an array");
        }
        decoded.cuts = ReadProceduralMeshCuts(*cuts);
    }
    settings_ = NormalizeProceduralMeshSettings(std::move(decoded));
    NotifyRenderStateDirty();
}

#if defined(HIKARI_WITH_EDITOR)
void ProceduralMeshComponent::BuildInspector(IInspectorBuilder& builder) {
    ProceduralMeshComponentSettings next = settings_;
    if (InspectFields(builder, next, kProceduralMeshFields)) {
        settings_ = NormalizeProceduralMeshSettings(std::move(next));
        NotifyRenderStateDirty();
    }
}
#endif

const ProceduralMeshSettings& ProceduralMeshComponent::GetSettings() const noexcept { return settings_; }

void ProceduralMeshComponent::SetSettings(const ProceduralMeshSettings& settings) {
    const ProceduralMeshSettings sanitized = SanitizeProceduralMeshSettings(settings);
    if (static_cast<const ProceduralMeshSettings&>(settings_) == sanitized) {
        return;
    }
    static_cast<ProceduralMeshSettings&>(settings_) = sanitized;
    NotifyRenderStateDirty();
}

const std::string& ProceduralMeshComponent::GetMaterialSelection() const noexcept {
    return settings_.materialSelection;
}

void ProceduralMeshComponent::SetMaterialSelection(std::string selection) {
    if (settings_.materialSelection == selection) {
        return;
    }
    settings_.materialSelection = std::move(selection);
    NotifyRenderStateDirty();
}

int ProceduralMeshComponent::GetGeometryFitPriority() const noexcept {
    // Shape-aware procedural fitting is more precise than the generated
    // render model's aggregate AABB.
    return 100;
}

bool ProceduralMeshComponent::QueryGeometryFit(GeometryFitDesc& outFit) const noexcept {
    const ProceduralMeshSettings settings = SanitizeProceduralMeshSettings(settings_);
    outFit = {};
    switch (settings.kind) {
    case ProceduralMeshKind::Plane:
    case ProceduralMeshKind::GridPlane:
        outFit.shape = GeometryFitShape::Box;
        outFit.size = {settings.width, 0.02f, settings.depth};
        outFit.radius = 0.01f;
        outFit.height = 0.02f;
        break;
    case ProceduralMeshKind::Ramp:
    case ProceduralMeshKind::Stairs:
        outFit.shape = GeometryFitShape::Box;
        FitProceduralSlopeBox(settings, outFit.center, outFit.rotationEulerDegrees, outFit.size);
        outFit.radius = outFit.size.y * 0.5f;
        outFit.height = outFit.size.y;
        break;
    case ProceduralMeshKind::Sphere:
        outFit.shape = GeometryFitShape::Sphere;
        outFit.size = {settings.width, settings.width, settings.width};
        outFit.radius = settings.width * 0.5f;
        outFit.height = settings.width;
        break;
    case ProceduralMeshKind::Cylinder:
    case ProceduralMeshKind::Capsule:
        // The current physics shape contract has no cylinder, so a
        // vertical capsule remains the deterministic approximation.
        outFit.shape = GeometryFitShape::Capsule;
        outFit.size = {settings.width, settings.height, settings.width};
        outFit.radius = settings.width * 0.5f;
        outFit.height = settings.height;
        break;
    case ProceduralMeshKind::Box:
    default:
        outFit.shape = GeometryFitShape::Box;
        outFit.size = {settings.width, settings.height, settings.depth};
        outFit.radius = (std::min)({settings.width, settings.height, settings.depth}) * 0.5f;
        outFit.height = settings.height;
        break;
    }
    return true;
}

void ProceduralMeshComponent::QueryGeometryFitParts(std::vector<GeometryFitDesc>& outParts) const {
    outParts.clear();
    const ProceduralMeshSettings settings = SanitizeProceduralMeshSettings(settings_);
    if (settings.kind != ProceduralMeshKind::Box || settings.cuts.empty()) {
        return;
    }
    for (const ProceduralMeshBox& box : DecomposeProceduralCutBox(settings)) {
        GeometryFitDesc part{};
        part.shape = GeometryFitShape::Box;
        part.center = box.center;
        part.size = box.size;
        part.radius = (std::min)({box.size.x, box.size.y, box.size.z}) * 0.5f;
        part.height = box.size.y;
        outParts.push_back(part);
    }
}

} // namespace HIKARI
