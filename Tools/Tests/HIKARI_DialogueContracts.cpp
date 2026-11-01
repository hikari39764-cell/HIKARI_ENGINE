#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Internal/HIKARI_ContractAssert.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"
#include "Gameplay/Dialogue/HIKARI_DialoguePresentation.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Render3D/Ui/HIKARI_GameUiSignedDistanceField.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

namespace {

using namespace HIKARI::DIALOGUE;

bool HasError(const std::vector<DialogueValidationIssue>& issues) {
    for (const DialogueValidationIssue& issue : issues) {
        if (issue.error)
            return true;
    }
    return false;
}

DialogueTheme MakeTheme() {
    DialogueTheme theme;
    theme.speakerTextStyle = {"11111111111111111111111111111111"};
    theme.bodyTextStyle = {"22222222222222222222222222222222"};
    theme.choiceTextStyle = {"33333333333333333333333333333333"};
    theme.dialoguePanelAssetPath = "UI/Dialogue/Dialog_Box.png";
    theme.speakerPlateAssetPath = "UI/Dialogue/Dialog_Name.png";
    theme.choiceAssetPath = "UI/Dialogue/Choose_Back.png";
    theme.advanceGamepadAssetPath = "UI/Dialogue/Continue_Gamepad.png";
    theme.advanceMouseAssetPath = "UI/Dialogue/Continue_Mouse.png";
    theme.advancePromptX = 1660.0f;
    theme.advancePromptY = 968.0f;
    theme.advancePromptScale = 1.15f;
    theme.advancePromptPulseSeconds = 0.82f;
    theme.advancePromptBobDistance = 9.0f;
    theme.advancePromptScaleAmount = 0.08f;
    theme.bodyTextRotationDegrees = -2.5f;
    theme.bodyTextScale = 1.2f;
    theme.choiceTextOffsetX = 28.0f;
    theme.choiceTextOffsetY = 10.0f;
    theme.choiceTextWidth = 420.0f;
    theme.choiceTextHeight = 66.0f;

    return theme;
}
DialogueDefinition MakeValidDialogue() {
    DialogueDefinition definition{};
    definition.name = "Contract Dialogue";
    definition.entryNodeId = "entry";
    definition.fallbackLocale = "en-US";

    DialogueNode line{};
    line.id = "entry";
    line.type = DialogueNodeType::Line;
    line.speakerId = "guide";
    line.portraitSlot = DialoguePortraitSlot::Left;
    line.portraitAction = DialoguePortraitAction::Show;
    line.speaker.values.emplace("ja-JP", "案内人");
    line.speaker.values.emplace("en-US", "Guide");
    line.text.values.emplace("ja-JP", "...");
    line.text.values.emplace("en-US", "Are you ready?");
    line.nextNodeId = "choice";

    DialogueNode choice{};
    choice.id = "choice";
    choice.type = DialogueNodeType::Choice;
    choice.text.values.emplace("en-US", "Choose");
    DialogueChoice yes{};
    yes.id = "yes";
    yes.text.values.emplace("en-US", "Yes");
    yes.nextNodeId = "end";
    choice.choices.push_back(std::move(yes));

    DialogueNode end{};
    end.id = "end";
    end.type = DialogueNodeType::End;
    definition.nodes.push_back(std::move(line));
    definition.nodes.push_back(std::move(choice));
    definition.nodes.push_back(std::move(end));
    return definition;
}

} // namespace

