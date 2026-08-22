#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "Assets/Dialogue/HIKARI_DialogueAssetStore.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialoguePreviewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    DialogueWorkspaceController::DialogueWorkspaceController()
        : previewRenderer_(std::make_unique<DialoguePreviewRenderer>()) {}

    DialogueWorkspaceController::~DialogueWorkspaceController() = default;

    void DialogueWorkspaceController::ApplyWorkspaceActivation(
        DocumentSceneBase& scene,
        const EditorWorkspaceActivation& activation) {
        if (activation.current != EditorWorkspaceId::Dialogue) return;
        projectRoot_ = scene.GetAssetDatabase().GetProjectRoot();
        if (!styleDocument_.IsOpen()) {
            std::string styleMessage{};
            if (!styleDocument_.Open(
                    projectRoot_,
                    scene.GetAssetDatabase(),
                    styleMessage)) {
                styleDocument_.New(projectRoot_, scene.GetAssetDatabase());
                statusMessage_ = styleMessage.empty()
                    ? "Dialogue UI style could not be loaded; using defaults"
                    : styleMessage + "; using defaults";
            }
            else {
                statusMessage_ = std::move(styleMessage);
            }
        }
        if (activation.dialogueAssetGuid) {
            if (document_.IsDirty() &&
                document_.GetAssetGuid() != *activation.dialogueAssetGuid) {
                statusMessage_ =
                    "Save or revert the current dialogue before opening another asset";
                return;
            }
            (void)OpenAsset(
                scene.GetAssetDatabase(),
                *activation.dialogueAssetGuid,
                statusMessage_);
            activePage_ = DialogueWorkspacePage::Story;
        }
        else if (!document_.IsOpen()) {
            document_.New();
            if (statusMessage_.empty()) statusMessage_ = "New dialogue";
        }
        NormalizeSelection();
        NormalizeSpeakerSelection();
    }

    DialogueWorkspaceResult DialogueWorkspaceController::Draw(
        DocumentSceneBase& scene,
        EditorCommandRouter& commandRouter) {
        DialogueWorkspaceResult result{};
        DrawDialogueWorkspaceWindows(
            *this, scene, result, commandRouter);
        result.statusMessage = statusMessage_;
        return result;
    }

    void DialogueWorkspaceController::BindDocumentCommands(
        EditorCommandRouter& commandRouter,
        DocumentSceneBase& scene) {
        commandRouter.Bind(
            EditorCommandId::SaveDocument,
            TEXT::Localize("menu.edit.save"),
            activePage_ == DialogueWorkspacePage::Style
                ? styleDocument_.IsOpen()
                : document_.IsOpen(),
            [this, &scene]() { (void)Save(scene, statusMessage_); });
        commandRouter.Bind(
            EditorCommandId::Undo,
            "Undo Dialogue Edit",
            activePage_ == DialogueWorkspacePage::Style
                ? styleDocument_.CanUndo()
                : document_.CanUndo(),
            [this]() { (void)Undo(statusMessage_); });
        commandRouter.Bind(
            EditorCommandId::Redo,
            "Redo Dialogue Edit",
            activePage_ == DialogueWorkspacePage::Style
                ? styleDocument_.CanRedo()
                : document_.CanRedo(),
            [this]() { (void)Redo(statusMessage_); });
    }

    bool DialogueWorkspaceController::Save(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        if (activePage_ == DialogueWorkspacePage::Style) {
            const bool saved = styleDocument_.Save(
                projectRoot_,
                scene.GetAssetDatabase(),
                outMessage);
            if (saved) {
                if (auto* store = scene.GetWorld().Services().Find<
                        DialogueAssetStore>()) {
                    store->InvalidateProjectStyle();
                }
            }
            return saved;
        }
        const bool saved = document_.Save(
            scene.GetAssetDatabase(), outMessage);
        if (saved) {
            if (auto* store = scene.GetWorld().Services().Find<
                    DialogueAssetStore>()) {
                store->Invalidate(document_.GetAssetGuid());
            }
        }
        return saved;
    }

    bool DialogueWorkspaceController::Undo(std::string& outMessage) {
        if (activePage_ == DialogueWorkspacePage::Style) {
            if (!styleDocument_.Undo()) {
                outMessage = "Nothing to undo";
                return false;
            }
            NormalizeSpeakerSelection();
            outMessage = "Dialogue UI style edit undone";
            return true;
        }
        if (!document_.Undo()) {
            outMessage = "Nothing to undo";
            return false;
        }
        NormalizeSelection();
        outMessage = "Dialogue edit undone";
        return true;
    }

    bool DialogueWorkspaceController::Redo(std::string& outMessage) {
        if (activePage_ == DialogueWorkspacePage::Style) {
            if (!styleDocument_.Redo()) {
                outMessage = "Nothing to redo";
                return false;
            }
            NormalizeSpeakerSelection();
            outMessage = "Dialogue UI style edit redone";
            return true;
        }
        if (!document_.Redo()) {
            outMessage = "Nothing to redo";
            return false;
        }
        NormalizeSelection();
        outMessage = "Dialogue edit redone";
        return true;
    }

    bool DialogueWorkspaceController::NewAsset(std::string& outMessage) {
        if (document_.IsDirty()) {
            outMessage = "Current dialogue has unsaved changes";
            return false;
        }
        document_.New();
        selectedNodeId_ = document_.Definition().entryNodeId;
        selectedChoiceIndex_.reset();
        outMessage = "New dialogue created";
        return true;
    }

    bool DialogueWorkspaceController::OpenAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        std::string& outMessage) {
        if (!assetGuid.IsValid()) {
            outMessage = "Select a Dialogue asset to open";
            return false;
        }
        if (document_.IsDirty() &&
            document_.GetAssetGuid() != assetGuid) {
            outMessage = "Current dialogue has unsaved changes";
            return false;
        }
        if (!document_.Open(database, assetGuid, outMessage)) return false;
        projectRoot_ = database.GetProjectRoot();
        selectedNodeId_ = document_.Definition().entryNodeId;
        selectedChoiceIndex_.reset();
        outMessage = "Dialogue asset opened";
        return true;
    }

    bool DialogueWorkspaceController::IsDocumentOpen() const noexcept {
        return document_.IsOpen();
    }

    bool DialogueWorkspaceController::IsDocumentDirty() const noexcept {
        return document_.IsDirty() || styleDocument_.IsDirty();
    }

    const DIALOGUE::DialogueDefinition&
        DialogueWorkspaceController::GetDefinition() const noexcept {
        return document_.Definition();
    }

    const DIALOGUE::DialogueProjectStyle&
        DialogueWorkspaceController::GetStyle() const noexcept {
        return styleDocument_.Style();
    }

    const std::string&
        DialogueWorkspaceController::GetStatusMessage() const noexcept {
        return statusMessage_;
    }

    DIALOGUE::DialogueNode*
        DialogueWorkspaceController::SelectedNode() noexcept {
        auto& nodes = document_.Definition().nodes;
        const auto found = std::find_if(
            nodes.begin(), nodes.end(),
            [this](const DIALOGUE::DialogueNode& node) {
                return node.id == selectedNodeId_;
            });
        return found == nodes.end() ? nullptr : &*found;
    }

    const DIALOGUE::DialogueNode*
        DialogueWorkspaceController::SelectedNode() const noexcept {
        const auto& nodes = document_.Definition().nodes;
        const auto found = std::find_if(
            nodes.begin(), nodes.end(),
            [this](const DIALOGUE::DialogueNode& node) {
                return node.id == selectedNodeId_;
            });
        return found == nodes.end() ? nullptr : &*found;
    }

    void DialogueWorkspaceController::NormalizeSelection() noexcept {
        auto& definition = document_.Definition();
        if (definition.nodes.empty()) {
            selectedNodeId_.clear();
            selectedChoiceIndex_.reset();
            return;
        }
        if (SelectedNode() == nullptr) {
            selectedNodeId_ = !definition.entryNodeId.empty()
                ? definition.entryNodeId
                : definition.nodes.front().id;
        }
        DIALOGUE::DialogueNode* node = SelectedNode();
        if (node == nullptr || !selectedChoiceIndex_ ||
            *selectedChoiceIndex_ >= node->choices.size()) {
            selectedChoiceIndex_.reset();
        }
    }

    void DialogueWorkspaceController::RecordMutation(
        DIALOGUE::DialogueDefinition before,
        uint64_t mergeGroup) {
        DIALOGUE::NormalizeDialogue(document_.Definition());
        document_.RecordApplied(std::move(before), mergeGroup);
        NormalizeSelection();
    }

    void DialogueWorkspaceController::RecordStyleMutation(
        DIALOGUE::DialogueProjectStyle before,
        uint64_t mergeGroup) {
        DIALOGUE::NormalizeDialogueProjectStyle(styleDocument_.Style());
        styleDocument_.RecordApplied(std::move(before), mergeGroup);
        NormalizeSpeakerSelection();
    }

    void DialogueWorkspaceController::NormalizeSpeakerSelection() noexcept {
        auto& speakers = styleDocument_.Style().speakers;
        if (speakers.empty()) {
            selectedSpeakerId_.clear();
            return;
        }
        if (DIALOGUE::FindDialogueSpeaker(
                styleDocument_.Style(),
                selectedSpeakerId_) == nullptr) {
            selectedSpeakerId_ = speakers.front().id;
        }
    }

    std::string DialogueWorkspaceController::AllocateNodeId(
        std::string_view prefix) const {
        const auto& nodes = document_.Definition().nodes;
        for (uint32_t suffix = 1u; suffix < 100000u; ++suffix) {
            const std::string candidate =
                std::string(prefix) + std::to_string(suffix);
            if (std::none_of(
                    nodes.begin(), nodes.end(),
                    [&candidate](const DIALOGUE::DialogueNode& node) {
                        return node.id == candidate;
                    })) {
                return candidate;
            }
        }
        return std::string(prefix) + "new";
    }

    std::string DialogueWorkspaceController::AllocateSpeakerId() const {
        const auto& style = styleDocument_.Style();
        for (uint32_t suffix = 1u; suffix < 100000u; ++suffix) {
            const std::string candidate = "character" +
                std::to_string(suffix);
            if (DIALOGUE::FindDialogueSpeaker(style, candidate) == nullptr) {
                return candidate;
            }
        }
        return "character_new";
    }

    void DialogueWorkspaceController::AddNode(
        DIALOGUE::DialogueNodeType type) {
        auto before = document_.Definition();
        auto& definition = document_.Definition();
        DIALOGUE::DialogueNode node{};
        node.type = type;
        node.id = AllocateNodeId(type == DIALOGUE::DialogueNodeType::End
            ? "end" : "node");
        if (type == DIALOGUE::DialogueNodeType::Line) {
            node.text.values[editingLocale_] = "New line";
            node.nextNodeId = definition.entryNodeId;
        }
        else if (type == DIALOGUE::DialogueNodeType::Choice) {
            DIALOGUE::DialogueChoice choice{};
            choice.id = "choice1";
            choice.text.values[editingLocale_] = "Choice";
            choice.nextNodeId = definition.entryNodeId;
            node.choices.push_back(std::move(choice));
        }
        else if (type == DIALOGUE::DialogueNodeType::Event) {
            node.eventId = "Dialogue.Event";
            node.nextNodeId = definition.entryNodeId;
        }
        definition.nodes.push_back(std::move(node));
        selectedNodeId_ = definition.nodes.back().id;
        selectedChoiceIndex_.reset();
        RecordMutation(std::move(before));
    }

    void DialogueWorkspaceController::AddSpeaker() {
        auto before = styleDocument_.Style();
        DIALOGUE::DialogueSpeakerProfile speaker{};
        speaker.id = AllocateSpeakerId();
        speaker.displayName.values[editingLocale_] = "New Character";
        styleDocument_.Style().speakers.push_back(std::move(speaker));
        selectedSpeakerId_ = styleDocument_.Style().speakers.back().id;
        RecordStyleMutation(std::move(before));
    }

    bool DialogueWorkspaceController::DeleteSelectedSpeaker(
        std::string& outMessage) {
        if (selectedSpeakerId_.empty()) {
            outMessage = "Select a character first";
            return false;
        }
        auto before = styleDocument_.Style();
        auto& speakers = styleDocument_.Style().speakers;
        const size_t previousSize = speakers.size();
        speakers.erase(
            std::remove_if(
                speakers.begin(),
                speakers.end(),
                [this](const DIALOGUE::DialogueSpeakerProfile& speaker) {
                    return speaker.id == selectedSpeakerId_;
                }),
            speakers.end());
        if (speakers.size() == previousSize) {
            outMessage = "Character was not found";
            return false;
        }
        selectedSpeakerId_.clear();
        RecordStyleMutation(std::move(before));
        outMessage = "Character removed from the project dialogue style";
        return true;
    }

    bool DialogueWorkspaceController::DeleteSelectedNode(
        std::string& outMessage) {
        auto& definition = document_.Definition();
        if (definition.nodes.size() <= 1u || selectedNodeId_.empty()) {
            outMessage = "A dialogue must keep at least one node";
            return false;
        }
        auto before = definition;
        definition.nodes.erase(
            std::remove_if(
                definition.nodes.begin(), definition.nodes.end(),
                [this](const DIALOGUE::DialogueNode& node) {
                    return node.id == selectedNodeId_;
                }),
            definition.nodes.end());
        if (definition.entryNodeId == selectedNodeId_) {
            definition.entryNodeId = definition.nodes.front().id;
        }
        selectedNodeId_ = definition.entryNodeId;
        selectedChoiceIndex_.reset();
        RecordMutation(std::move(before));
        outMessage = "Dialogue node deleted; repair invalid targets if needed";
        return true;
    }

    bool DialogueWorkspaceController::RenameSelectedNode(
        std::string newId,
        std::string& outMessage) {
        if (newId.empty()) {
            outMessage = "Node id cannot be empty";
            return false;
        }
        DIALOGUE::DialogueNode* selected = SelectedNode();
        if (selected == nullptr || selected->id == newId) return true;
        const auto& nodes = document_.Definition().nodes;
        if (std::any_of(
                nodes.begin(), nodes.end(),
                [&newId](const DIALOGUE::DialogueNode& node) {
                    return node.id == newId;
                })) {
            outMessage = "Node id must be unique";
            return false;
        }
        auto before = document_.Definition();
        auto& definition = document_.Definition();
        const std::string oldId = selected->id;
        selected->id = newId;
        if (definition.entryNodeId == oldId) {
            definition.entryNodeId = newId;
        }
        for (DIALOGUE::DialogueNode& node : definition.nodes) {
            if (node.nextNodeId == oldId) node.nextNodeId = newId;
            for (DIALOGUE::DialogueChoice& choice : node.choices) {
                if (choice.nextNodeId == oldId) {
                    choice.nextNodeId = newId;
                }
            }
        }
        selectedNodeId_ = std::move(newId);
        RecordMutation(std::move(before));
        outMessage = "Node id and references updated";
        return true;
    }

} // namespace HIKARI::EDITOR
