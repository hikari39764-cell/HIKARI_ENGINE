#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceUi.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <imgui_internal.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"

namespace HIKARI::EDITOR {
std::string MakeWorkspaceControlLabel(std::string_view label, std::string_view workspaceId,
                                      std::string_view controlId) {
    std::string result(label);
    result += "###GameProject/";
    result += workspaceId;
    result += '/';
    result += controlId;
    return result;
}

GameProjectEditorWorkspaceUi::~GameProjectEditorWorkspaceUi() { previewTarget_.Reset(); }

D3D12_GPU_DESCRIPTOR_HANDLE GameProjectEditorWorkspaceUi::ThumbnailTexture(const std::string& key,
                                                                           const uint8_t* pixels, uint32_t width,
                                                                           uint32_t height, uint64_t revision) {
    Thumbnail& thumbnail = thumbnails_[key];
    if (thumbnail.handle < 0 || thumbnail.revision != revision) {
        if (thumbnail.handle >= 0) {
            DXTEX::DxTextureManager::ReleaseTextureDeferred(thumbnail.handle);
        }
        const std::vector<uint8_t> copy(pixels, pixels + static_cast<size_t>(width) * height * 4u);
        thumbnail.handle = DXTEX::DxTextureManager::CreateRgbaTexture(
            "editor/workspace/thumbnail/" + key + "/" + std::to_string(revision), width, height, copy,
            DXTEX::TextureColorSpace::Srgb);
        thumbnail.revision = revision;
    }
    if (thumbnail.handle < 0) {
        return {};
    }
    return DXTEX::DxTextureManager::GetSrvGpuHandle(thumbnail.handle);
}

void GameProjectEditorWorkspaceUi::PushSelectionColor(bool selected) {
    if (selected) {
        const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.72f));
    }
}

void GameProjectEditorWorkspaceUi::PopSelectionColor(bool selected) {
    if (selected) {
        ImGui::PopStyleColor();
    }
}

void GameProjectEditorWorkspaceUi::SetAssets(AssetDatabase& database, AssetRegistry& registry) {
    database_ = &database;
    assets_ = &registry;
}

void GameProjectEditorWorkspaceUi::BeginFrame(std::string_view workspaceId) {
    workspaceId_.assign(workspaceId);
    previewOpen_ = false;
    previewContent_.DiscardPending();
    panelDepth_ = 0u;
    tableDepth_ = 0u;
    canvasActive_ = false;
    canvasDrawList_ = nullptr;
}

void GameProjectEditorWorkspaceUi::SetWorkspaceId(std::string_view workspaceId) { workspaceId_.assign(workspaceId); }

void GameProjectEditorWorkspaceUi::ClearDockRegions() noexcept {
    dockRegions_.fill(0u);
    forceDock_ = false;
    floatingPanelCount_ = 0;
}

void GameProjectEditorWorkspaceUi::DrawDockSpace(bool reset, bool wideCenter) {
    const auto* viewport = ImGui::GetMainViewport();
    const std::string name = "HIKARI_ProjectWorkspace/" + workspaceId_;
    const ImGuiID root = ImGui::GetID(name.c_str());
    forceDock_ = reset || ImGui::DockBuilderGetNode(root) == nullptr;
    if (forceDock_) {
        ImGui::DockBuilderRemoveNode(root);
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(root, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(root, viewport->WorkSize);
        ImGuiID body = root;
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Up, wideCenter ? 0.06f : 0.08f, &dockRegions_[1], &body);
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Left, 0.21f, &dockRegions_[2], &body);
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Right, 0.30f, &dockRegions_[3], &body);
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Down, wideCenter ? 0.11f : 0.25f, &dockRegions_[4], &body);
        dockRegions_[0] = body;
        ImGui::DockBuilderFinish(root);
    } else {
        // 保存済みレイアウトは ImGui が管理する。新規パネルだけ中央へ入れる。
        dockRegions_.fill(root);
    }
    HIKARI::EDITOR::DrawWorkspaceDockSpace(root, viewport);
}

