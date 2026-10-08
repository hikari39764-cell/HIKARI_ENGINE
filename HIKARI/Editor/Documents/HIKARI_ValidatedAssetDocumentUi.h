#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <string>

#include <imgui.h>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"

// 検証付き資産文書の共通 UI。後処理・環境・天気など、一度に一つの資産を開く編集器が使う。
namespace HIKARI::EDITOR {

inline constexpr ImVec4 kAssetDocumentErrorColor{1.0f, 0.45f, 0.45f, 1.0f};
inline constexpr ImVec4 kAssetDocumentWarningColor{1.0f, 0.72f, 0.25f, 1.0f};

// guid の資産を文書として開く。別の資産に未保存の変更があれば、保存か破棄を選ばせて false を返す。
template <class Traits>
bool OpenAssetDocumentForEdit(ValidatedAssetDocument<Traits>& document, const AssetGuid& guid, AssetDatabase& database,
                              std::string& status) {
    if (document.IsOpen() && document.Guid() == guid) {
        return true;
    }
    std::string message;
    if (document.Open(database, guid, message)) {
        status.clear();
        return true;
    }
    if (!document.IsOpen() || !document.IsDirty()) {
        ImGui::TextColored(kAssetDocumentErrorColor, "%s", message.c_str());
        return false;
    }
    ImGui::TextColored(kAssetDocumentWarningColor, "%s", TEXT::Localize("asset.document.pendingOther").c_str());
    if (ImGui::Button(TEXT::Localize("menu.edit.save").c_str()) && !document.Save(database, message)) {
        status = message;
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("asset.document.discardAndOpen").c_str())) {
        if (document.Open(database, guid, message, true)) {
            status.clear();
            return true;
        }
        status = message;
    }
    return false;
}

// 元に戻す・やり直す・保存・破棄の操作列。
template <class Traits>
void DrawAssetDocumentToolbar(ValidatedAssetDocument<Traits>& document, AssetDatabase& database, std::string& status) {
    std::string message;
    ImGui::BeginDisabled(!document.CanUndo());
    if (ImGui::Button(TEXT::Localize("menu.edit.undo").c_str())) {
        (void)document.Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.CanRedo());
    if (ImGui::Button(TEXT::Localize("menu.edit.redo").c_str())) {
        (void)document.Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.IsDirty());
    if (ImGui::Button(TEXT::Localize("menu.edit.save").c_str())) {
        status = document.Save(database, message) ? std::string{} : message;
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.assetInspector.revert").c_str())) {
        status = document.Open(database, document.Guid(), message, true) ? std::string{} : message;
    }
    ImGui::EndDisabled();
    if (document.IsDirty()) {
        ImGui::SameLine();
        ImGui::TextColored(kAssetDocumentWarningColor, "%s", TEXT::Localize("asset.document.unsaved").c_str());
    }
}

} // namespace HIKARI::EDITOR

#endif
