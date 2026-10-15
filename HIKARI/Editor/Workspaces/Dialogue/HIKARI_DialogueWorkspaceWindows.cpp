#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialoguePreviewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace HIKARI::EDITOR {

namespace {
constexpr ImVec4 kAccent{0.20f, 0.58f, 1.0f, 1.0f};
constexpr ImVec4 kModified{1.0f, 0.72f, 0.25f, 1.0f};
constexpr ImVec4 kError{1.0f, 0.38f, 0.35f, 1.0f};
constexpr ImVec4 kReady{0.36f, 0.86f, 0.58f, 1.0f};

uint64_t MergeGroup(std::string_view field) noexcept {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char character : field) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash != 0u ? hash : 1u;
}

bool ContainsCaseInsensitive(std::string_view value, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    const auto lower = [](unsigned char character) { return static_cast<char>(std::tolower(character)); };
    std::string loweredValue(value);
    std::string loweredFilter(filter);
    std::transform(loweredValue.begin(), loweredValue.end(), loweredValue.begin(), lower);
    std::transform(loweredFilter.begin(), loweredFilter.end(), loweredFilter.begin(), lower);
    return loweredValue.find(loweredFilter) != std::string::npos;
}

bool InputString(const char* label, std::string& value, bool multiline = false, size_t minimumCapacity = 512u,
                 float multilineHeight = 150.0f) {
    const size_t capacity = (std::min)((std::max)(minimumCapacity, value.size() + 512u), size_t{65536u});
    std::vector<char> buffer(capacity, '\0');
    if (!value.empty()) {
        std::memcpy(buffer.data(), value.data(), (std::min)(value.size(), capacity - 1u));
    }
    const bool changed =
        multiline ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), ImVec2(-1.0f, multilineHeight))
                  : ImGui::InputText(label, buffer.data(), buffer.size());
    if (changed)
        value.assign(buffer.data());
    return changed;
}

std::string_view ResolveName(const DIALOGUE::DialogueSpeakerProfile& speaker, std::string_view locale,
                             std::string_view fallback) {
    const std::string_view name = DIALOGUE::ResolveLocalizedText(speaker.displayName, locale, fallback);
    return name.empty() ? std::string_view(speaker.id) : name;
}

std::string NodeSummary(const DIALOGUE::DialogueNode& node, const DIALOGUE::DialogueProjectStyle& style,
                        std::string_view locale, std::string_view fallback) {
    if (node.type == DIALOGUE::DialogueNodeType::End)
        return TEXT::Localize("ui.vfx.text.End");
    if (node.type == DIALOGUE::DialogueNodeType::Event) {
        return node.eventId.empty() ? TEXT::Localize("editor.ui.event.ad8919")
                                    : TEXT::Localize("editor.ui.event.ad8919") + "  |  " + node.eventId;
    }
    if (node.type == DIALOGUE::DialogueNodeType::Choice) {
        return TEXT::Localize("editor.ui.choice.0682be") + "  |  " + std::to_string(node.choices.size()) + " " +
               TEXT::Localize("editor.ui.branches.52a9ce");
    }
    std::string prefix = TEXT::Localize("editor.ui.narration.8ca626");
    if (const auto* speaker = DIALOGUE::FindDialogueSpeaker(style, node.speakerId)) {
        prefix = std::string(ResolveName(*speaker, locale, fallback));
    }
    std::string_view text = DIALOGUE::ResolveLocalizedText(node.text, locale, fallback);
    constexpr size_t kSummaryLimit = 42u;
    std::string excerpt(text.substr(0u, (std::min)(text.size(), kSummaryLimit)));
    if (text.size() > kSummaryLimit)
        excerpt += "...";
    return prefix + (excerpt.empty() ? std::string{} : "  |  " + excerpt);
}