void GameProjectEditorWorkspaceUi::PrepareSceneViewportWindow() {
    if (dockRegions_[0] != 0u) {
        ImGui::SetNextWindowDockID(dockRegions_[0], forceDock_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    }
}

void GameProjectEditorWorkspaceUi::EndFrame() noexcept {
    if (canvasActive_) {
        EndCanvas();
    }
    while (tableDepth_ > 0u) {
        ImGui::EndTable();
        --tableDepth_;
    }
    while (panelDepth_ > 0u) {
        ImGui::End();
        --panelDepth_;
    }
    workspaceId_.clear();
}

bool GameProjectEditorWorkspaceUi::BeginPanel(std::string_view stablePanelId, std::string_view displayName, bool* open,
                                              EDITOR_PROJECT::EditorWorkspaceDockRegion region) {
    const std::string label = MakeWorkspaceControlLabel(displayName, workspaceId_, stablePanelId);
    ++panelDepth_;
    const size_t slot = static_cast<size_t>(region);
    if (slot < dockRegions_.size() && dockRegions_[slot]) {
        ImGui::SetNextWindowDockID(dockRegions_[slot], forceDock_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    } else {
        // ドックに入らない窓は初回だけ、主窓の右寄りに少しずつずらして置く。
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float offset = 28.0f * static_cast<float>(floatingPanelCount_++);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.56f + offset,
                                       viewport->WorkPos.y + viewport->WorkSize.y * 0.08f + offset),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 440.0f), ImGuiCond_FirstUseEver);
    }
    return ImGui::Begin(label.c_str(), open);
}

EDITOR_PROJECT::EditorWorkspaceInteraction GameProjectEditorWorkspaceUi::GetInteraction() const {
    const auto& io = ImGui::GetIO();
    // 元に戻す・やり直しは編集器の命令（Ctrl+Z、編集メニュー、工具列）から来る。キーは命令の側が先に取るので、
    // ここで読んでも届かない。
    return {
        ImGui::IsAnyItemActive() || ImGui::IsMouseDown(0) ? 1u : 0u,
        undoRequested_ ? 1u : 0u,
        redoRequested_ ? 1u : 0u,
        !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false) ? 1u : 0u,
    };
}

bool GameProjectEditorWorkspaceUi::InputTextMultiline(std::string_view id, std::string_view label, char* buffer,
                                                      size_t capacity, float height) {
    if (!buffer || !capacity)
        return false;
    Text(label);
    return ImGui::InputTextMultiline(MakeWorkspaceControlLabel(label, workspaceId_, id).c_str(), buffer, capacity,
                                     ImVec2(-1.0f, height));
}

void GameProjectEditorWorkspaceUi::EndPanel() {
    if (panelDepth_ == 0u) {
        return;
    }
    if (canvasActive_) {
        EndCanvas();
    }
    while (tableDepth_ > 0u) {
        ImGui::EndTable();
        --tableDepth_;
    }
    ImGui::End();
    --panelDepth_;
}

void GameProjectEditorWorkspaceUi::Text(std::string_view utf8Text) {
    ImGui::TextUnformatted(utf8Text.data(), utf8Text.data() + utf8Text.size());
}

void GameProjectEditorWorkspaceUi::Separator() { ImGui::Separator(); }

void GameProjectEditorWorkspaceUi::SameLine() { ImGui::SameLine(); }

bool GameProjectEditorWorkspaceUi::Button(std::string_view stableControlId, std::string_view label) {
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    return ImGui::Button(stableLabel.c_str());
}

bool GameProjectEditorWorkspaceUi::InputText(std::string_view stableControlId, std::string_view label, char* utf8Buffer,
                                             size_t bufferCapacity) {
    if (utf8Buffer == nullptr || bufferCapacity == 0u) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    return ImGui::InputText(stableLabel.c_str(), utf8Buffer, bufferCapacity);
}

bool GameProjectEditorWorkspaceUi::Checkbox(std::string_view stableControlId, std::string_view label, bool* value) {
    if (value == nullptr) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    return ImGui::Checkbox(stableLabel.c_str(), value);
}

