#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include "Assets/Material/Compiler/HIKARI_MaterialRampRasterizer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

ImU32 RampColor(const MATH::Vec4& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w));
}

MaterialRampPointData* FindPoint(MaterialRampData& ramp, const std::string& id) {
    const auto found =
        std::find_if(ramp.points.begin(), ramp.points.end(), [&id](const auto& point) { return point.id == id; });
    return found != ramp.points.end() ? &*found : nullptr;
}

} // namespace

void ModelMaterialWorkspaceController::DrawRampWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.terrain.toolRamp", "ModelMaterial/Ramp").c_str())) {
        ImGui::End();
        return;
    }
    if (!authoringDocument_.HasMaterial()) {
        ImGui::TextWrapped(TEXT::Localize("editor.ui.open.or.create.a.material.to.edit.its.ramp.data.a0980b").c_str());
        ImGui::End();
        return;
    }

    MaterialAssetData& source = authoringDocument_.EditMaterial();
    if (source.kind != MaterialAssetKind::Ramp) {
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.ramp.curves.are.independent.reusable.assets.bind.them.from.a.mat.cd64c7")
                .c_str());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.create.ramp.asset.43292d", "Create Ramp Asset").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            (void)CreateAuxiliaryMaterialAsset(scene, MaterialAssetKind::Ramp, statusMessage_);
        }
        ImGui::End();
        return;
    }
    MaterialRampData& ramp = source.ramp;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.dimension.45e147", "Dimension").c_str(),
                          ToString(ramp.dimension).data())) {
        for (MaterialRampDimension value : {MaterialRampDimension::OneD, MaterialRampDimension::TwoD}) {
            if (ImGui::Selectable(ToString(value).data(), value == ramp.dimension)) {
                MaterialAssetData before = source;
                ramp.dimension = value;
                ramp.height = value == MaterialRampDimension::OneD ? 1u : (std::max)(ramp.height, 16u);
                if (value == MaterialRampDimension::TwoD) {
                    ramp.width = (std::min)(ramp.width, ASSETS::MATERIAL::kMaximumMaterialRamp2DResolution);
                    ramp.height = (std::min)(ramp.height, ASSETS::MATERIAL::kMaximumMaterialRamp2DResolution);
                }
                authoringDocument_.CommitMaterial(std::move(before), "Change Ramp dimension");
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("ui.vfx.text.Interpolation", "Interpolation").c_str(),
                          ToString(ramp.interpolation).data())) {
        for (MaterialRampInterpolation value : {MaterialRampInterpolation::Constant, MaterialRampInterpolation::Linear,
                                                MaterialRampInterpolation::Smooth}) {
            if (ImGui::Selectable(ToString(value).data(), value == ramp.interpolation)) {
                MaterialAssetData before = source;
                ramp.interpolation = value;
                authoringDocument_.CommitMaterial(std::move(before), "Change Ramp interpolation");
            }
        }
        ImGui::EndCombo();
    }

    int width = static_cast<int>(ramp.width);
    const int maximumResolution = static_cast<int>(ramp.dimension == MaterialRampDimension::OneD
                                                       ? ASSETS::MATERIAL::kMaximumMaterialRamp1DResolution
                                                       : ASSETS::MATERIAL::kMaximumMaterialRamp2DResolution);
    if (ImGui::InputInt(TEXT::LocalizeWindow("ui.vfx.text.Width", "Width").c_str(), &width, 16, 64,
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
        MaterialAssetData before = source;
        ramp.width = static_cast<uint32_t>(std::clamp(width, 16, maximumResolution));
        authoringDocument_.CommitMaterial(std::move(before), "Change Ramp resolution");
    }
    if (ramp.dimension == MaterialRampDimension::TwoD) {
        int height = static_cast<int>(ramp.height);
        if (ImGui::InputInt(TEXT::LocalizeWindow("editor.ui.height.3f608b", "Height").c_str(), &height, 16, 64,
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            MaterialAssetData before = source;
            ramp.height = static_cast<uint32_t>(std::clamp(height, 16, maximumResolution));
            authoringDocument_.CommitMaterial(std::move(before), "Change Ramp resolution");
        }
    }

    ImGui::SeparatorText(TEXT::Localize("ui.vfx.text.Preview").c_str());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size{(std::max)(ImGui::GetContentRegionAvail().x, 160.0f),
                ramp.dimension == MaterialRampDimension::OneD ? 72.0f : 180.0f};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(28, 30, 34, 255));
    std::vector<size_t> order(ramp.points.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&ramp](size_t left, size_t right) {
        return ramp.points[left].position.x < ramp.points[right].position.x;
    });
    if (order.size() == 1u) {
        drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, RampColor(ramp.points.front().color));
    } else {
        for (size_t i = 1u; i < order.size(); ++i) {
            const MaterialRampPointData& left = ramp.points[order[i - 1u]];
            const MaterialRampPointData& right = ramp.points[order[i]];
            const float x0 = origin.x + std::clamp(left.position.x, 0.0f, 1.0f) * size.x;
            const float x1 = origin.x + std::clamp(right.position.x, 0.0f, 1.0f) * size.x;
            const ImU32 leftColor = RampColor(left.color);
            const ImU32 rightColor =
                ramp.interpolation == MaterialRampInterpolation::Constant ? leftColor : RampColor(right.color);
            drawList->AddRectFilledMultiColor({x0, origin.y}, {x1, origin.y + size.y}, leftColor, rightColor,
                                              rightColor, leftColor);
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##RampPreview", size);
    for (MaterialRampPointData& point : ramp.points) {
        const ImVec2 pointPosition{origin.x + std::clamp(point.position.x, 0.0f, 1.0f) * size.x,
                                   origin.y + (ramp.dimension == MaterialRampDimension::TwoD
                                                   ? std::clamp(point.position.y, 0.0f, 1.0f) * size.y
                                                   : size.y * 0.5f)};
        drawList->AddCircleFilled(pointPosition, selectedRampPointId_ == point.id ? 8.0f : 6.0f,
                                  RampColor(point.color));
        drawList->AddCircle(
            pointPosition, selectedRampPointId_ == point.id ? 9.0f : 7.0f,
            selectedRampPointId_ == point.id ? IM_COL32(255, 210, 90, 255) : IM_COL32(235, 240, 245, 255), 16, 2.0f);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 delta{ImGui::GetIO().MousePos.x - pointPosition.x,
                               ImGui::GetIO().MousePos.y - pointPosition.y};
            if (delta.x * delta.x + delta.y * delta.y <= 100.0f) {
                selectedRampPointId_ = point.id;
            }
        }
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + size.y});
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.ramp.point.43d5d9", "Add Ramp Point").c_str())) {
        MaterialAssetData before = source;
        MaterialRampPointData point{};
        point.id = "RampPoint_" + std::to_string(authoringIdCounter_++);
        point.position = {0.5f, 0.5f};
        selectedRampPointId_ = point.id;
        ramp.points.push_back(std::move(point));
        authoringDocument_.CommitMaterial(std::move(before), "Add Ramp point");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedRampPointId_.empty());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.delete.point.325db5", "Delete Point").c_str())) {
        MaterialAssetData before = source;
        std::erase_if(ramp.points, [this](const auto& point) { return point.id == selectedRampPointId_; });
        selectedRampPointId_.clear();
        authoringDocument_.CommitMaterial(std::move(before), "Delete Ramp point");
    }
    ImGui::EndDisabled();

    MaterialRampPointData* selected = FindPoint(ramp, selectedRampPointId_);
    if (selected != nullptr) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.selected.point.f6866e").c_str());
        MaterialAssetData beforePosition = source;
        bool positionChanged = false;
        if (ramp.dimension == MaterialRampDimension::OneD) {
            positionChanged = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.vfx.text.Position", "Position").c_str(),
                                                 &selected->position.x, 0.0f, 1.0f);
            if (positionChanged)
                selected->position.y = 0.0f;
        } else {
            positionChanged = ImGui::SliderFloat2("Position", &selected->position.x, 0.0f, 1.0f);
        }
        TrackMaterialItem(std::move(beforePosition), "Move Ramp point", positionChanged);
        MaterialAssetData beforeColor = source;
        const bool colorChanged =
            ImGui::ColorEdit4(TEXT::LocalizeWindow("ui.vfx.text.Color", "Color").c_str(), &selected->color.x);
        TrackMaterialItem(std::move(beforeColor), "Edit Ramp color", colorChanged);
    }
    ImGui::TextDisabled(TEXT::Localize("material.ramp.edit.gestureHint").c_str());
    ImGui::End();
#else
    (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