bool TargetCombo(const char* label, const DIALOGUE::DialogueDefinition& definition,
                 const DIALOGUE::DialogueProjectStyle& style, std::string_view locale, std::string& target) {
    const DIALOGUE::DialogueNode* current = DIALOGUE::FindDialogueNode(definition, target);
    const std::string preview = current != nullptr ? NodeSummary(*current, style, locale, definition.fallbackLocale)
                                                   : TEXT::Localize("editor.ui.choose.next.step.e15d49");
    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const DIALOGUE::DialogueNode& node : definition.nodes) {
            const bool selected = node.id == target;
            const std::string summary = NodeSummary(node, style, locale, definition.fallbackLocale);
            ImGui::PushID(node.id.c_str());
            if (ImGui::Selectable(summary.c_str(), selected)) {
                target = node.id;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string AssetRelativePath(const AssetDatabase& database, const AssetRecord& record) {
    std::filesystem::path source = record.sourcePath;
    if (!source.is_absolute())
        source = database.GetProjectRoot() / source;
    std::error_code error{};
    const std::filesystem::path relative =
        std::filesystem::relative(source.lexically_normal(), database.GetAssetsRoot(), error);
    if (error || relative.empty() || relative.has_root_path())
        return {};
    for (const auto& part : relative) {
        if (part == "..")
            return {};
    }
    return relative.generic_string();
}

bool DrawTextureAssetField(const char* label, const AssetDatabase& database, std::string& assetPath) {
    std::string guid{};
    for (const AssetRecord* record : database.CollectByType(AssetType::Texture)) {
        if (record != nullptr && AssetRelativePath(database, *record) == assetPath) {
            guid = record->guid.value;
            break;
        }
    }
    AssetFieldOptions options{};
    options.label = label;
    options.requiredType = AssetType::Texture;
    options.allowCopyGuid = false;
    options.allowLocate = false;
    if (!DrawAssetField(&database, options, guid))
        return false;
    if (guid.empty()) {
        assetPath.clear();
        return true;
    }
    const AssetRecord* record = database.FindByGuid(AssetGuid{guid});
    if (record == nullptr || record->type != AssetType::Texture) {
        return false;
    }
    assetPath = AssetRelativePath(database, *record);
    return true;
}

bool DrawLocalizedText(const char* label, DIALOGUE::LocalizedText& text, std::string_view locale,
                       bool multiline = false, float height = 150.0f) {
    std::string value{};
    if (const auto found = text.values.find(std::string(locale)); found != text.values.end()) {
        value = found->second;
    }
    if (!InputString(label, value, multiline, multiline ? 4096u : 512u, height)) {
        return false;
    }
    text.values[std::string(locale)] = std::move(value);
    return true;
}

bool DrawColor(const char* label, DIALOGUE::DialogueColor& color) {
    float values[4]{color.r, color.g, color.b, color.a};
    if (!ImGui::ColorEdit4(label, values, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar)) {
        return false;
    }
    color = {values[0], values[1], values[2], values[3]};
    return true;
}

bool DrawFullWidthSelectable(const char* label, bool selected, float height = 0.0f) {
    // Selectable intentionally does not support negative widths.
    // A zero width is its documented fill-available-width contract.
    return ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
}

const char* StyleSectionLabel(DialogueStyleSection section) {
    switch (section) {
    case DialogueStyleSection::Canvas: return TEXT::Localize("editor.ui.canvas.artwork.74a94b").c_str();
    case DialogueStyleSection::Typography: return TEXT::Localize("editor.ui.typography.f4beb9").c_str();
    case DialogueStyleSection::Portraits: return TEXT::Localize("editor.ui.portrait.layout.216ada").c_str();
    case DialogueStyleSection::Characters: return TEXT::Localize("editor.ui.characters.b6f59f").c_str();
    case DialogueStyleSection::Motion: return TEXT::Localize("editor.ui.motion.backdrop.97e967").c_str();
    default: return TEXT::Localize("editor.ui.canvas.artwork.74a94b").c_str();
    }
}

const char* StyleSectionNavigationLabel(DialogueStyleSection section) {
    switch (section) {
    case DialogueStyleSection::Canvas: return TEXT::Localize("editor.ui.1.canvas.artwork.cd3827").c_str();
    case DialogueStyleSection::Typography: return TEXT::Localize("editor.ui.2.typography.40c242").c_str();
    case DialogueStyleSection::Portraits: return TEXT::Localize("editor.ui.3.portrait.layout.eb1372").c_str();
    case DialogueStyleSection::Characters: return TEXT::Localize("editor.ui.4.characters.0b3db3").c_str();
    case DialogueStyleSection::Motion: return TEXT::Localize("editor.ui.5.motion.backdrop.44cfe9").c_str();
    default: return TEXT::Localize("editor.ui.1.canvas.artwork.cd3827").c_str();
    }
}

const char* StyleSectionDescription(DialogueStyleSection section) {
    switch (section) {
    case DialogueStyleSection::Canvas:
        return TEXT::Localize("editor.ui.choose.the.dialogue.name.and.choice.artwork.then.position.it.on..368953")
            .c_str();
    case DialogueStyleSection::Typography:
        return TEXT::Localize("editor.ui.choose.one.project.font.and.author.readable.name.body.and.choice.1524d2")
            .c_str();
    case DialogueStyleSection::Portraits:
        return TEXT::Localize("editor.ui.place.the.two.portrait.slots.used.by.every.dialogue.event.b88fe6").c_str();
    case DialogueStyleSection::Characters:
        return TEXT::Localize("editor.ui.register.reusable.names.portraits.and.per.character.alignment.99128c").c_str();
    case DialogueStyleSection::Motion:
        return TEXT::Localize("editor.ui.tune.reveal.portrait.focus.and.the.treatment.of.the.game.view.be.1b7ccb")
            .c_str();
    default: return TEXT::Localize("editor.ui.build.the.project.wide.dialogue.presentation.51ee01").c_str();
    }
}

const char* NodeTypeLabel(DIALOGUE::DialogueNodeType type) {
    switch (type) {
    case DIALOGUE::DialogueNodeType::Line: return TEXT::Localize("editor.ui.line.528a45").c_str();
    case DIALOGUE::DialogueNodeType::Choice: return TEXT::Localize("editor.ui.choice.20d9bc").c_str();
    case DIALOGUE::DialogueNodeType::Event: return TEXT::Localize("editor.ui.signal.289e7c").c_str();
    case DIALOGUE::DialogueNodeType::End: return TEXT::Localize("editor.ui.end.d205ab").c_str();
    default: return TEXT::Localize("editor.ui.step.0c9b85").c_str();
    }
}

// Canvas 上で直接編集できる要素。後ろほど手前に描かれ、クリック判定も優先する。
constexpr std::pair<DialoguePreviewElement, const char*> kPreviewElements[]{
    {DialoguePreviewElement::LeftPortrait, "Left portrait"}, {DialoguePreviewElement::RightPortrait, "Right portrait"},
    {DialoguePreviewElement::Panel, "Dialogue panel"},       {DialoguePreviewElement::SpeakerPlate, "Name plate"},
    {DialoguePreviewElement::ChoicePanel, "Choice"},         {DialoguePreviewElement::SpeakerText, "Name text"},
    {DialoguePreviewElement::BodyText, "Dialogue text"},     {DialoguePreviewElement::ChoiceText, "Choice text"},
};

// 要素の x / y / width / height 欄。ChoiceText の位置は Choice パネルからの offset。
template <class Style>
auto PreviewElementFields(Style& style, DialoguePreviewElement element) noexcept {
    auto& theme = style.theme;
    using Fields = std::array<decltype(&theme.panelX), 4u>;
    switch (element) {
    case DialoguePreviewElement::Panel:
        return Fields{&theme.panelX, &theme.panelY, &theme.panelWidth, &theme.panelHeight};
    case DialoguePreviewElement::SpeakerPlate:
        return Fields{&theme.speakerPlateX, &theme.speakerPlateY, &theme.speakerPlateWidth, &theme.speakerPlateHeight};
    case DialoguePreviewElement::SpeakerText:
        return Fields{&theme.speakerTextX, &theme.speakerTextY, &theme.speakerTextWidth, &theme.speakerTextHeight};
    case DialoguePreviewElement::BodyText:
        return Fields{&theme.bodyTextX, &theme.bodyTextY, &theme.bodyTextWidth, &theme.bodyTextHeight};
    case DialoguePreviewElement::ChoicePanel:
        return Fields{&theme.choiceX, &theme.choiceY, &theme.choiceWidth, &theme.choiceHeight};
    case DialoguePreviewElement::ChoiceText:
        return Fields{&theme.choiceTextOffsetX, &theme.choiceTextOffsetY, &theme.choiceTextWidth,
                      &theme.choiceTextHeight};
    case DialoguePreviewElement::LeftPortrait:
        return Fields{&style.leftPortrait.x, &style.leftPortrait.y, &style.leftPortrait.width,
                      &style.leftPortrait.height};
    case DialoguePreviewElement::RightPortrait:
        return Fields{&style.rightPortrait.x, &style.rightPortrait.y, &style.rightPortrait.width,
                      &style.rightPortrait.height};
    default: return Fields{};
    }
}

struct PreviewRect {
    DialoguePreviewElement element = DialoguePreviewElement::None;
    const char* name = "";
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

PreviewRect MakePreviewRect(const DIALOGUE::DialogueProjectStyle& style, DialoguePreviewElement element,
                            const char* name) noexcept {
    const auto fields = PreviewElementFields(style, element);
    PreviewRect rect{element, name, *fields[0], *fields[1], *fields[2], *fields[3]};
    if (element == DialoguePreviewElement::ChoiceText) {
        rect.x = style.theme.choiceX + rect.x;
        rect.y = style.theme.choiceY + rect.y;
    }
    return rect;
}

struct ArtworkField {
    const char* labelKey;
    std::string DIALOGUE::DialogueTheme::* assetPath;
};

constexpr ArtworkField kArtworkFields[]{
    {"editor.ui.dialogue.panel.097823", &DIALOGUE::DialogueTheme::dialoguePanelAssetPath},
    {"editor.ui.name.plate.55efa6", &DIALOGUE::DialogueTheme::speakerPlateAssetPath},
    {"editor.ui.choice.panel.33406a", &DIALOGUE::DialogueTheme::choiceAssetPath},
    {"editor.ui.continue.gamepad.423af3", &DIALOGUE::DialogueTheme::advanceGamepadAssetPath},
    {"editor.ui.continue.mouse.433032", &DIALOGUE::DialogueTheme::advanceMouseAssetPath},
};
} // namespace

// controller の private 状態を読む窓群。DrawDialogueWorkspaceWindows から 1 フレームだけ生成する。
class DialogueWorkspaceWindows {
public:
    DialogueWorkspaceWindows(DialogueWorkspaceController& controller, DocumentSceneBase& scene)
        : controller_(controller), scene_(scene), database_(scene.GetAssetDatabase()),
          definition_(controller.document_.Definition()), style_(controller.styleDocument_.Style()) {}

    void Draw() {
        DrawNavigator();
        DrawPreview();
        DrawInspector();
        DrawUnsavedConfirmation();
    }

private:
    void RecordDialogue(DIALOGUE::DialogueDefinition before, std::string_view field, bool continuous = true) {
        controller_.RecordMutation(std::move(before), continuous ? MergeGroup(field) : 0u);
    }
    void RecordStyle(DIALOGUE::DialogueProjectStyle before, std::string_view field, bool continuous = true) {
        controller_.RecordStyleMutation(std::move(before), continuous ? MergeGroup(field) : 0u);
    }
    void Seal() {
        if (!ImGui::IsItemDeactivatedAfterEdit())
            return;
        controller_.document_.SealMerge();
        controller_.styleDocument_.SealMerge();
    }
    std::string& Status() noexcept { return controller_.statusMessage_; }

    void DrawNavigator() {
        if (ImGui::Begin(TEXT::LocalizeWindow("editor.ui.dialogue.tools.097275", "Dialogue/Navigator").c_str())) {
            ImGui::TextColored(kAccent, TEXT::Localize("editor.ui.dialogue.authoring.ca704f").c_str());
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.project.presentation.and.conversation.events.20b37f").c_str());
            ImGui::Spacing();
            const bool stylePage = DrawPageTabs();
            ImGui::Spacing();
            DrawDocumentActions(stylePage);
            ImGui::Separator();
            DrawEditingLanguage();

            const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
            if (ImGui::BeginChild("##DialogueNavigatorBody", ImVec2(0.0f, -footerHeight), false,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                if (stylePage)
                    DrawStyleNavigator();
                else
                    DrawStoryNavigator();
            }
            ImGui::EndChild();
            ImGui::Separator();
            if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.back.to.scene.0e730f", "Back to Scene").c_str(),
                              ImVec2(-1.0f, 0.0f))) {
                EditorWorkspaceOpenRequest request{};
                request.workspaceId = EditorWorkspaceId::Scene;
                (void)controller_.RequestWorkspaceTransition(std::move(request), Status());
            }
        }
        ImGui::End();
    }

    bool DrawPageTabs() {
        bool stylePage = controller_.activePage_ == DialogueWorkspacePage::Style;
        if (ImGui::BeginTable("##DialoguePageTabs", 2,
                              ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableNextColumn();
            if (IconTextButton(EditorGlyph::Material, TEXT::Localize("editor.dialogue.style").c_str(), "stylePage",
                               stylePage ? EditorButtonTone::Primary : EditorButtonTone::Quiet, {-1, 36})) {
                controller_.activePage_ = DialogueWorkspacePage::Style;
                stylePage = true;
            }
            ImGui::TableNextColumn();
            if (IconTextButton(EditorGlyph::Sequence, TEXT::Localize("editor.dialogue.event").c_str(), "storyPage",
                               !stylePage ? EditorButtonTone::Primary : EditorButtonTone::Quiet, {-1, 36})) {
                controller_.activePage_ = DialogueWorkspacePage::Story;
                stylePage = false;
            }
            ImGui::EndTable();
        }
        return stylePage;
    }

    void DrawDocumentActions(bool stylePage) {
        if (ImGui::BeginTable("##DialogueDocumentActions", 3,
                              ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableNextColumn();
            if (IconButton(EditorGlyph::Save, "dialogueSave", EditorButtonTone::Quiet,
                           {ImGui::GetContentRegionAvail().x, 28}, TEXT::Localize("ui.common.save").c_str())) {
                (void)controller_.Save(scene_, Status());
            }
            const bool canUndo = stylePage ? controller_.styleDocument_.CanUndo() : controller_.document_.CanUndo();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!canUndo);
            if (IconButton(EditorGlyph::Undo, "dialogueUndo", EditorButtonTone::Quiet,
                           {ImGui::GetContentRegionAvail().x, 28}, TEXT::Localize("menu.edit.undo").c_str())) {
                (void)controller_.Undo(Status());
            }
            ImGui::EndDisabled();
            const bool canRedo = stylePage ? controller_.styleDocument_.CanRedo() : controller_.document_.CanRedo();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!canRedo);
            if (IconButton(EditorGlyph::Redo, "dialogueRedo", EditorButtonTone::Quiet,
                           {ImGui::GetContentRegionAvail().x, 28}, TEXT::Localize("menu.edit.redo").c_str())) {
                (void)controller_.Redo(Status());
            }
            ImGui::EndDisabled();
            ImGui::EndTable();
        }
        const bool activeDirty = stylePage ? controller_.styleDocument_.IsDirty() : controller_.document_.IsDirty();
        if (activeDirty) {
            ImGui::TextColored(
                kModified, stylePage ? TEXT::Localize("editor.ui.ui.style.has.unsaved.changes.b18536").c_str()
                                     : TEXT::Localize("editor.ui.dialogue.event.has.unsaved.changes.b4dc55").c_str());
        }
    }

    void DrawEditingLanguage() {
        const char* locales[]{"ja-JP", "en-US", "zh-CN"};
        ImGui::TextDisabled(TEXT::Localize("editor.ui.editing.language.b32616").c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##DialogueEditingLanguage", controller_.editingLocale_.c_str())) {
            for (const char* locale : locales) {
                const bool selected = controller_.editingLocale_ == locale;
                if (ImGui::Selectable(locale, selected)) {
                    controller_.editingLocale_ = locale;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    void DrawStyleNavigator() {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.style.workflow.8f19d1").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.start.with.artwork.and.typography.register.the.cast.then.tune.mo.b28632")
                .c_str());
        ImGui::Spacing();
        for (uint8_t value = 0u; value <= static_cast<uint8_t>(DialogueStyleSection::Motion); ++value) {
            const auto section = static_cast<DialogueStyleSection>(value);
            if (DrawFullWidthSelectable(StyleSectionNavigationLabel(section), controller_.styleSection_ == section,
                                        32.0f)) {
                controller_.styleSection_ = section;
            }
            if (controller_.styleSection_ == section) {
                ImGui::Indent(10.0f);
                ImGui::PushTextWrapPos();
                ImGui::TextDisabled("%s", StyleSectionDescription(section));
                ImGui::PopTextWrapPos();
                ImGui::Unindent(10.0f);
            }
        }
        if (controller_.styleSection_ != DialogueStyleSection::Characters)
            return;
        ImGui::SeparatorText(TEXT::Localize("editor.ui.project.characters.aabbd7").c_str());
        if (style_.speakers.empty()) {
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.no.characters.are.registered.yet.add.one.here.then.assign.its.lo.9c1301")
                    .c_str());
        }
        for (const auto& speaker : style_.speakers) {
            const std::string label =
                std::string(ResolveName(speaker, controller_.editingLocale_, style_.fallbackLocale));
            ImGui::PushID(speaker.id.c_str());
            if (DrawFullWidthSelectable(label.c_str(), controller_.selectedSpeakerId_ == speaker.id, 28.0f)) {
                controller_.selectedSpeakerId_ = speaker.id;
            }
            ImGui::PopID();
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                                  ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            (void)controller_.DeleteSelectedSpeaker(Status());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.character.ccbd86", "+ Add Character").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            controller_.AddSpeaker();
        }
    }

    void DrawStoryNavigator() {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.event.workflow.8209d5").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.one.dialogue.asset.is.one.complete.conversation.add.steps.below..8f74ad")
                .c_str());
        ImGui::Spacing();
        DrawDialogueAssetList();
        if (!controller_.document_.IsOpen()) {
            ImGui::Spacing();
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.open.an.existing.dialogue.asset.above.or.create.a.new.event.44bf96").c_str());
            return;
        }
        ImGui::TextDisabled(TEXT::Localize("editor.ui.current.event.f840cb").c_str());
        ImGui::TextWrapped("%s%s", definition_.name.c_str(), controller_.document_.IsDirty() ? "  *" : "");
        if (ImGui::BeginTable("##DialogueCurrentActions", 2,
                              ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableNextColumn();
            if (ImGui::Button(TEXT::LocalizeWindow("ui.assetInspector.revert", "Revert").c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                (void)controller_.RequestRevert(database_, Status());
            }
            ImGui::TableNextColumn();
            if (ImGui::Button(TEXT::LocalizeWindow("ui.input.close", "Close").c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                (void)controller_.RequestCloseAsset(Status());
            }
            ImGui::EndTable();
        }
        DrawStoryStepList();
    }

    void DrawDialogueAssetList() {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.dialogue.assets.8ddd00").c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##DialogueAssetSearch",
                                 TEXT::Localize("editor.ui.search.dialogue.assets.82112b").c_str(),
                                 controller_.assetSearch_.data(), controller_.assetSearch_.size());
        const std::string_view search(controller_.assetSearch_.data());
        bool listedAsset = false;
        for (const DialogueAssetListEntry& asset : controller_.ListAssets(database_)) {
            if (!ContainsCaseInsensitive(asset.name, search) &&
                !ContainsCaseInsensitive(asset.sourcePath.generic_string(), search)) {
                continue;
            }
            listedAsset = true;
            const std::string label =
                (asset.current ? "[OPEN]  " : "") + asset.name + "##DialogueAsset/" + asset.guid.value;
            if (DrawFullWidthSelectable(label.c_str(), asset.current, 30.0f)) {
                (void)controller_.RequestOpenAsset(database_, asset.guid, Status());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", asset.sourcePath.generic_string().c_str());
            }
        }
        if (!listedAsset) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.matching.dialogue.assets.5b559b").c_str());
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.new.dialogue.event.5e0581", "New Dialogue Event").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            (void)controller_.RequestNewAsset(Status());
        }
    }

    void DrawStoryStepList() {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.story.steps.3bbc62").c_str());
        if (ImGui::Checkbox(
                TEXT::LocalizeWindow("editor.ui.auto.link.inserted.step.85d8b6", "Auto-link inserted step").c_str(),
                &controller_.autoBindNewNodes_)) {
            Status() = controller_.autoBindNewNodes_ ? "New story steps will be linked automatically"
                                                     : "Automatic story-step linking disabled";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                TEXT::Localize("editor.ui.insert.after.the.selected.step.link.the.current.continuation.to..a3c741")
                    .c_str());
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.story.step.8e7a80", "+ Add Story Step").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            ImGui::OpenPopup("AddDialogueStep");
        }
        if (ImGui::BeginPopup("AddDialogueStep")) {
            if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.line.ea9676", "Line").c_str())) {
                controller_.AddNode(DIALOGUE::DialogueNodeType::Line);
            }
            if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.choice.0682be", "Choice").c_str())) {
                controller_.AddNode(DIALOGUE::DialogueNodeType::Choice);
            }
            if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.event.signal.58ef83", "Event signal").c_str())) {
                controller_.AddNode(DIALOGUE::DialogueNodeType::Event);
            }
            if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.vfx.text.End", "End").c_str())) {
                controller_.AddNode(DIALOGUE::DialogueNodeType::End);
            }
            ImGui::EndPopup();
        }
        ImGui::Spacing();
        for (size_t index = 0u; index < definition_.nodes.size(); ++index) {
            const auto& node = definition_.nodes[index];
            const bool selected = controller_.selectedNodeId_ == node.id;
            const std::string label = std::to_string(index + 1u) +
                                      (node.id == definition_.entryNodeId ? "  [START]" : "") + "  [" +
                                      NodeTypeLabel(node.type) + "]  " +
                                      NodeSummary(node, style_, controller_.editingLocale_, definition_.fallbackLocale);
            ImGui::PushID(node.id.c_str());
            if (DrawFullWidthSelectable(label.c_str(), selected, 38.0f)) {
                controller_.selectedNodeId_ = node.id;
                controller_.selectedChoiceIndex_.reset();
                controller_.nodeIdDraftOwner_.clear();
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(!controller_.CanBindSelectedToNextNode());
        if (ImGui::Button(
                TEXT::LocalizeWindow("editor.ui.bind.selected.to.next.step.567a41", "Bind Selected to Next Step")
                    .c_str(),
                ImVec2(-1.0f, 0.0f))) {
            (void)controller_.BindSelectedToNextNode(Status());
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                TEXT::Localize("editor.ui.link.this.line.or.event.to.the.following.list.item.for.a.choice..09ae09")
                    .c_str());
        }
        ImGui::BeginDisabled(definition_.nodes.size() <= 1u);
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.remove.step.9fcbf9", "Remove Step").c_str(),
                          ImVec2(-1.0f, 0.0f)) ||
            (definition_.nodes.size() > 1 &&
             ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                                   ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))) {
            (void)controller_.DeleteSelectedNode(Status());
        }
        ImGui::EndDisabled();
    }

    void DrawPreviewToolbar() {
        ImGui::TextColored(kAccent, controller_.activePage_ == DialogueWorkspacePage::Style
                                        ? TEXT::Localize("editor.ui.ui.style.canvas.c3bc93").c_str()
                                        : TEXT::Localize("editor.ui.dialogue.event.preview.ef3e4d").c_str());
        ImGui::SameLine();
        ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.animate.e733b2", "Animate").c_str(),
                        &controller_.previewPlaying_);
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.replay.c0f85d", "Replay").c_str()))
            controller_.previewElapsed_ = 0.0f;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.worldText.text", "Text").c_str(),
                           &controller_.previewRevealAmount_, 0.0f, 1.0f, "%.2f");
        if (controller_.activePage_ == DialogueWorkspacePage::Style) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.select.an.outline.to.move.it.drag.its.lower.right.handle.to.resi.81f6b1")
                    .c_str());
        }
        if (controller_.previewPlaying_) {
            controller_.previewElapsed_ += (std::max)(ImGui::GetIO().DeltaTime, 0.0f);
        }
    }

    void DrawPreview() {
        if (ImGui::Begin(TEXT::LocalizeWindow("ui.sceneCreation.canvas", "Dialogue/Preview").c_str())) {
            DrawPreviewToolbar();

            DIALOGUE::DialogueNode previewSample{};
            DIALOGUE::DialogueNode* previewNode = controller_.SelectedNode();
            if (controller_.activePage_ == DialogueWorkspacePage::Style || previewNode == nullptr) {
                previewSample.id = "preview";
                previewSample.type = DIALOGUE::DialogueNodeType::Line;
                previewSample.speakerId = controller_.selectedSpeakerId_;
                previewSample.text.values["en-US"] = "This is how dialogue will appear in the game.";
                previewSample.text.values["ja-JP"] = "ゲーム内の会話表示をここで確認できます。";
                previewSample.text.values["zh-CN"] = "可以在这里确认游戏中的实际对话效果。";
                previewNode = &previewSample;
            }

            const float referenceWidth = (std::max)(style_.theme.referenceWidth, 1.0f);
            const float referenceHeight = (std::max)(style_.theme.referenceHeight, 1.0f);
            const float aspect = referenceWidth / referenceHeight;
            ImVec2 available = ImGui::GetContentRegionAvail();
            available.y -= ImGui::GetTextLineHeightWithSpacing();
            // Keep the canvas inside its dock even while the user is resizing
            // panels. Large hard minimums make sibling windows appear missing.
            available.x = (std::max)(available.x, 64.0f);
            available.y = (std::max)(available.y, 64.0f);
            ImVec2 canvas = available;
            if (canvas.x / canvas.y > aspect)
                canvas.x = canvas.y * aspect;
            else
                canvas.y = canvas.x / aspect;
            const ImVec2 availableOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 origin{availableOrigin.x + (available.x - canvas.x) * 0.5f,
                                availableOrigin.y + (available.y - canvas.y) * 0.5f};
            ImGui::SetCursorScreenPos(origin);

            DialoguePreviewRenderer* renderer = controller_.previewRenderer_.get();
            bool previewReady = false;
            if (previewNode != nullptr && renderer != nullptr) {
                previewReady = renderer->Render(
                    database_, definition_, style_, *previewNode, controller_.editingLocale_,
                    controller_.selectedChoiceIndex_.value_or(0u), controller_.previewElapsed_,
                    controller_.previewRevealAmount_, static_cast<uint32_t>((std::max)(std::round(canvas.x), 1.0f)),
                    static_cast<uint32_t>((std::max)(std::round(canvas.y), 1.0f)));
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE output =
                renderer != nullptr ? renderer->GetOutputSrv() : D3D12_GPU_DESCRIPTOR_HANDLE{};
            if (previewReady && output.ptr != 0u) {
                ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(output.ptr)), canvas);
            } else {
                ImGui::InvisibleButton("##DialoguePreviewCanvas", canvas, ImGuiButtonFlags_MouseButtonLeft);
            }
            const bool canvasHovered = ImGui::IsItemHovered();
            if (!previewReady) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(origin, {origin.x + canvas.x, origin.y + canvas.y}, IM_COL32(26, 30, 40, 255));
                const std::string message =
                    renderer != nullptr ? renderer->GetStatus() : "Preview renderer unavailable";
                const ImVec2 textSize = ImGui::CalcTextSize(message.c_str());
                drawList->AddText(
                    {origin.x + (canvas.x - textSize.x) * 0.5f, origin.y + (canvas.y - textSize.y) * 0.5f},
                    IM_COL32(196, 205, 222, 255), message.c_str());
            }
            EditPreviewLayout(origin, {canvas.x / referenceWidth, canvas.y / referenceHeight}, canvasHovered);

            ImGui::SetCursorScreenPos({availableOrigin.x, availableOrigin.y + available.y});
            ImGui::Dummy({1.0f, 1.0f});
            if (renderer != nullptr) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.live.preview.s.8878fb").c_str(),
                                    renderer->GetStatus().c_str());
            }
        }
        ImGui::End();
    }

    // Style ページでは canvas 上の枠をクリックで選択し、ドラッグで移動、右下ハンドルでサイズ変更する。
    void EditPreviewLayout(const ImVec2& origin, const ImVec2& scale, bool canvasHovered) {
        std::array<PreviewRect, std::size(kPreviewElements)> rects{};
        for (size_t index = 0u; index < rects.size(); ++index) {
            rects[index] = MakePreviewRect(style_, kPreviewElements[index].first, kPreviewElements[index].second);
        }
        const auto minimum = [&](const PreviewRect& rect) {
            return ImVec2{origin.x + rect.x * scale.x, origin.y + rect.y * scale.y};
        };
        const auto maximum = [&](const PreviewRect& rect) {
            return ImVec2{origin.x + (rect.x + rect.width) * scale.x, origin.y + (rect.y + rect.height) * scale.y};
        };
        const auto contains = [](const ImVec2& point, const ImVec2& min, const ImVec2& max) {
            return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
        };
        const bool canEditCanvas = controller_.activePage_ == DialogueWorkspacePage::Style;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (canEditCanvas && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            DialoguePreviewElement picked = DialoguePreviewElement::None;
            bool resize = false;
            for (const PreviewRect& rect : rects) {
                if (rect.element != controller_.previewElement_)
                    continue;
                const ImVec2 corner = maximum(rect);
                if (contains(mouse, {corner.x - 11.0f, corner.y - 11.0f}, {corner.x + 5.0f, corner.y + 5.0f})) {
                    picked = rect.element;
                    resize = true;
                }
                break;
            }
            if (picked == DialoguePreviewElement::None) {
                for (auto it = rects.rbegin(); it != rects.rend(); ++it) {
                    if (contains(mouse, minimum(*it), maximum(*it))) {
                        picked = it->element;
                        break;
                    }
                }
            }
            controller_.previewElement_ = picked;
            controller_.previewDragResize_ = resize;
            controller_.previewDragChanged_ = false;
            controller_.previewDragMouseX_ = mouse.x;
            controller_.previewDragMouseY_ = mouse.y;
            controller_.previewStyleDragBefore_ = picked == DialoguePreviewElement::None
                                                      ? std::optional<DIALOGUE::DialogueProjectStyle>{}
                                                      : std::optional<DIALOGUE::DialogueProjectStyle>{style_};
        }
        if (controller_.previewStyleDragBefore_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx = (mouse.x - controller_.previewDragMouseX_) / (std::max)(scale.x, 1.0e-4f);
            const float dy = (mouse.y - controller_.previewDragMouseY_) / (std::max)(scale.y, 1.0e-4f);
            const auto fields = PreviewElementFields(style_, controller_.previewElement_);
            const auto start = PreviewElementFields(*controller_.previewStyleDragBefore_, controller_.previewElement_);
            if (fields[0] != nullptr) {
                if (controller_.previewDragResize_) {
                    *fields[2] = (std::max)(*start[2] + dx, 1.0f);
                    *fields[3] = (std::max)(*start[3] + dy, 1.0f);
                } else {
                    *fields[0] = *start[0] + dx;
                    *fields[1] = *start[1] + dy;
                }
            }
            controller_.previewDragChanged_ = std::abs(dx) > 0.01f || std::abs(dy) > 0.01f;
        }
        if (controller_.previewStyleDragBefore_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (controller_.previewDragChanged_) {
                controller_.RecordStyleMutation(std::move(*controller_.previewStyleDragBefore_), 0u);
            }
            controller_.previewStyleDragBefore_.reset();
            controller_.previewDragChanged_ = false;
        }
        if (!canEditCanvas)
            return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (const PreviewRect& rect : rects) {
            const bool selected = rect.element == controller_.previewElement_;
            const ImU32 color = selected ? IM_COL32(255, 210, 64, 235) : IM_COL32(48, 170, 255, 90);
            const ImVec2 rectMin = minimum(rect);
            const ImVec2 rectMax = maximum(rect);
            drawList->AddRect(rectMin, rectMax, color, 0.0f, 0, selected ? 2.0f : 1.0f);
            if (selected) {
                drawList->AddRectFilled({rectMax.x - 7.0f, rectMax.y - 7.0f}, {rectMax.x + 3.0f, rectMax.y + 3.0f},
                                        color);
                drawList->AddText({rectMin.x + 4.0f, rectMin.y + 3.0f}, color, rect.name);
            }
        }
    }

    void DrawInspector() {
        if (ImGui::Begin(TEXT::LocalizeWindow("editor.ui.properties.bc6c88", "Dialogue/Inspector").c_str())) {
            if (controller_.activePage_ == DialogueWorkspacePage::Style)
                DrawStyleInspector();
            else
                DrawStoryInspector();
            if (!Status().empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", Status().c_str());
            }
        }
        ImGui::End();
    }

    void DrawStyleInspector() {
        ImGui::TextColored(kAccent, "%s", StyleSectionLabel(controller_.styleSection_));
        ImGui::TextWrapped("%s", StyleSectionDescription(controller_.styleSection_));
        ImGui::Separator();
        switch (controller_.styleSection_) {
        case DialogueStyleSection::Canvas: DrawCanvasStyle(); break;
        case DialogueStyleSection::Typography: DrawTypographyStyle(); break;
        case DialogueStyleSection::Portraits: DrawPortraitStyle(); break;
        case DialogueStyleSection::Characters: DrawCharacterStyle(); break;
        case DialogueStyleSection::Motion: DrawMotionStyle(); break;
        default: break;
        }
        const auto issues = DIALOGUE::ValidateDialogueProjectStyle(style_);
        if (!issues.empty()) {
            ImGui::Separator();
            ImGui::TextColored(kError, TEXT::Localize("editor.ui.style.needs.attention.4a82d8").c_str());
            ImGui::TextWrapped("%s", issues.front().message.c_str());
        }
    }

    void DrawCanvasStyle() {
        auto before = style_;
        bool changed = false;
        for (const ArtworkField& artwork : kArtworkFields) {
            changed = DrawTextureAssetField(TEXT::Localize(artwork.labelKey).c_str(), database_,
                                            style_.theme.*artwork.assetPath) ||
                      changed;
        }
        if (changed)
            RecordStyle(std::move(before), "style.art", false);
        ImGui::Spacing();
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.place.and.resize.artwork.directly.on.the.canvas.the.panel.is.tre.43e567")
                .c_str());
        if (ImGui::CollapsingHeader(
                TEXT::LocalizeWindow("editor.ui.reference.canvas.29bf4e", "Reference canvas").c_str())) {
            before = style_;
            float size[2]{style_.theme.referenceWidth, style_.theme.referenceHeight};
            if (ImGui::DragFloat2(
                    TEXT::LocalizeWindow("editor.ui.design.resolution.435501", "Design resolution").c_str(), size, 1.0f,
                    320.0f, 8192.0f, "%.0f")) {
                style_.theme.referenceWidth = size[0];
                style_.theme.referenceHeight = size[1];
                RecordStyle(std::move(before), "style.canvas");
            }
            Seal();
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.continue.prompt.fc9ef9", "Continue prompt").c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            before = style_;
            bool promptChanged =
                ImGui::DragFloat2(TEXT::LocalizeWindow("editor.ui.prompt.position.c2d53e", "Prompt position").c_str(),
                                  &style_.theme.advancePromptX, 1.0f, -4096.0f, 4096.0f, "%.0f");
            promptChanged =
                ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.scale.15563f", "Prompt scale").c_str(),
                                   &style_.theme.advancePromptScale, 0.20f, 4.0f, "%.2fx") ||
                promptChanged;
            if (promptChanged) {
                RecordStyle(std::move(before), "style.advancePromptLayout");
            }
            Seal();
        }
    }

    void DrawTypographyStyle() {
        auto before = style_;
        ImGuiInspectorBuilder inspector;
        InspectorContext context{};
        context.assetDatabase = &database_;
        inspector.SetContext(context);
        bool changed = inspector.AssetIdPicker(
            AuthoringFieldDescriptor{"speakerTextStyle", TEXT::Localize("editor.ui.character.name.c0a090")},
            AssetType::TextStyle, style_.theme.speakerTextStyle.value);
        changed = inspector.AssetIdPicker(
                      AuthoringFieldDescriptor{"bodyTextStyle", TEXT::Localize("editor.ui.dialogue.text.1bd98a")},
                      AssetType::TextStyle, style_.theme.bodyTextStyle.value) ||
                  changed;
        changed = inspector.AssetIdPicker(
                      AuthoringFieldDescriptor{"choiceTextStyle", TEXT::Localize("editor.ui.choice.text.a9c015")},
                      AssetType::TextStyle, style_.theme.choiceTextStyle.value) ||
                  changed;
        if (changed)
            RecordStyle(std::move(before), "style.typography");
        Seal();
    }

    void DrawPortraitStyle() {
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.the.runtime.displays.at.most.two.portraits.move.the.left.and.rig.bd843e")
                .c_str());
        DrawPortraitSlot(TEXT::LocalizeWindow("editor.ui.left.slot.aa2913", "Left slot"), "##Left", style_.leftPortrait,
                         "style.leftPortrait");
        DrawPortraitSlot(TEXT::LocalizeWindow("editor.ui.right.slot.ad5773", "Right slot"), "##Right",
                         style_.rightPortrait, "style.rightPortrait");
    }

    void DrawPortraitSlot(const std::string& header, std::string_view idSuffix,
                          DIALOGUE::DialoguePortraitSlotStyle& slot, std::string_view field) {
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            return;
        const auto stableId = [idSuffix](std::string_view name) { return std::string(name) + std::string(idSuffix); };
        auto before = style_;
        bool changed = ImGui::DragFloat2(TEXT::LocalizeWindow("ui.vfx.text.Position", stableId("Position")).c_str(),
                                         &slot.x, 1.0f, -4096.0f, 4096.0f, "%.0f");
        changed = ImGui::DragFloat2(TEXT::LocalizeWindow("ui.inspector.worldSprite.size", stableId("Size")).c_str(),
                                    &slot.width, 1.0f, 1.0f, 4096.0f, "%.0f") ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.rotation", stableId("Rotation")).c_str(),
                                     &slot.rotationDegrees, -45.0f, 45.0f, "%.1f deg") ||
                  changed;
        if (changed)
            RecordStyle(std::move(before), field);
        Seal();
    }

    void DrawCharacterStyle() {
        auto* speaker = DIALOGUE::FindDialogueSpeaker(style_, controller_.selectedSpeakerId_);
        if (speaker == nullptr) {
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.add.a.character.from.the.left.panel.names.and.portraits.register.88e7d9")
                    .c_str());
            return;
        }
        auto before = style_;
        bool changed = DrawLocalizedText(TEXT::LocalizeWindow("editor.ui.display.name.c7874a", "Display name").c_str(),
                                         speaker->displayName, controller_.editingLocale_);
        std::string portraitGuid = speaker->portraitAssetGuid;
        AssetFieldOptions portraitOptions{};
        portraitOptions.label = "Portrait texture";
        portraitOptions.requiredType = AssetType::Texture;
        portraitOptions.allowCopyGuid = false;
        portraitOptions.allowLocate = false;
        if (DrawAssetField(&database_, portraitOptions, portraitGuid)) {
            speaker->portraitAssetGuid = std::move(portraitGuid);
            changed = true;
        }
        if (speaker->portraitAssetGuid.empty()) {
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.no.portrait.assigned.the.canvas.will.show.dialogue.without.a.cha.41b3b8")
                    .c_str());
        } else if (speaker->resolvedPortraitAssetPath.empty()) {
            ImGui::TextColored(
                kError,
                TEXT::Localize("editor.ui.portrait.unavailable.choose.an.imported.texture.asset.again.fc38e9").c_str());
        } else {
            ImGui::TextColored(kReady, TEXT::Localize("editor.ui.live.portrait.preview.ready.61a06d").c_str());
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.the.center.canvas.updates.while.you.adjust.this.character.aec428").c_str());
        }
        const char* sides[]{TEXT::Localize("ui.quality.exposureAutomatic").c_str(),
                            TEXT::Localize("ui.input.directionLeft").c_str(),
                            TEXT::Localize("ui.input.directionRight").c_str()};
        int side = static_cast<int>(speaker->preferredSlot);
        changed = ImGui::Combo(TEXT::LocalizeWindow("editor.ui.preferred.side.7bd0e7", "Preferred side").c_str(), &side,
                               sides, 3) ||
                  changed;
        speaker->preferredSlot = static_cast<DIALOGUE::DialoguePortraitSlot>(side);
        ImGui::SeparatorText(TEXT::Localize("editor.ui.portrait.alignment.84f55e").c_str());
        changed = ImGui::DragFloat2(TEXT::LocalizeWindow("editor.ui.offset.ce6677", "Offset").c_str(),
                                    &speaker->offsetX, 1.0f, -2048.0f, 2048.0f, "%.0f") ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.input.scale", "Scale").c_str(), &speaker->scale, 0.1f,
                                     3.0f, "%.2fx") ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.rotation", "Rotation").c_str(),
                                     &speaker->rotationDegrees, -45.0f, 45.0f, "%.1f deg") ||
                  changed;
        changed =
            ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.mirror.horizontally.3b5ec9", "Mirror horizontally").c_str(),
                            &speaker->mirror) ||
            changed;
        if (changed) {
            RecordStyle(std::move(before), "style.character");
            DIALOGUE::ResolveDialogueProjectStyleAssets(style_, database_);
        }
        Seal();
        DrawCharacterIdentity(*speaker);
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.remove.character.076063", "Remove Character").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            (void)controller_.DeleteSelectedSpeaker(Status());
        }
        ImGui::PopStyleColor();
    }

    // id の変更は style と、その話者を参照する dialogue の両方へ反映する。
    void DrawCharacterIdentity(DIALOGUE::DialogueSpeakerProfile& speaker) {
        if (!ImGui::CollapsingHeader(
                TEXT::LocalizeWindow("editor.ui.advanced.identity.54f0f7", "Advanced identity").c_str()))
            return;
        const std::string originalId = speaker.id;
        auto before = style_;
        std::string id = speaker.id;
        if (InputString(TEXT::LocalizeWindow("editor.ui.stable.character.id.c80fb2", "Stable character id").c_str(), id,
                        false, 128u) &&
            !id.empty() && (id == originalId || DIALOGUE::FindDialogueSpeaker(style_, id) == nullptr)) {
            speaker.id = id;
            controller_.selectedSpeakerId_ = id;
            auto dialogueBefore = definition_;
            bool dialogueChanged = false;
            for (auto& node : definition_.nodes) {
                if (node.speakerId == originalId) {
                    node.speakerId = id;
                    dialogueChanged = true;
                }
            }
            RecordStyle(std::move(before), "style.characterId", false);
            if (dialogueChanged) {
                RecordDialogue(std::move(dialogueBefore), "dialogue.characterId", false);
            }
        }
        Seal();
    }

    void DrawMotionStyle() {
        auto& theme = style_.theme;
        auto& transition = style_.portraitTransition;
        auto before = style_;
        bool changed =
            ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.panel.entrance.9ed47b", "Panel entrance").c_str(),
                               &theme.panelRevealSeconds, 0.05f, 1.5f, "%.2f s");
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.text.speed.3768e9", "Text speed").c_str(),
                                     &theme.charactersPerSecond, 5.0f, 120.0f, "%.0f chars/s") ||
                  changed;
        ImGui::SeparatorText(TEXT::Localize("editor.ui.continue.prompt.fc9ef9").c_str());
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.cycle.9ebf34", "Prompt cycle").c_str(),
                                     &theme.advancePromptPulseSeconds, 0.20f, 3.0f, "%.2f s") ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.bob.1ebb99", "Prompt bob").c_str(),
                                     &theme.advancePromptBobDistance, 0.0f, 32.0f, "%.1f px") ||
                  changed;
        changed = ImGui::SliderFloat(
                      TEXT::LocalizeWindow("editor.ui.prompt.pulse.scale.b621a2", "Prompt pulse scale").c_str(),
                      &theme.advancePromptScaleAmount, 0.0f, 0.20f, "%.3f") ||
                  changed;
        ImGui::SeparatorText(TEXT::Localize("editor.ui.portrait.focus.2bb526").c_str());
        changed = DrawColor(TEXT::LocalizeWindow("editor.ui.speaking.portrait.a7665b", "Speaking portrait").c_str(),
                            transition.speakingTint) ||
                  changed;
        changed = DrawColor(TEXT::LocalizeWindow("editor.ui.listening.portrait.a384af", "Listening portrait").c_str(),
                            transition.listeningTint) ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.focus.blend.bfb55a", "Focus blend").c_str(),
                                     &transition.focusSeconds, 0.05f, 0.8f, "%.2f s") ||
                  changed;
        changed =
            ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.portrait.entrance.17093f", "Portrait entrance").c_str(),
                               &transition.enterSeconds, 0.05f, 1.2f, "%.2f s") ||
            changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.entry.slide.dc2e87", "Entry slide").c_str(),
                                     &transition.slideDistance, 0.0f, 240.0f, "%.0f px") ||
                  changed;
        changed = DrawColor(TEXT::LocalizeWindow("editor.ui.edge.light.fa66de", "Edge light").c_str(),
                            transition.edgeLightColor) ||
                  changed;
        changed = ImGui::SliderFloat(
                      TEXT::LocalizeWindow("editor.ui.edge.light.strength.4d86b9", "Edge light strength").c_str(),
                      &transition.edgeLightIntensity, 0.0f, 2.0f, "%.2f") ||
                  changed;
        ImGui::SeparatorText(TEXT::Localize("editor.ui.background.game.view.e8990a").c_str());
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.darken.d7b6ff", "Darken").c_str(),
                                     &theme.backdropDim, 0.0f, 0.8f, "%.2f") ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.vfx.text.Blur", "Blur").c_str(), &theme.backdropBlur,
                                     0.0f, 1.0f, "%.2f") ||
                  changed;
        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.desaturate.f6d138", "Desaturate").c_str(),
                                     &theme.backdropDesaturation, 0.0f, 1.0f, "%.2f") ||
                  changed;
        if (changed)
            RecordStyle(std::move(before), "style.motion");
        Seal();
    }

    void DrawStoryInspector() {
        ImGui::TextColored(kAccent, TEXT::Localize("editor.ui.dialogue.event.8bbdfe").c_str());
        if (!controller_.document_.IsOpen()) {
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.no.dialogue.asset.is.open.select.an.existing.asset.or.create.a.n.e7db2a")
                    .c_str());
            return;
        }
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.edit.the.selected.story.step.here.the.center.preview.uses.the.sa.35ee76")
                .c_str());
        ImGui::Separator();
        std::string name = definition_.name;
        if (InputString(TEXT::LocalizeWindow("editor.ui.event.name.8e10d7", "Event name").c_str(), name)) {
            auto before = definition_;
            definition_.name = std::move(name);
            RecordDialogue(std::move(before), "dialogue.name");
        }
        Seal();
        DIALOGUE::DialogueNode* node = controller_.SelectedNode();
        if (node == nullptr) {
            ImGui::TextColored(kModified, TEXT::Localize("editor.ui.no.story.step.selected.b5c5a3").c_str());
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.choose.a.step.in.dialogue.tools.or.add.a.new.line.choice.signal..84d62a")
                    .c_str());
        } else {
            switch (node->type) {
            case DIALOGUE::DialogueNodeType::Line: DrawLineStep(*node); break;
            case DIALOGUE::DialogueNodeType::Choice: DrawChoiceStep(*node); break;
            case DIALOGUE::DialogueNodeType::Event: DrawEventStep(*node); break;
            default:
                ImGui::TextDisabled(TEXT::Localize("editor.ui.end.step.b2bffe").c_str());
                ImGui::TextWrapped(
                    TEXT::Localize("editor.ui.this.step.completes.the.dialogue.event.and.releases.the.trigger..c96704")
                        .c_str());
                break;
            }
            DrawStepAdvanced(*node);
        }

        const auto issues = DIALOGUE::ValidateDialogue(definition_);
        if (!issues.empty()) {
            ImGui::Separator();
            ImGui::TextColored(kError, TEXT::Localize("editor.ui.dialogue.needs.attention.02e600").c_str());
            ImGui::TextWrapped("%s", issues.front().message.c_str());
        }
    }

    void DrawSpeakerCombo(DIALOGUE::DialogueNode& node) {
        const auto* currentSpeaker = DIALOGUE::FindDialogueSpeaker(style_, node.speakerId);
        const std::string speakerPreview =
            currentSpeaker
                ? std::string(ResolveName(*currentSpeaker, controller_.editingLocale_, style_.fallbackLocale))
                : std::string("Narration / no portrait");
        if (!ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.character.ee9946", "Character").c_str(),
                               speakerPreview.c_str()))
            return;
        if (ImGui::Selectable(
                TEXT::LocalizeWindow("editor.ui.narration.no.portrait.bec1a6", "Narration / no portrait").c_str(),
                node.speakerId.empty())) {
            auto before = definition_;
            node.speakerId.clear();
            RecordDialogue(std::move(before), "line.speaker", false);
        }
        for (const auto& speaker : style_.speakers) {
            const bool selected = node.speakerId == speaker.id;
            const std::string label =
                std::string(ResolveName(speaker, controller_.editingLocale_, style_.fallbackLocale));
            ImGui::PushID(speaker.id.c_str());
            if (ImGui::Selectable(label.c_str(), selected)) {
                auto before = definition_;
                node.speakerId = speaker.id;
                RecordDialogue(std::move(before), "line.speaker", false);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    void DrawNextStep(DIALOGUE::DialogueNode& node, std::string_view field) {
        std::string next = node.nextNodeId;
        if (TargetCombo(TEXT::LocalizeWindow("editor.ui.continue.to.02bdf3", "Continue to").c_str(), definition_,
                        style_, controller_.editingLocale_, next)) {
            auto before = definition_;
            node.nextNodeId = std::move(next);
            RecordDialogue(std::move(before), field, false);
        }
    }

    void DrawLineStep(DIALOGUE::DialogueNode& node) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.line.step.a1ebff").c_str());
        ImGui::SeparatorText(TEXT::Localize("editor.ui.speaker.7c23b0").c_str());
        DrawSpeakerCombo(node);
        auto before = definition_;
        if (DrawLocalizedText(TEXT::LocalizeWindow("ui.assetBrowser.typeDialogue", "Dialogue").c_str(), node.text,
                              controller_.editingLocale_, true, 180.0f)) {
            RecordDialogue(std::move(before), "line.text");
        }
        Seal();
        ImGui::SeparatorText(TEXT::Localize("editor.ui.portrait.direction.960d48").c_str());
        const char* actions[]{TEXT::Localize("ui.quality.exposureAutomatic").c_str(),
                              TEXT::Localize("editor.ui.show.replace.f060fd").c_str(),
                              TEXT::Localize("editor.ui.keep.current.786906").c_str(),
                              TEXT::Localize("ui.vfx.hierarchy.hide").c_str()};
        int action = static_cast<int>(node.portraitAction);
        before = definition_;
        bool changed =
            ImGui::Combo(TEXT::LocalizeWindow("editor.ui.portrait.behavior.1d8231", "Portrait behavior").c_str(),
                         &action, actions, 4);
        node.portraitAction = static_cast<DIALOGUE::DialoguePortraitAction>(action);
        const char* slots[]{TEXT::Localize("ui.quality.exposureAutomatic").c_str(),
                            TEXT::Localize("ui.input.directionLeft").c_str(),
                            TEXT::Localize("ui.input.directionRight").c_str()};
        int slot = static_cast<int>(node.portraitSlot);
        changed = ImGui::Combo(TEXT::LocalizeWindow("editor.ui.portrait.side.136200", "Portrait side").c_str(), &slot,
                               slots, 3) ||
                  changed;
        node.portraitSlot = static_cast<DIALOGUE::DialoguePortraitSlot>(slot);
        if (changed)
            RecordDialogue(std::move(before), "line.portrait", false);
        DrawNextStep(node, "line.next");
        if (ImGui::CollapsingHeader(
                TEXT::LocalizeWindow("editor.ui.playback.options.75eab2", "Playback options").c_str())) {
            before = definition_;
            changed = ImGui::DragFloat(
                TEXT::LocalizeWindow("editor.ui.auto.continue.after.89d8dd", "Auto continue after").c_str(),
                &node.autoAdvanceSeconds, 0.05f, 0.0f, 120.0f, "%.2f s");
            changed = ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.player.can.reveal.text.instantly.7358e0",
                                                           "Player can reveal text instantly")
                                          .c_str(),
                                      &node.skippable) ||
                      changed;
            if (changed)
                RecordDialogue(std::move(before), "line.playback");
            Seal();
        }
    }

    void DrawChoiceStep(DIALOGUE::DialogueNode& node) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.choice.step.1e8d95").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.each.option.links.to.another.step.the.selected.option.uses.the.s.57f80a")
                .c_str());
        for (size_t index = 0u; index < node.choices.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const bool open = ImGui::CollapsingHeader(
                ("Option " + std::to_string(index + 1u)).c_str(),
                index == controller_.selectedChoiceIndex_.value_or(0u) ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            if (open) {
                controller_.selectedChoiceIndex_ = index;
                auto before = definition_;
                if (DrawLocalizedText(TEXT::LocalizeWindow("ui.inspector.worldText.text", "Text").c_str(),
                                      node.choices[index].text, controller_.editingLocale_)) {
                    RecordDialogue(std::move(before), "choice.text");
                }
                Seal();
                std::string target = node.choices[index].nextNodeId;
                if (TargetCombo(TEXT::LocalizeWindow("editor.ui.go.to.8a68d9", "Go to").c_str(), definition_, style_,
                                controller_.editingLocale_, target)) {
                    before = definition_;
                    node.choices[index].nextNodeId = std::move(target);
                    RecordDialogue(std::move(before), "choice.target", false);
                }
                if (node.choices.size() > 1u &&
                    ImGui::Button(TEXT::LocalizeWindow("editor.ui.remove.option.17b06d", "Remove option").c_str())) {
                    before = definition_;
                    node.choices.erase(node.choices.begin() + index);
                    controller_.selectedChoiceIndex_.reset();
                    RecordDialogue(std::move(before), "choice.remove", false);
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::PopID();
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.option.097dcf", "+ Add Option").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            auto before = definition_;
            DIALOGUE::DialogueChoice choice{};
            choice.id = "choice" + std::to_string(node.choices.size() + 1u);
            choice.text.values[controller_.editingLocale_] = "New option";
            choice.nextNodeId = definition_.entryNodeId;
            node.choices.push_back(std::move(choice));
            RecordDialogue(std::move(before), "choice.add", false);
        }
    }

    void DrawEventStep(DIALOGUE::DialogueNode& node) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.signal.step.82a95a").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.emit.a.named.event.then.continue.trigger.sequences.can.wait.for..49174a")
                .c_str());
        auto before = definition_;
        std::string eventId = node.eventId;
        if (InputString(TEXT::LocalizeWindow("editor.ui.event.ad8919", "Event").c_str(), eventId)) {
            node.eventId = std::move(eventId);
            RecordDialogue(std::move(before), "event.name");
        }
        Seal();
        DrawNextStep(node, "event.next");
    }

    void DrawStepAdvanced(DIALOGUE::DialogueNode& node) {
        if (!ImGui::CollapsingHeader(TEXT::LocalizeWindow("ui.vfx.layer.advanced", "Advanced").c_str()))
            return;
        if (controller_.nodeIdDraftOwner_ != node.id) {
            controller_.nodeIdDraftOwner_ = node.id;
            controller_.nodeIdDraft_ = node.id;
        }
        (void)InputString(TEXT::LocalizeWindow("editor.ui.step.id.8d5440", "Step id").c_str(), controller_.nodeIdDraft_,
                          false, 128u);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            (void)controller_.RenameSelectedNode(controller_.nodeIdDraft_, Status());
        }
        int type = static_cast<int>(node.type);
        const char* types[]{
            TEXT::Localize("editor.ui.line.ea9676").c_str(), TEXT::Localize("editor.ui.choice.0682be").c_str(),
            TEXT::Localize("editor.ui.event.ad8919").c_str(), TEXT::Localize("ui.vfx.text.End").c_str()};
        if (ImGui::Combo(TEXT::LocalizeWindow("editor.ui.step.type.5dd37b", "Step type").c_str(), &type, types, 4)) {
            auto before = definition_;
            node.type = static_cast<DIALOGUE::DialogueNodeType>(type);
            RecordDialogue(std::move(before), "node.type", false);
        }
    }

    void DrawUnsavedConfirmation() {
        const auto unsavedTitle =
            TEXT::LocalizeWindow("editor.ui.unsaved.dialogue.documents.8c230c", "Dialogue/UnsavedDocuments");
        if (controller_.ConsumePendingConfirmationOpenRequest()) {
            ImGui::OpenPopup(unsavedTitle.c_str());
        }
        if (!ImGui::BeginPopupModal(unsavedTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextWrapped("%s", controller_.GetPendingConfirmationText());
        ImGui::Spacing();
        if (controller_.PendingActionSupportsSave()) {
            if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Save and Continue", "Save and Continue").c_str(),
                              ImVec2(150.0f, 0.0f))) {
                if (controller_.ConfirmSaveThenContinue(scene_, Status())) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Discard and Continue", "Discard and Continue").c_str(),
                          ImVec2(170.0f, 0.0f))) {
            if (controller_.ConfirmDiscardAndContinue(scene_, Status())) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(90.0f, 0.0f))) {
            controller_.CancelPending();
            Status() = "Dialogue action cancelled";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    DialogueWorkspaceController& controller_;
    DocumentSceneBase& scene_;
    AssetDatabase& database_;
    DIALOGUE::DialogueDefinition& definition_;
    DIALOGUE::DialogueProjectStyle& style_;
};