bool GameProjectEditorWorkspaceUi::InputInt(std::string_view stableControlId, std::string_view label, int32_t* value,
                                            EDITOR_PROJECT::EditorWorkspaceIntInputOptions options) {
    if (value == nullptr) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    int edited = static_cast<int>(*value);
    const bool changed = ImGui::InputInt(stableLabel.c_str(), &edited, std::max(1, static_cast<int>(options.step)));
    if (changed) {
        if (options.clampToRange != 0u && options.minimum <= options.maximum) {
            edited = std::clamp(edited, static_cast<int>(options.minimum), static_cast<int>(options.maximum));
        }
        *value = static_cast<int32_t>(edited);
    }
    return changed;
}

bool GameProjectEditorWorkspaceUi::InputFloat(std::string_view stableControlId, std::string_view label, float* value,
                                              EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options) {
    if (value == nullptr) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    const bool changed = ImGui::InputFloat(stableLabel.c_str(), value, std::max(0.0f, options.step));
    if (changed && options.clampToRange != 0u && options.minimum <= options.maximum) {
        *value = std::clamp(*value, options.minimum, options.maximum);
    }
    return changed;
}

bool GameProjectEditorWorkspaceUi::InputFloat2(std::string_view stableControlId, std::string_view label,
                                               EDITOR_PROJECT::EditorWorkspacePoint* value,
                                               EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options) {
    if (value == nullptr) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    float values[2]{value->x, value->y};
    const float speed = std::max(0.001f, options.step);
    const bool hasRange = options.clampToRange != 0u && options.minimum <= options.maximum;
    const float minimum = hasRange ? options.minimum : 0.0f;
    const float maximum = hasRange ? options.maximum : 0.0f;
    const bool changed = ImGui::DragFloat2(stableLabel.c_str(), values, speed, minimum, maximum);
    if (changed) {
        value->x = values[0];
        value->y = values[1];
        if (hasRange) {
            value->x = std::clamp(value->x, options.minimum, options.maximum);
            value->y = std::clamp(value->y, options.minimum, options.maximum);
        }
    }
    return changed;
}

bool GameProjectEditorWorkspaceUi::Color4(std::string_view stableControlId, std::string_view label,
                                          EDITOR_PROJECT::EditorWorkspaceColor* value) {
    if (value == nullptr) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    float color[4]{value->r, value->g, value->b, value->a};
    const bool changed = ImGui::ColorEdit4(stableLabel.c_str(), color);
    if (changed) {
        value->r = color[0];
        value->g = color[1];
        value->b = color[2];
        value->a = color[3];
    }
    return changed;
}

bool GameProjectEditorWorkspaceUi::Selectable(std::string_view stableControlId, std::string_view label, bool selected) {
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    PushSelectionColor(selected);
    const bool pressed = ImGui::Selectable(stableLabel.c_str(), selected);
    PopSelectionColor(selected);
    return pressed;
}

bool GameProjectEditorWorkspaceUi::Choice(std::string_view stableControlId, std::string_view label, bool selected) {
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    const std::string text(label);
    PushSelectionColor(selected);
    const bool pressed =
        ImGui::Selectable(stableLabel.c_str(), selected, 0, ImGui::CalcTextSize(text.c_str(), nullptr, true));
    PopSelectionColor(selected);
    return pressed;
}

bool GameProjectEditorWorkspaceUi::SliderFloat(std::string_view stableControlId, std::string_view label, float* value,
                                               float minimum, float maximum) {
    if (value == nullptr || !(minimum < maximum)) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    const float span = maximum - minimum;
    const bool wide = minimum > 0.0f ? maximum / minimum >= 100.0f : minimum == 0.0f && maximum >= 1000.0f;
    // 対数の滑块は値の大きさで桁を変える。0 まである物は 0 の近くを潰さないよう桁を固定する。
    const float shown = std::fabs(*value);
    const char* format = wide ? (minimum == 0.0f   ? "%.3f"
                                 : shown >= 100.0f ? "%.0f"
                                 : shown >= 10.0f  ? "%.1f"
                                 : shown >= 1.0f   ? "%.2f"
                                                   : "%.3f")
                              : (span > 500.0f ? "%.0f" : (span > 20.0f ? "%.1f" : (span > 1.5f ? "%.2f" : "%.3f")));
    return ImGui::SliderFloat(stableLabel.c_str(), value, minimum, maximum, format,
                              ImGuiSliderFlags_AlwaysClamp | (wide ? ImGuiSliderFlags_Logarithmic : 0));
}

