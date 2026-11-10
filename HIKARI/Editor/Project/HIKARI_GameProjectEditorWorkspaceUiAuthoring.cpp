#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceUi.h"

#include <cstring>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "HIKARI/Project/EditorNodeGraph.h"
#include "HIKARI/Project/PropertyFields.h"
#include "Runtime/Project/HIKARI_GameProjectAuthoringHost.h"

// 作業区の界面のうち、資産の選択・項目の面板・節点図・クリップボード。
namespace HIKARI::EDITOR {

bool GameProjectEditorWorkspaceUi::AssetField(std::string_view stableControlId, std::string_view label,
                                              GameProjectAssetKind kind, char* assetGuidUtf8, size_t bufferCapacity) {
    return DrawAssetGuidField(stableControlId, label, ToAssetType(kind), {}, assetGuidUtf8, bufferCapacity);
}

bool GameProjectEditorWorkspaceUi::ModuleAssetField(std::string_view stableControlId, std::string_view label,
                                                    std::string_view moduleAssetTypeId, char* assetGuidUtf8,
                                                    size_t bufferCapacity) {
    return !moduleAssetTypeId.empty() && DrawAssetGuidField(stableControlId, label, AssetType::ModuleAsset,
                                                            moduleAssetTypeId, assetGuidUtf8, bufferCapacity);
}

bool GameProjectEditorWorkspaceUi::DrawAssetGuidField(std::string_view stableControlId, std::string_view label,
                                                      AssetType type, std::string_view moduleAssetTypeId,
                                                      char* assetGuidUtf8, size_t bufferCapacity) {
    if (database_ == nullptr || assetGuidUtf8 == nullptr || bufferCapacity == 0u) {
        return false;
    }
    // 終端の無い buffer は「選ばれていない」として扱う。
    const size_t length = ::strnlen(assetGuidUtf8, bufferCapacity);
    std::string guid(assetGuidUtf8, length < bufferCapacity ? length : 0u);

    // 検査器と同じ選択欄（検索できる一覧、資産浏览器からの拖放）。名前は他の控件に合わせて右に置く。
    AssetFieldOptions options{};
    options.label = "##Asset";
    options.requiredType = type;
    options.moduleAssetTypeId = moduleAssetTypeId;
    options.allowLocate = false;
    options.drawLabel = false;
    options.width = ImGui::CalcItemWidth();
    // 一まとまりにしておくと、直後の Tooltip が欄のどこに乗っても出る。
    ImGui::BeginGroup();
    ImGui::PushID(MakeWorkspaceControlLabel({}, workspaceId_, stableControlId).c_str());
    const bool changed = DrawAssetField(database_, options, guid);
    ImGui::PopID();
    if (!label.empty()) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
    }
    ImGui::EndGroup();
    if (!changed || guid.size() >= bufferCapacity) {
        return false;
    }
    std::memcpy(assetGuidUtf8, guid.c_str(), guid.size() + 1u);
    return true;
}

void GameProjectEditorWorkspaceUi::Fields(std::string_view stableControlId, IPropertyFieldDeclaration& declaration) {
    if (panelDepth_ == 0u || canvasActive_) {
        return;
    }
    // 部品の検査器と同じ構築器で描くので、同じ宣言は検査器でも作業区でも同じ控件になる。
    ImGuiInspectorBuilder builder{};
    InspectorContext context{};
    context.assetRegistry = assets_;
    context.assetDatabase = database_;
    builder.SetContext(context);
    ImGui::PushID(MakeWorkspaceControlLabel({}, workspaceId_, stableControlId).c_str());
    InspectPropertyFields(declaration, builder);
    ImGui::PopID();
}

void GameProjectEditorWorkspaceUi::NodeGraph(std::string_view stableGraphId,
                                             const EDITOR_PROJECT::EditorNodeGraphDesc& desc,
                                             EDITOR_PROJECT::EditorNodeGraphResult& outResult) {
    outResult = {};
    if (panelDepth_ == 0u || canvasActive_ || desc.structSize < sizeof(EDITOR_PROJECT::EditorNodeGraphDesc)) {
        return;
    }
    const std::string key = MakeWorkspaceControlLabel({}, workspaceId_, stableGraphId);
    nodeGraphs_[key].Draw(key.c_str(), desc, outResult);
}

void GameProjectEditorWorkspaceUi::SetClipboardText(std::string_view utf8Text) {
    ImGui::SetClipboardText(std::string(utf8Text).c_str());
}

size_t GameProjectEditorWorkspaceUi::GetClipboardText(char* utf8Buffer, size_t bufferCapacity) {
    const char* text = ImGui::GetClipboardText();
    const size_t needed = (text != nullptr ? std::strlen(text) : 0u) + 1u;
    if (utf8Buffer != nullptr && bufferCapacity != 0u) {
        if (text != nullptr && needed <= bufferCapacity) {
            std::memcpy(utf8Buffer, text, needed);
        } else {
            utf8Buffer[0] = '\0';
        }
    }
    return needed;
}

} // namespace HIKARI::EDITOR
