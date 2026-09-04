#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
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
            (void)RequestOpenAsset(
                scene.GetAssetDatabase(),
                *activation.dialogueAssetGuid,
                statusMessage_);
            activePage_ = DialogueWorkspacePage::Story;
        }
        NormalizeSelection();
        NormalizeSpeakerSelection();
    }

    DialogueWorkspaceResult DialogueWorkspaceController::Draw(
        DocumentSceneBase& scene,
        EditorCommandRouter& commandRouter) {
        DialogueWorkspaceResult result{};
        DrawDialogueWorkspaceWindows(
            *this, scene, commandRouter);
        result.workspaceRequest =
            std::move(deferredWorkspaceRequest_);
        deferredWorkspaceRequest_.reset();
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
            return SaveStyle(scene, outMessage);
        }
        return SaveDialogue(scene, outMessage);
    }

    bool DialogueWorkspaceController::SaveDialogue(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        if (!document_.IsOpen()) {
            outMessage = "Open or create a Dialogue asset first";
            return false;
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

    bool DialogueWorkspaceController::SaveStyle(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        if (!styleDocument_.IsOpen()) {
            outMessage = "Dialogue UI style is not open";
            return false;
        }
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

    bool DialogueWorkspaceController::SaveAll(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        if (styleDocument_.IsDirty() &&
            !SaveStyle(scene, outMessage)) {
            return false;
        }
        if (document_.IsDirty() &&
            !SaveDialogue(scene, outMessage)) {
            return false;
        }
        outMessage = "Dialogue documents saved";
        return true;
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

    bool DialogueWorkspaceController::ApplyNewAsset(
        std::string& outMessage) {
        document_.New();
        selectedNodeId_ = document_.Definition().entryNodeId;
        selectedChoiceIndex_.reset();
        outMessage = "New dialogue created";
        return true;
    }

    bool DialogueWorkspaceController::ApplyOpenAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        std::string& outMessage) {
        if (!assetGuid.IsValid()) {
            outMessage = "Select a Dialogue asset to open";
            return false;
        }
        if (!document_.Open(database, assetGuid, outMessage)) return false;
        projectRoot_ = database.GetProjectRoot();
        selectedNodeId_ = document_.Definition().entryNodeId;
        selectedChoiceIndex_.reset();
        outMessage = "Dialogue asset opened";
        return true;
    }

    DialogueDocumentTransitionIntent
        DialogueWorkspaceController::ToTransitionIntent(
            PendingDocumentAction action) noexcept {
        switch (action) {
        case PendingDocumentAction::NewAsset:
            return DialogueDocumentTransitionIntent::NewAsset;
        case PendingDocumentAction::OpenAsset:
            return DialogueDocumentTransitionIntent::OpenAsset;
        case PendingDocumentAction::CloseAsset:
            return DialogueDocumentTransitionIntent::CloseAsset;
        case PendingDocumentAction::Revert:
            return DialogueDocumentTransitionIntent::RevertAsset;
        case PendingDocumentAction::WorkspaceTransition:
            return DialogueDocumentTransitionIntent::LeaveWorkspace;
        case PendingDocumentAction::None:
        default:
            return DialogueDocumentTransitionIntent::None;
        }
    }

    bool DialogueWorkspaceController::QueueTransition(
        PendingAction action,
        bool dirty,
        const char* dirtyMessage,
        std::string& outMessage) {
        const DialogueDocumentTransitionRequest request =
            documentTransition_.Request(
                ToTransitionIntent(action.kind),
                dirty);
        switch (request) {
        case DialogueDocumentTransitionRequest::ApplyImmediately:
            return true;
        case DialogueDocumentTransitionRequest::ConfirmationRequired:
            pendingAction_ = std::move(action);
            outMessage = dirtyMessage;
            return false;
        case DialogueDocumentTransitionRequest::Busy:
            outMessage =
                "A Dialogue document decision is already pending";
            return false;
        case DialogueDocumentTransitionRequest::InvalidIntent:
        default:
            outMessage = "Dialogue document transition intent is invalid";
            return false;
        }
    }

    bool DialogueWorkspaceController::RequestNewAsset(
        std::string& outMessage) {
        if (!QueueTransition(
                { PendingDocumentAction::NewAsset, {}, {} },
                document_.IsDirty(),
                "Current dialogue has unsaved changes",
                outMessage)) {
            return false;
        }
        return ApplyNewAsset(outMessage);
    }

    bool DialogueWorkspaceController::RequestOpenAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        std::string& outMessage) {
        if (!assetGuid.IsValid()) {
            outMessage = "Select a Dialogue asset to open";
            return false;
        }
        if (document_.IsOpen() &&
            document_.GetAssetGuid() == assetGuid) {
            outMessage = "Dialogue asset is already open";
            return true;
        }
        if (!QueueTransition(
                {
                    PendingDocumentAction::OpenAsset,
                    assetGuid,
                    {},
                },
                document_.IsDirty(),
                "Current dialogue has unsaved changes",
                outMessage)) {
            return false;
        }
        return ApplyOpenAsset(database, assetGuid, outMessage);
    }

    bool DialogueWorkspaceController::RequestCloseAsset(
        std::string& outMessage) {
        if (!document_.IsOpen()) {
            outMessage = "No Dialogue asset is open";
            return true;
        }
        if (!QueueTransition(
                { PendingDocumentAction::CloseAsset, {}, {} },
                document_.IsDirty(),
                "Current dialogue has unsaved changes",
                outMessage)) {
            return false;
        }
        document_.Close();
        selectedNodeId_.clear();
        selectedChoiceIndex_.reset();
        outMessage = "Dialogue asset closed";
        return true;
    }

    bool DialogueWorkspaceController::RequestRevert(
        AssetDatabase& database,
        std::string& outMessage) {
        if (!document_.IsOpen() ||
            !document_.GetAssetGuid().IsValid()) {
            outMessage = "Only a saved Dialogue asset can be reverted";
            return false;
        }
        if (!QueueTransition(
                { PendingDocumentAction::Revert, {}, {} },
                document_.IsDirty(),
                "Reverting will discard unsaved dialogue changes",
                outMessage)) {
            return false;
        }
        const bool reverted = document_.Revert(database, outMessage);
        if (reverted) {
            selectedNodeId_ = document_.Definition().entryNodeId;
            selectedChoiceIndex_.reset();
        }
        return reverted;
    }

    bool DialogueWorkspaceController::RequestWorkspaceTransition(
        EditorWorkspaceOpenRequest request,
        std::string& outMessage) {
        if (request.workspaceId == EditorWorkspaceId::Dialogue) {
            outMessage = "Dialogue workspace is already active";
            return false;
        }
        if (!QueueTransition(
                {
                    PendingDocumentAction::WorkspaceTransition,
                    {},
                    request,
                },
                IsDocumentDirty(),
                "Dialogue documents have unsaved changes",
                outMessage)) {
            return false;
        }
        deferredWorkspaceRequest_ = std::move(request);
        outMessage = "Leaving Dialogue workspace";
        return true;
    }

    bool DialogueWorkspaceController::HasPendingConfirmation()
        const noexcept {
        return documentTransition_.HasPendingIntent();
    }

    bool DialogueWorkspaceController::ConsumePendingConfirmationOpenRequest()
        noexcept {
        return documentTransition_.ConsumeConfirmationOpenRequest();
    }

    bool DialogueWorkspaceController::PendingActionSupportsSave()
        const noexcept {
        return pendingAction_.kind != PendingDocumentAction::None &&
            pendingAction_.kind != PendingDocumentAction::Revert;
    }

    const char* DialogueWorkspaceController::GetPendingConfirmationText()
        const noexcept {
        switch (pendingAction_.kind) {
        case PendingDocumentAction::NewAsset:
            return "Save changes before creating a new Dialogue asset?";
        case PendingDocumentAction::OpenAsset:
            return "Save changes before opening another Dialogue asset?";
        case PendingDocumentAction::CloseAsset:
            return "Save changes before closing the Dialogue asset?";
        case PendingDocumentAction::Revert:
            return "Discard current changes and reload the saved Dialogue asset?";
        case PendingDocumentAction::WorkspaceTransition:
            return "Save Dialogue and UI style changes before leaving this workspace?";
        case PendingDocumentAction::None:
        default:
            return "Save changes to the current Dialogue documents?";
        }
    }

    bool DialogueWorkspaceController::ApplyPending(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        const DialogueDocumentTransitionIntent pendingIntent =
            ToTransitionIntent(pendingAction_.kind);
        if (pendingIntent == DialogueDocumentTransitionIntent::None ||
            pendingIntent != documentTransition_.GetIntent()) {
            pendingAction_ = {};
            documentTransition_.Cancel();
            outMessage = "Dialogue document transition intent was lost";
            return false;
        }
        if (!documentTransition_.BeginApply()) {
            outMessage = "Dialogue document transition is not ready";
            return false;
        }
        const PendingAction& action = pendingAction_;
        bool applied = false;
        switch (action.kind) {
        case PendingDocumentAction::NewAsset:
            applied = ApplyNewAsset(outMessage);
            break;
        case PendingDocumentAction::OpenAsset:
            applied = ApplyOpenAsset(
                scene.GetAssetDatabase(),
                action.assetGuid,
                outMessage);
            break;
        case PendingDocumentAction::CloseAsset:
            document_.Close();
            selectedNodeId_.clear();
            selectedChoiceIndex_.reset();
            outMessage = "Dialogue asset closed";
            applied = true;
            break;
        case PendingDocumentAction::Revert: {
            const bool reverted = document_.Revert(
                scene.GetAssetDatabase(), outMessage);
            if (reverted) {
                selectedNodeId_ = document_.Definition().entryNodeId;
                selectedChoiceIndex_.reset();
            }
            applied = reverted;
            break;
        }
        case PendingDocumentAction::WorkspaceTransition:
            if (!action.workspaceRequest) {
                outMessage = "Workspace transition target is missing";
                break;
            }
            deferredWorkspaceRequest_ = *action.workspaceRequest;
            outMessage = "Dialogue workspace transition confirmed";
            applied = true;
            break;
        case PendingDocumentAction::None:
        default:
            outMessage = "No pending Dialogue action";
            break;
        }
        if (!documentTransition_.CompleteApply(applied)) {
            outMessage = "Dialogue document transition lost its apply state";
            return false;
        }
        if (applied) {
            pendingAction_ = {};
        }
        return applied;
    }

    bool DialogueWorkspaceController::ConfirmSaveThenContinue(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        if (!HasPendingConfirmation()) {
            outMessage = "No pending Dialogue action";
            return false;
        }
        if (documentTransition_.GetState() ==
            DialogueDocumentTransitionState::ReadyToApply) {
            return ApplyPending(scene, outMessage);
        }
        if (!PendingActionSupportsSave()) {
            outMessage = "This Dialogue action only supports discard or cancel";
            return false;
        }
        if (!documentTransition_.BeginSave()) {
            outMessage = "Dialogue document transition is already resolving";
            return false;
        }
        const bool saveAll = pendingAction_.kind ==
            PendingDocumentAction::WorkspaceTransition;
        bool saved = false;
        if (saveAll) {
            saved = SaveAll(scene, outMessage);
        }
        else {
            saved = !document_.IsDirty() ||
                SaveDialogue(scene, outMessage);
        }
        if (!documentTransition_.CompleteSave(saved)) {
            outMessage = "Dialogue save transaction lost its state";
            return false;
        }
        if (!saved) return false;
        return ApplyPending(scene, outMessage);
    }

    bool DialogueWorkspaceController::ConfirmDiscardAndContinue(
        DocumentSceneBase& scene,
        std::string& outMessage) {
        if (!HasPendingConfirmation()) {
            outMessage = "No pending Dialogue action";
            return false;
        }
        if (documentTransition_.GetState() ==
            DialogueDocumentTransitionState::ReadyToApply) {
            return ApplyPending(scene, outMessage);
        }
        if (documentTransition_.GetState() !=
            DialogueDocumentTransitionState::AwaitingDecision) {
            outMessage = "Dialogue document transition is already resolving";
            return false;
        }
        if (pendingAction_.kind ==
                PendingDocumentAction::WorkspaceTransition) {
            if (document_.IsDirty()) {
                if (document_.GetAssetGuid().IsValid()) {
                    if (!document_.Revert(
                            scene.GetAssetDatabase(), outMessage)) {
                        return false;
                    }
                }
                else {
                    document_.Close();
                    selectedNodeId_.clear();
                    selectedChoiceIndex_.reset();
                }
            }
            if (styleDocument_.IsDirty()) {
                styleDocument_.DiscardChanges(
                    projectRoot_,
                    scene.GetAssetDatabase());
            }
        }
        if (!documentTransition_.ChooseDiscard()) {
            outMessage = "Dialogue document discard could not be resolved";
            return false;
        }
        return ApplyPending(scene, outMessage);
    }

    void DialogueWorkspaceController::CancelPending() noexcept {
        pendingAction_ = {};
        documentTransition_.Cancel();
    }

    std::vector<DialogueAssetListEntry>
        DialogueWorkspaceController::ListAssets(
            const AssetDatabase& database) const {
        std::vector<DialogueAssetListEntry> result{};
        for (const AssetRecord* record :
            database.CollectByType(AssetType::Dialogue)) {
            if (record == nullptr || !record->guid.IsValid()) {
                continue;
            }
            result.push_back(DialogueAssetListEntry{
                record->guid,
                record->displayName.empty()
                    ? record->sourcePath.stem().string()
                    : record->displayName,
                record->sourcePath,
                document_.IsOpen() &&
                    document_.GetAssetGuid() == record->guid,
            });
        }
        std::sort(
            result.begin(),
            result.end(),
            [](const DialogueAssetListEntry& lhs,
                const DialogueAssetListEntry& rhs) {
                if (lhs.name != rhs.name) {
                    return lhs.name < rhs.name;
                }
                return lhs.sourcePath.generic_string() <
                    rhs.sourcePath.generic_string();
            });
        return result;
    }

    bool DialogueWorkspaceController::IsDocumentOpen() const noexcept {
        return document_.IsOpen();
    }

    bool DialogueWorkspaceController::IsDocumentDirty() const noexcept {
        return document_.IsDirty() || styleDocument_.IsDirty();
    }

    const AssetGuid&
        DialogueWorkspaceController::GetDocumentAssetGuid() const noexcept {
        return document_.GetAssetGuid();
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
        const auto selected = std::find_if(
            definition.nodes.begin(),
            definition.nodes.end(),
            [this](const DIALOGUE::DialogueNode& candidate) {
                return candidate.id == selectedNodeId_;
            });
        const bool hasSelected = selected != definition.nodes.end();
        const size_t selectedIndex = hasSelected
            ? static_cast<size_t>(
                std::distance(definition.nodes.begin(), selected))
            : definition.nodes.size();
        std::optional<size_t> selectedChoice = selectedChoiceIndex_;
        if (hasSelected &&
            selected->type == DIALOGUE::DialogueNodeType::Choice &&
            (!selectedChoice ||
                *selectedChoice >= selected->choices.size()) &&
            selected->choices.size() == 1u) {
            selectedChoice = 0u;
        }
        std::string inheritedTarget{};
        if (hasSelected) {
            if (selected->type == DIALOGUE::DialogueNodeType::Line ||
                selected->type == DIALOGUE::DialogueNodeType::Event) {
                inheritedTarget = selected->nextNodeId;
            }
            else if (selected->type ==
                    DIALOGUE::DialogueNodeType::Choice &&
                selectedChoice &&
                *selectedChoice < selected->choices.size()) {
                inheritedTarget =
                    selected->choices[*selectedChoice].nextNodeId;
            }
        }
        if (inheritedTarget.empty() ||
            inheritedTarget == selectedNodeId_ ||
            DIALOGUE::FindDialogueNode(
                definition, inheritedTarget) == nullptr) {
            inheritedTarget.clear();
            if (hasSelected && selectedIndex + 1u <
                    definition.nodes.size()) {
                inheritedTarget =
                    definition.nodes[selectedIndex + 1u].id;
            }
            else {
                const auto end = std::find_if(
                    definition.nodes.begin(),
                    definition.nodes.end(),
                    [this](const DIALOGUE::DialogueNode& candidate) {
                        return candidate.id != selectedNodeId_ &&
                            candidate.type ==
                                DIALOGUE::DialogueNodeType::End;
                    });
                if (end != definition.nodes.end()) {
                    inheritedTarget = end->id;
                }
            }
        }

        DIALOGUE::DialogueNode node{};
        node.type = type;
        node.id = AllocateNodeId(type == DIALOGUE::DialogueNodeType::End
            ? "end" : "node");
        const std::string newNodeId = node.id;
        if (type == DIALOGUE::DialogueNodeType::Line) {
            node.text.values[editingLocale_] = "New line";
            node.nextNodeId = inheritedTarget;
        }
        else if (type == DIALOGUE::DialogueNodeType::Choice) {
            DIALOGUE::DialogueChoice choice{};
            choice.id = "choice1";
            choice.text.values[editingLocale_] = "Choice";
            choice.nextNodeId = inheritedTarget;
            node.choices.push_back(std::move(choice));
        }
        else if (type == DIALOGUE::DialogueNodeType::Event) {
            node.eventId = "Dialogue.Event";
            node.nextNodeId = inheritedTarget;
        }

        const size_t insertIndex = hasSelected
            ? selectedIndex + 1u
            : definition.nodes.size();
        definition.nodes.insert(
            definition.nodes.begin() + insertIndex,
            std::move(node));

        bool autoBound = false;
        if (autoBindNewNodes_ && hasSelected) {
            DIALOGUE::DialogueNode& predecessor =
                definition.nodes[selectedIndex];
            if (predecessor.type == DIALOGUE::DialogueNodeType::Line ||
                predecessor.type == DIALOGUE::DialogueNodeType::Event) {
                predecessor.nextNodeId = newNodeId;
                autoBound = true;
            }
            else if (predecessor.type ==
                    DIALOGUE::DialogueNodeType::Choice &&
                selectedChoice &&
                *selectedChoice < predecessor.choices.size()) {
                predecessor.choices[*selectedChoice].nextNodeId =
                    newNodeId;
                autoBound = true;
            }
        }

        selectedNodeId_ = newNodeId;
        selectedChoiceIndex_.reset();
        RecordMutation(std::move(before));
        statusMessage_ = autoBound
            ? "Story step inserted and linked after the previous step"
            : "Story step inserted after the selected step";
    }

    bool DialogueWorkspaceController::CanBindSelectedToNextNode()
        const noexcept {
        if (!document_.IsOpen()) {
            return false;
        }
        const auto& nodes = document_.Definition().nodes;
        const auto selected = std::find_if(
            nodes.begin(),
            nodes.end(),
            [this](const DIALOGUE::DialogueNode& candidate) {
                return candidate.id == selectedNodeId_;
            });
        if (selected == nodes.end() ||
            std::next(selected) == nodes.end()) {
            return false;
        }
        if (selected->type == DIALOGUE::DialogueNodeType::Line ||
            selected->type == DIALOGUE::DialogueNodeType::Event) {
            return true;
        }
        if (selected->type != DIALOGUE::DialogueNodeType::Choice) {
            return false;
        }
        return (selectedChoiceIndex_ &&
                *selectedChoiceIndex_ < selected->choices.size()) ||
            selected->choices.size() == 1u;
    }

    bool DialogueWorkspaceController::BindSelectedToNextNode(
        std::string& outMessage) {
        if (!document_.IsOpen()) {
            outMessage = "Open or create a Dialogue asset first";
            return false;
        }
        auto& definition = document_.Definition();
        const auto selected = std::find_if(
            definition.nodes.begin(),
            definition.nodes.end(),
            [this](const DIALOGUE::DialogueNode& candidate) {
                return candidate.id == selectedNodeId_;
            });
        if (selected == definition.nodes.end()) {
            outMessage = "Select a story step first";
            return false;
        }
        const auto next = std::next(selected);
        if (next == definition.nodes.end()) {
            outMessage = "The selected step is already the final list item";
            return false;
        }

        std::string* target = nullptr;
        if (selected->type == DIALOGUE::DialogueNodeType::Line ||
            selected->type == DIALOGUE::DialogueNodeType::Event) {
            target = &selected->nextNodeId;
        }
        else if (selected->type == DIALOGUE::DialogueNodeType::Choice) {
            size_t choiceIndex = selected->choices.size();
            if (selectedChoiceIndex_ &&
                *selectedChoiceIndex_ < selected->choices.size()) {
                choiceIndex = *selectedChoiceIndex_;
            }
            else if (selected->choices.size() == 1u) {
                choiceIndex = 0u;
            }
            if (choiceIndex < selected->choices.size()) {
                target = &selected->choices[choiceIndex].nextNodeId;
            }
        }
        if (target == nullptr) {
            outMessage = selected->type ==
                    DIALOGUE::DialogueNodeType::Choice
                ? "Select the Choice option to bind"
                : "End steps cannot continue to another step";
            return false;
        }
        if (*target == next->id) {
            outMessage = "Selected step is already linked to the next step";
            return true;
        }

        auto before = definition;
        *target = next->id;
        const std::string targetId = next->id;
        RecordMutation(std::move(before));
        outMessage = "Linked selected step to " + targetId;
        return true;
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