bool GameProjectEditorWorkspaceUi::Section(std::string_view stableControlId, std::string_view label, bool defaultOpen) {
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    return ImGui::CollapsingHeader(stableLabel.c_str(), defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
}

void GameProjectEditorWorkspaceUi::Tooltip(std::string_view utf8Text) {
    if (!utf8Text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%.*s", static_cast<int>(utf8Text.size()), utf8Text.data());
    }
}

bool GameProjectEditorWorkspaceUi::Combo(std::string_view stableControlId, std::string_view label,
                                         std::span<const std::string_view> items, int32_t* selected) {
    if (selected == nullptr) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel(label, workspaceId_, stableControlId);
    const bool valid = *selected >= 0 && static_cast<size_t>(*selected) < items.size();
    const std::string shown = valid ? std::string(items[static_cast<size_t>(*selected)]) : std::string();
    bool changed = false;
    if (ImGui::BeginCombo(stableLabel.c_str(), shown.c_str())) {
        for (size_t index = 0u; index < items.size(); ++index) {
            const std::string item(items[index]);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(item.c_str(), valid && static_cast<size_t>(*selected) == index)) {
                *selected = static_cast<int32_t>(index);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool GameProjectEditorWorkspaceUi::ImageSelectable(std::string_view stableControlId, std::string_view label,
                                                   const uint8_t* pixels, uint32_t width, uint32_t height,
                                                   uint64_t revision, bool selected, float rowHeight) {
    const std::string stableLabel = MakeWorkspaceControlLabel("", workspaceId_, stableControlId);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float row = (std::max)(rowHeight, ImGui::GetTextLineHeight());
    PushSelectionColor(selected);
    const bool pressed = ImGui::Selectable(stableLabel.c_str(), selected, 0, ImVec2(0.0f, row));
    PopSelectionColor(selected);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float textX = start.x + 6.0f;
    if (pixels != nullptr && width != 0u && height != 0u) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = ThumbnailTexture(stableLabel, pixels, width, height, revision);
        if (srv.ptr != 0u) {
            const float picture = row - 4.0f;
            const ImVec2 corner(start.x + 2.0f, start.y + 2.0f);
            drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)), corner,
                               ImVec2(corner.x + picture, corner.y + picture));
            textX = corner.x + picture + 8.0f;
        }
    }
    const std::string text(label);
    drawList->AddText(ImVec2(textX, start.y + (row - ImGui::GetTextLineHeight()) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    return pressed;
}

bool GameProjectEditorWorkspaceUi::BeginTable(std::string_view stableTableId, uint32_t columnCount) {
    if (columnCount == 0u || columnCount > 64u || panelDepth_ == 0u || canvasActive_) {
        return false;
    }
    const std::string stableLabel = MakeWorkspaceControlLabel("Table", workspaceId_, stableTableId);
    if (!ImGui::BeginTable(stableLabel.c_str(), static_cast<int>(columnCount),
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        return false;
    }
    ++tableDepth_;
    return true;
}

void GameProjectEditorWorkspaceUi::TableNextRow() {
    if (tableDepth_ != 0u) {
        ImGui::TableNextRow();
    }
}

bool GameProjectEditorWorkspaceUi::TableNextColumn() { return tableDepth_ != 0u && ImGui::TableNextColumn(); }

void GameProjectEditorWorkspaceUi::EndTable() {
    if (tableDepth_ == 0u) {
        return;
    }
    if (canvasActive_) {
        EndCanvas();
    }
    ImGui::EndTable();
    --tableDepth_;
}

} // namespace HIKARI::EDITOR