void DialogueWorkspaceController::DrawDockSpace(bool resetDefaultDockLayout) const {
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // v3 invalidates the incomplete layout produced by the first formal
    // workspace pass. A leaf root is also repaired automatically instead
    // of requiring the user to find the global layout reset command.
    const ImGuiID dockspaceId = ImGui::GetID("HIKARI_DialogueAuthoringDockSpace_v3");
    ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
    const bool needsDefaultLayout = dockNode == nullptr || dockNode->IsLeafNode();
    if (needsDefaultLayout || resetDefaultDockLayout) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
        ImGuiID center = dockspaceId;
        ImGuiID left = 0u;
        ImGuiID right = 0u;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.23f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, &right, &center);
        ImGui::DockBuilderDockWindow("Dialogue Tools###Dialogue/Navigator", left);
        ImGui::DockBuilderDockWindow("Canvas###Dialogue/Preview", center);
        ImGui::DockBuilderDockWindow("Properties###Dialogue/Inspector", right);
        ImGui::DockBuilderFinish(dockspaceId);
    }
    HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport);
}

void DrawDialogueWorkspaceWindows(DialogueWorkspaceController& controller, DocumentSceneBase& scene) {
    DialogueWorkspaceWindows(controller, scene).Draw();
}

} // namespace HIKARI::EDITOR
