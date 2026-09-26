#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <json.hpp>

#include "Editor/Documents/HIKARI_RuntimeBindingEditorDocument.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

class EditorCommandRouter;

struct RuntimeBindingReference {
    std::string targetKind{};
    std::string targetId{};
    std::string targetLabel{};
    std::string slotId{};
    std::string slotLabel{};
    std::string access{};
    std::string status{"resolved"};
};

struct RuntimeBindingRemovalResult {
    bool removed = false;
    bool confirmationRequired = false;
    bool referenceIndexAvailable = false;
    std::string message{};
    std::vector<RuntimeBindingReference> references{};
};

struct RuntimeBindingWorkspaceResult {
    bool exitToSceneRequested = false;
    std::string statusMessage{};
};

class RuntimeBindingWorkspaceController final {
public:
    using ReferenceProvider = std::function<std::vector<RuntimeBindingReference>(std::string_view)>;
    using ReferenceRevealCallback = std::function<void(const RuntimeBindingReference&)>;

    bool Activate(DocumentSceneBase& scene, std::string& outMessage);
    void DrawDockSpace(bool resetDefaultDockLayout) const;
    RuntimeBindingWorkspaceResult Draw(DocumentSceneBase& scene, EditorCommandRouter& commandRouter);
    void BindDocumentCommands(EditorCommandRouter& commandRouter) const;

    void SetReferenceProvider(ReferenceProvider provider);
    void SetReferenceRevealCallback(ReferenceRevealCallback callback);

    bool IsDocumentOpen() const noexcept;
    bool IsDocumentDirty() const noexcept;
    // 文書の状態。シェル、AI の文脈、バックグラウンドのセッションで共有する。
    nlohmann::json DescribeDocumentState() const;
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    bool Save(DocumentSceneBase& scene, std::string& outMessage);
    bool Undo(DocumentSceneBase& scene, std::string& outMessage);
    bool Redo(DocumentSceneBase& scene, std::string& outMessage);

    bool CreateVariable(DocumentSceneBase& scene, BINDING::ValueType type, std::string_view displayName,
                        std::string_view requestedId, std::string& outId, std::string& outMessage);
    bool AddEventDefinition(DocumentSceneBase& scene, bool hasPayload, BINDING::ValueType payloadType,
                            std::string_view displayName, std::string_view requestedId, std::string& outId,
                            std::string& outMessage);
    bool SetItemProperty(DocumentSceneBase& scene, std::string_view itemId, std::string_view propertyId,
                         const nlohmann::json& value, std::string& outMessage, uint64_t mergeGroup = 0u);
    RuntimeBindingRemovalResult RemoveItem(DocumentSceneBase& scene, std::string_view itemId, bool confirmed);

    bool PreviewSetVariable(DocumentSceneBase& scene, std::string_view variableId, const nlohmann::json& value,
                            std::string& outMessage);
    bool PreviewEmitEvent(DocumentSceneBase& scene, std::string_view eventId, const nlohmann::json* payload,
                          std::string& outMessage);
    bool PreviewReset(DocumentSceneBase& scene, std::string_view variableId, std::string& outMessage);

    const BINDING::Catalog& GetCatalog() const noexcept;
    std::vector<BINDING::CatalogDiagnostic> GetDiagnostics() const;
    std::vector<RuntimeBindingReference> GetReferences(std::string_view itemId) const;
    bool IsReferenceIndexAvailable() const noexcept;
    const std::string& GetStatusMessage() const noexcept;
    void SelectItem(std::string itemId);

private:
    static constexpr std::string_view kPreviewWriterId = "editor.runtime_binding.preview";

    enum class CatalogFilter : uint8_t {
        All,
        Variables,
        Events,
    };

    enum class SelectionKind : uint8_t {
        None,
        Variable,
        Event,
    };

    bool EnsureDocumentOpen(DocumentSceneBase& scene, std::string& outMessage);
    bool ApplyCatalog(DocumentSceneBase& scene, BINDING::Catalog catalog, std::string& outMessage,
                      uint64_t mergeGroup = 0u);
    bool ProjectCatalog(DocumentSceneBase& scene, std::string& outMessage);
    std::string AllocateStableId(std::string_view prefix) const;
    bool ContainsId(std::string_view id) const noexcept;
    void NormalizeSelection() noexcept;
    BINDING::VariableDefinition* SelectedVariable() noexcept;
    const BINDING::VariableDefinition* SelectedVariable() const noexcept;
    BINDING::EventDefinition* SelectedEvent() noexcept;
    const BINDING::EventDefinition* SelectedEvent() const noexcept;
    void InvalidateInspectorDraft() noexcept;

    void DrawToolbarWindow(DocumentSceneBase& scene, RuntimeBindingWorkspaceResult& result,
                           EditorCommandRouter& commandRouter);
    void DrawCatalogWindow(DocumentSceneBase& scene);
    void DrawLiveReferencesWindow(DocumentSceneBase& scene);
    void DrawInspectorWindow(DocumentSceneBase& scene);

    RuntimeBindingEditorDocument document_{};
    ReferenceProvider referenceProvider_{};
    ReferenceRevealCallback referenceRevealCallback_{};

    CatalogFilter catalogFilter_ = CatalogFilter::All;
    SelectionKind selectionKind_ = SelectionKind::None;
    std::string selectedItemId_{};
    std::array<char, 128u> searchBuffer_{};

    std::string inspectorDraftItemId_{};
    std::array<char, 256u> inspectorNameBuffer_{};
    std::array<char, 1024u> inspectorDescriptionBuffer_{};
    BINDING::Value inspectorDefaultValue_{};
    bool inspectorHasRange_ = false;
    float inspectorMinimum_ = 0.0f;
    float inspectorMaximum_ = 1.0f;
    float inspectorStep_ = 0.01f;
    bool inspectorHasPayload_ = false;
    BINDING::ValueType inspectorPayloadType_ = BINDING::ValueType::Float;

    std::string previewDraftItemId_{};
    BINDING::Value previewVariableValue_{};
    BINDING::Value previewEventPayload_{};
    std::unordered_set<std::string> previewVariableIds_{};

    std::string pendingDeleteItemId_{};
    std::vector<RuntimeBindingReference> pendingDeleteReferences_{};
    bool requestDeleteDialog_ = false;

    std::string statusMessage_{};
};

} // namespace HIKARI::EDITOR