int main(int, char** argv) {
    using namespace HIKARI::DIALOGUE;

    {
        using namespace HIKARI;
        using namespace HIKARI::EDITOR;
        struct WorkflowScene final : DocumentSceneBase {
            WorkflowScene() : DocumentSceneBase("dialogue-workflow") {}
            const char* GetSceneName() const override { return "Dialogue workflow"; }
        } scene;
        const auto root =
            std::filesystem::absolute(TEXT::PathFromUtf8(argv[0])).parent_path() / "dialogue-workflow-contract";
        auto& database = scene.GetAssetDatabase();
        assert(database.Initialize(root));
        DialogueWorkspaceController controller;
        std::string message;
        assert(controller.RequestNewAsset(message) && controller.IsDocumentDirty());
        assert(!controller.RequestCloseAsset(message) && controller.HasPendingConfirmation());
        assert(controller.ConsumePendingConfirmationOpenRequest());
        assert(!controller.ConsumePendingConfirmationOpenRequest());
        assert(!controller.RequestNewAsset(message));
        controller.CancelPending();
        assert(controller.IsDocumentDirty() && !controller.HasPendingConfirmation());
        assert(!controller.ConfirmDiscardAndContinue(scene, message));

        // ソース書き込み失敗でも草稿と要求を保持し、修復後に保存・続行できる。
        const auto blocker = root / "Assets/Dialogues";
        const auto savedDirectory = root / "Assets/Dialogues-held";
        const bool held = std::filesystem::exists(blocker);
        if (held)
            std::filesystem::rename(blocker, savedDirectory);
        {
            std::ofstream file(blocker);
            file << "blocked";
        }
        assert(!controller.RequestCloseAsset(message));
        assert(!controller.ConfirmSaveThenContinue(scene, message));
        assert(controller.HasPendingConfirmation() && controller.IsDocumentDirty());
        std::filesystem::remove(blocker);
        if (held)
            std::filesystem::rename(savedDirectory, blocker);
        assert(controller.ConfirmSaveThenContinue(scene, message));
        assert(!controller.IsDocumentOpen() && !controller.HasPendingConfirmation());

        const auto records = database.CollectByType(AssetType::Dialogue);
        assert(!records.empty());
        const auto guid = records.back()->guid;
        const auto path = root / records.back()->sourcePath;
        std::ifstream file(path, std::ios::binary);
        const std::string saved{std::istreambuf_iterator<char>(file), {}};
        file.close();
        {
            std::ofstream broken(path);
            broken << "{";
        }
        assert(controller.RequestNewAsset(message));
        assert(!controller.RequestOpenAsset(database, guid, message));
        assert(!controller.ConfirmDiscardAndContinue(scene, message));
        assert(controller.HasPendingConfirmation() && controller.IsDocumentDirty());
        assert(!controller.RequestCloseAsset(message));
        {
            std::ofstream restored(path, std::ios::binary);
            restored << saved;
        }
        const auto count = database.CollectByType(AssetType::Dialogue).size();
        // discard 確認後の再試行は既に決めた操作のみ実行し、別の新規資産を保存しない。
        assert(controller.ConfirmSaveThenContinue(scene, message));
        assert(controller.GetDocumentAssetGuid() == guid && !controller.IsDocumentDirty());
        assert(database.CollectByType(AssetType::Dialogue).size() == count);
        assert(controller.RequestRevert(database, message));
        assert(controller.RequestNewAsset(message));
        assert(!controller.RequestWorkspaceTransition({EditorWorkspaceId::Scene}, message));
        assert(controller.ConfirmDiscardAndContinue(scene, message));
        assert(!controller.HasPendingConfirmation() && !controller.IsDocumentOpen());
    }

    DialogueDefinition definition = MakeValidDialogue();
    NormalizeDialogue(definition);
    if (HasError(ValidateDialogue(definition)))
        return EXIT_FAILURE;

    const DialogueNode* entry = FindDialogueNode(definition, "entry");
    if (entry == nullptr)
        return EXIT_FAILURE;
    if (ResolveLocalizedText(entry->text, "ja", definition.fallbackLocale) != "...") {
        return EXIT_FAILURE;
    }
    if (ResolveLocalizedText(entry->text, "fr-FR", definition.fallbackLocale) != "Are you ready?") {
        return EXIT_FAILURE;
    }

    nlohmann::json payload{};
    SerializeDialogueJson(definition, payload);
    if (payload.contains("theme"))
        return EXIT_FAILURE;
    auto embedded = payload;
    embedded["theme"] = nlohmann::json::object();
    DialogueDefinition rejected;
    if (DeserializeDialogueJson(embedded, rejected))
        return EXIT_FAILURE;
    DialogueDefinition roundTrip{};
    std::string error{};
    if (!DeserializeDialogueJson(payload, roundTrip, &error)) {
        return EXIT_FAILURE;
    }
    if (roundTrip.nodes.front().speakerId != "guide" ||
        roundTrip.nodes.front().portraitSlot != DialoguePortraitSlot::Left ||
        roundTrip.nodes.front().portraitAction != DialoguePortraitAction::Show ||
        HasError(ValidateDialogue(roundTrip))) {
        return EXIT_FAILURE;
    }

    nlohmann::json themePayload{};
    SerializeDialogueThemeJson(MakeTheme(), themePayload);
    DialogueTheme themeRoundTrip{};
    if (!DeserializeDialogueThemeJson(themePayload, themeRoundTrip) ||
        themeRoundTrip.dialoguePanelAssetPath != "UI/Dialogue/Dialog_Box.png" ||
        themeRoundTrip.advanceGamepadAssetPath != "UI/Dialogue/Continue_Gamepad.png" ||
        themeRoundTrip.advanceMouseAssetPath != "UI/Dialogue/Continue_Mouse.png" ||
        themeRoundTrip.advancePromptX != 1660.0f || themeRoundTrip.advancePromptY != 968.0f ||
        themeRoundTrip.advancePromptScale != 1.15f || themeRoundTrip.advancePromptPulseSeconds != 0.82f ||
        themeRoundTrip.advancePromptBobDistance != 9.0f || themeRoundTrip.advancePromptScaleAmount != 0.08f ||
        themeRoundTrip.bodyTextRotationDegrees != -2.5f || themeRoundTrip.bodyTextScale != 1.2f ||
        themeRoundTrip.bodyTextStyle.value != "22222222222222222222222222222222" ||
        themeRoundTrip.choiceTextOffsetX != 28.0f || themeRoundTrip.choiceTextWidth != 420.0f ||
        HasError(ValidateDialogueTheme(themeRoundTrip))) {
        return EXIT_FAILURE;
    }

    DialogueProjectStyle projectStyle{};
    projectStyle.theme = MakeTheme();
    DialogueSpeakerProfile guide{};
    guide.id = "guide";
    guide.displayName.values["ja-JP"] = "案内人";
    guide.displayName.values["en-US"] = "Guide";
    guide.portraitAssetGuid = "portrait-guid";
    guide.preferredSlot = DialoguePortraitSlot::Left;
    guide.offsetX = 14.0f;
    guide.scale = 1.1f;
    projectStyle.speakers.push_back(guide);
    NormalizeDialogueProjectStyle(projectStyle);
    if (HasError(ValidateDialogueProjectStyle(projectStyle))) {
        return EXIT_FAILURE;
    }
    nlohmann::json stylePayload{};
    SerializeDialogueProjectStyleJson(projectStyle, stylePayload);
    DialogueProjectStyle styleRoundTrip{};
    if (!DeserializeDialogueProjectStyleJson(stylePayload, styleRoundTrip, &error) ||
        styleRoundTrip.speakers.size() != 1u || styleRoundTrip.speakers.front().id != "guide" ||
        styleRoundTrip.speakers.front().portraitAssetGuid != "portrait-guid" ||
        styleRoundTrip.speakers.front().preferredSlot != DialoguePortraitSlot::Left ||
        styleRoundTrip.speakers.front().offsetX != 14.0f || styleRoundTrip.speakers.front().scale != 1.1f ||
        !styleRoundTrip.speakers.front().resolvedPortraitAssetPath.empty() ||
        HasError(ValidateDialogueProjectStyle(styleRoundTrip))) {
        return EXIT_FAILURE;
    }

    const DialoguePortraitVisualIdentity character2Visual{
        "9cb18ab239711258691e4263fb90818e", "UI/Dialogue/Character2.png", 0.0f, 0.0f, 1.0f, 0.0f, false};
    const DialoguePortraitVisualIdentity character3Visual = character2Visual;
    if (!SameDialoguePortraitVisualIdentity(character2Visual, character3Visual)) {
        return EXIT_FAILURE;
    }
    const DialoguePortraitVisualIdentity mirroredVariant{character2Visual.assetGuid,
                                                         character2Visual.assetPath,
                                                         character2Visual.offsetX,
                                                         character2Visual.offsetY,
                                                         character2Visual.scale,
                                                         character2Visual.rotationDegrees,
                                                         true};
    if (SameDialoguePortraitVisualIdentity(character2Visual, mirroredVariant)) {
        return EXIT_FAILURE;
    }
    styleRoundTrip.speakers.push_back(styleRoundTrip.speakers.front());
    if (!HasError(ValidateDialogueProjectStyle(styleRoundTrip))) {
        return EXIT_FAILURE;
    }

    roundTrip.nodes.front().nextNodeId = "missing";
    if (!HasError(ValidateDialogue(roundTrip)))
        return EXIT_FAILURE;

    DialogueTheme invalidRotation = MakeTheme();
    invalidRotation.choiceTextRotationDegrees = std::numeric_limits<float>::infinity();
    if (!HasError(ValidateDialogueTheme(invalidRotation))) {
        return EXIT_FAILURE;
    }
    NormalizeDialogueTheme(invalidRotation);
    if (HasError(ValidateDialogueTheme(invalidRotation)) || invalidRotation.choiceTextRotationDegrees != 0.0f) {
        return EXIT_FAILURE;
    }

    const uint8_t coverage[9]{
        0u, 64u, 0u, 64u, 255u, 64u, 0u, 64u, 0u,
    };
    std::vector<uint8_t> signedDistance{};
    int sdfWidth = 0;
    int sdfHeight = 0;
    if (!HIKARI::GAME_UI::BuildGameUiSignedDistanceField(coverage, 3, 3, 2, 128u, 16.0f, signedDistance, sdfWidth,
                                                         sdfHeight) ||
        sdfWidth != 7 || sdfHeight != 7 || signedDistance.size() != 49u || signedDistance[3u * 7u + 3u] <= 128u ||
        signedDistance.front() >= 128u) {
        return EXIT_FAILURE;
    }

    const std::filesystem::path uiProject =
        std::filesystem::absolute(HIKARI::TEXT::PathFromUtf8(argv[0])).parent_path() /
        HIKARI::TEXT::PathFromUtf8("DialogueUiContractProject-\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88");
    const std::filesystem::path uiAsset = uiProject / "Assets" / "UI" / "placeholder.png";
    const std::filesystem::path uiFont =
        uiProject / "Assets" / HIKARI::TEXT::PathFromUtf8("Fonts/\xE9\x9D\x9E\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.ttf");
    std::filesystem::create_directories(uiAsset.parent_path());
    std::filesystem::create_directories(uiFont.parent_path());
    {
        std::ofstream placeholder(uiAsset, std::ios::binary);
        placeholder.put('\0');
    }
    {
        std::ofstream font(uiFont, std::ios::binary);
        font.put('\0');
    }
    HIKARI::GAME_UI::GameUiPresentationService ui{};
    ui.SetProjectRoot(uiProject);
    HIKARI::GameProjectUiSpriteRequest ordinary{};
    ordinary.sourceId = 1u;
    ordinary.width = 10.0f;
    ordinary.height = 10.0f;
    if (!ui.SubmitSprite("UI/placeholder.png", ordinary)) {
        return EXIT_FAILURE;
    }
    HIKARI::GameProjectUiSpriteRequest modal = ordinary;
    modal.sourceId = 2u;
    modal.flags = HIKARI::kGameProjectUiSubmissionModal;
    if (!ui.SubmitSprite("UI/placeholder.png", modal)) {
        return EXIT_FAILURE;
    }
    HIKARI::GameProjectUiSpriteRequest portrait = modal;
    portrait.sourceId = 3u;
    portrait.effect = HIKARI::GameProjectUiEffect::DialoguePortraitFocus;
    if (!ui.SubmitSprite("UI/placeholder.png", portrait)) {
        return EXIT_FAILURE;
    }
    HIKARI::GameProjectUiBackdropRequest backdrop{};
    backdrop.sourceId = 4u;
    backdrop.flags = HIKARI::kGameProjectUiSuppressNonModal;
    if (!ui.SubmitBackdrop(backdrop))
        return EXIT_FAILURE;
    ui.Tick(42u);
    const HIKARI::GAME_UI::GameUiFrameSource& uiFrame = ui.GetFrameSource();
    if (uiFrame.sprites.size() != 2u || uiFrame.sprites.front().request.sourceId != modal.sourceId ||
        uiFrame.sprites.back().request.sourceId != portrait.sourceId ||
        uiFrame.sprites.back().request.effect != HIKARI::GameProjectUiEffect::DialoguePortraitFocus ||
        !uiFrame.hasBackdrop) {
        return EXIT_FAILURE;
    }
    HIKARI::GameProjectUiTextRequest textRequest{};
    textRequest.sourceId = 5u;
    const std::string unicodeFontAssetPath = "Fonts/\xE9\x9D\x9E\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.ttf";
    if (!ui.SubmitText(unicodeFontAssetPath, "UTF-8 font path", textRequest)) {
        return EXIT_FAILURE;
    }
    ui.Tick(43u);
    const HIKARI::GAME_UI::GameUiFrameSource& textFrame = ui.GetFrameSource();
    if (textFrame.texts.size() != 1u ||
        textFrame.texts.front().absoluteFontAssetPath != HIKARI::TEXT::PathToUtf8(uiFont)) {
        return EXIT_FAILURE;
    }
    std::error_code cleanupError{};
    std::filesystem::remove_all(uiProject, cleanupError);

    return EXIT_SUCCESS;
}
