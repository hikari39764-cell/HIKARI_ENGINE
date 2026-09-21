#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
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
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialoguePreviewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        constexpr ImVec4 kAccent{ 0.20f, 0.58f, 1.0f, 1.0f };
        constexpr ImVec4 kModified{ 1.0f, 0.72f, 0.25f, 1.0f };
        constexpr ImVec4 kError{ 1.0f, 0.38f, 0.35f, 1.0f };
        constexpr ImVec4 kReady{ 0.36f, 0.86f, 0.58f, 1.0f };


        uint64_t MergeGroup(std::string_view field) noexcept {
            uint64_t hash = 1469598103934665603ull;
            for (const unsigned char character : field) {
                hash ^= character;
                hash *= 1099511628211ull;
            }
            return hash != 0u ? hash : 1u;
        }

        bool ContainsCaseInsensitive(
            std::string_view value,
            std::string_view filter) {
            if (filter.empty()) {
                return true;
            }
            const auto lower = [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            };
            std::string loweredValue(value);
            std::string loweredFilter(filter);
            std::transform(
                loweredValue.begin(), loweredValue.end(),
                loweredValue.begin(), lower);
            std::transform(
                loweredFilter.begin(), loweredFilter.end(),
                loweredFilter.begin(), lower);
            return loweredValue.find(loweredFilter) != std::string::npos;
        }

        bool InputString(
            const char* label,
            std::string& value,
            bool multiline = false,
            size_t minimumCapacity = 512u,
            float multilineHeight = 150.0f) {
            const size_t capacity = (std::min)(
                (std::max)(minimumCapacity, value.size() + 512u),
                size_t{ 65536u });
            std::vector<char> buffer(capacity, '\0');
            if (!value.empty()) {
                std::memcpy(
                    buffer.data(),
                    value.data(),
                    (std::min)(value.size(), capacity - 1u));
            }
            const bool changed = multiline
                ? ImGui::InputTextMultiline(
                    label,
                    buffer.data(),
                    buffer.size(),
                    ImVec2(-1.0f, multilineHeight))
                : ImGui::InputText(
                    label,
                    buffer.data(),
                    buffer.size());
            if (changed) value.assign(buffer.data());
            return changed;
        }

        std::string_view ResolveName(
            const DIALOGUE::DialogueSpeakerProfile& speaker,
            std::string_view locale,
            std::string_view fallback) {
            const std::string_view name = DIALOGUE::ResolveLocalizedText(
                speaker.displayName,
                locale,
                fallback);
            return name.empty() ? std::string_view(speaker.id) : name;
        }

        std::string NodeSummary(
            const DIALOGUE::DialogueNode& node,
            const DIALOGUE::DialogueProjectStyle& style,
            std::string_view locale,
            std::string_view fallback) {
            if (node.type == DIALOGUE::DialogueNodeType::End) return TEXT::Localize("ui.vfx.text.End");
            if (node.type == DIALOGUE::DialogueNodeType::Event) {
                return node.eventId.empty()
                    ? TEXT::Localize("editor.ui.event.ad8919")
                    : TEXT::Localize("editor.ui.event.ad8919") + "  |  " + node.eventId;
            }
            if (node.type == DIALOGUE::DialogueNodeType::Choice) {
                return TEXT::Localize("editor.ui.choice.0682be") + "  |  " +
                    std::to_string(node.choices.size()) + " " + TEXT::Localize("editor.ui.branches.52a9ce");
            }
            std::string prefix = TEXT::Localize("editor.ui.narration.8ca626");
            if (const auto* speaker = DIALOGUE::FindDialogueSpeaker(
                    style, node.speakerId)) {
                prefix = std::string(ResolveName(
                    *speaker, locale, fallback));
            }
            std::string_view text = DIALOGUE::ResolveLocalizedText(
                node.text, locale, fallback);
            constexpr size_t kSummaryLimit = 42u;
            std::string excerpt(text.substr(
                0u,
                (std::min)(text.size(), kSummaryLimit)));
            if (text.size() > kSummaryLimit) excerpt += "...";
            return prefix + (excerpt.empty() ? std::string{} : "  |  " + excerpt);
        }

        bool TargetCombo(
            const char* label,
            const DIALOGUE::DialogueDefinition& definition,
            const DIALOGUE::DialogueProjectStyle& style,
            std::string_view locale,
            std::string& target) {
            const DIALOGUE::DialogueNode* current =
                DIALOGUE::FindDialogueNode(definition, target);
            const std::string preview = current != nullptr
                ? NodeSummary(
                    *current, style, locale, definition.fallbackLocale)
                : TEXT::Localize("editor.ui.choose.next.step.e15d49");
            bool changed = false;
            if (ImGui::BeginCombo(label, preview.c_str())) {
                for (const DIALOGUE::DialogueNode& node : definition.nodes) {
                    const bool selected = node.id == target;
                    const std::string summary = NodeSummary(
                        node, style, locale, definition.fallbackLocale);
                    ImGui::PushID(node.id.c_str());
                    if (ImGui::Selectable(summary.c_str(), selected)) {
                        target = node.id;
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        std::string AssetRelativePath(
            const AssetDatabase& database,
            const AssetRecord& record) {
            std::filesystem::path source = record.sourcePath;
            if (!source.is_absolute()) source = database.GetProjectRoot() / source;
            std::error_code error{};
            const std::filesystem::path relative = std::filesystem::relative(
                source.lexically_normal(), database.GetAssetsRoot(), error);
            if (error || relative.empty() || relative.has_root_path()) return {};
            for (const auto& part : relative) {
                if (part == "..") return {};
            }
            return relative.generic_string();
        }

        bool DrawTextureAssetField(
            const char* label,
            const AssetDatabase& database,
            std::string& assetPath) {
            std::string guid{};
            for (const AssetRecord* record :
                 database.CollectByType(AssetType::Texture)) {
                if (record != nullptr &&
                    AssetRelativePath(database, *record) == assetPath) {
                    guid = record->guid.value;
                    break;
                }
            }
            AssetFieldOptions options{};
            options.label = label;
            options.requiredType = AssetType::Texture;
            options.allowCopyGuid = false;
            options.allowLocate = false;
            if (!DrawAssetField(&database, options, guid)) return false;
            if (guid.empty()) {
                assetPath.clear();
                return true;
            }
            const AssetRecord* record = database.FindByGuid(AssetGuid{ guid });
            if (record == nullptr || record->type != AssetType::Texture) {
                return false;
            }
            assetPath = AssetRelativePath(database, *record);
            return true;
        }

        bool DrawFontAssetField(
            const AssetDatabase& database,
            std::string& assetPath) {
            const char* preview = assetPath.empty()
                ? "Choose a CJK font"
                : assetPath.c_str();
            bool changed = false;
            if (!ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.font.b97c4d", "Font").c_str(), preview)) return false;

            static std::array<char, 192u> search{};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##DialogueFontSearch",TEXT::Localize("editor.ui.search.ttf.otf.or.ttc.dea4d2").c_str(),
                search.data(),
                search.size());
            std::string filter{ search.data() };
            std::transform(
                filter.begin(), filter.end(), filter.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });

            std::vector<std::string> fonts{};
            std::error_code iteratorError{};
            const std::filesystem::path root = database.GetAssetsRoot();
            for (std::filesystem::recursive_directory_iterator iterator(
                     root,
                     std::filesystem::directory_options::skip_permission_denied,
                     iteratorError), end;
                 iterator != end && !iteratorError;
                 iterator.increment(iteratorError)) {
                if (!iterator->is_regular_file(iteratorError) || iteratorError) {
                    iteratorError.clear();
                    continue;
                }
                std::string extension = iterator->path().extension().string();
                std::transform(
                    extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                if (extension != ".ttf" && extension != ".otf" &&
                    extension != ".ttc") {
                    continue;
                }
                std::error_code relativeError{};
                const std::filesystem::path relative =
                    std::filesystem::relative(
                        iterator->path(), root, relativeError);
                if (relativeError || relative.empty() ||
                    relative.has_root_path()) {
                    continue;
                }
                std::string candidate = relative.generic_string();
                std::string folded = candidate;
                std::transform(
                    folded.begin(), folded.end(), folded.begin(),
                    [](unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                if (!filter.empty() && folded.find(filter) == std::string::npos) {
                    continue;
                }
                fonts.push_back(std::move(candidate));
            }
            std::sort(fonts.begin(), fonts.end());
            if (fonts.empty()) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.no.matching.font.in.assets.7f6a64").c_str());
            }
            for (const std::string& font : fonts) {
                const bool selected = font == assetPath;
                if (ImGui::Selectable(font.c_str(), selected)) {
                    assetPath = font;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
            return changed;
        }

        bool DrawLocalizedText(
            const char* label,
            DIALOGUE::LocalizedText& text,
            std::string_view locale,
            bool multiline = false,
            float height = 150.0f) {
            std::string value{};
            if (const auto found = text.values.find(std::string(locale));
                found != text.values.end()) {
                value = found->second;
            }
            if (!InputString(
                    label, value, multiline,
                    multiline ? 4096u : 512u, height)) {
                return false;
            }
            text.values[std::string(locale)] = std::move(value);
            return true;
        }

        bool DrawColor(const char* label, DIALOGUE::DialogueColor& color) {
            float values[4]{ color.r, color.g, color.b, color.a };
            if (!ImGui::ColorEdit4(
                    label, values,
                    ImGuiColorEditFlags_Float |
                        ImGuiColorEditFlags_AlphaBar)) {
                return false;
            }
            color = { values[0], values[1], values[2], values[3] };
            return true;
        }

        bool DrawTextStyle(
            const char* id,
            DIALOGUE::DialogueTextStyle& style) {
            bool changed = false;
            ImGui::PushID(id);
            changed = DrawColor(TEXT::LocalizeWindow("ui.inspector.worldText.text", "Text").c_str(), style.textColor) || changed;
            changed = DrawColor(TEXT::LocalizeWindow("editor.ui.outline.cdd629", "Outline").c_str(), style.outlineColor) || changed;
            changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.outline.thickness.e7aab6", "Outline thickness").c_str(), &style.outlineWidth,
                0.0f, 8.0f, "%.2f") || changed;
            if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.shadow.and.glow.84bc9d", "Shadow and glow").c_str())) {
                changed = DrawColor(TEXT::LocalizeWindow("ui.debugView.shadow", "Shadow").c_str(), style.shadowColor) || changed;
                changed = ImGui::DragFloat2(TEXT::LocalizeWindow("editor.ui.shadow.offset.2d8a71", "Shadow offset").c_str(), &style.shadowOffsetX,
                    0.1f, -32.0f, 32.0f, "%.1f") || changed;
                changed = DrawColor(TEXT::LocalizeWindow("ui.vfx.effect.Glow", "Glow").c_str(), style.glowColor) || changed;
                changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.glow.size.f2e05b", "Glow size").c_str(), &style.glowWidth, 0.0f, 12.0f) || changed;
                changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.glow.strength.24792c", "Glow strength").c_str(), &style.glowIntensity,
                    0.0f, 4.0f) || changed;
            }
            if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.spacing.2acbb3", "Spacing").c_str())) {
                changed = ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.letter.spacing.71ae71", "Letter spacing").c_str(), &style.letterSpacing,
                    0.1f, -8.0f, 32.0f) || changed;
                changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.line.height.65be51", "Line height").c_str(), &style.lineHeightMultiplier,
                    0.7f, 2.5f) || changed;
            }
            ImGui::PopID();
            return changed;
        }

        bool DrawFullWidthSelectable(
            const char* label,
            bool selected,
            float height = 0.0f) {
            // Selectable intentionally does not support negative widths.
            // A zero width is its documented fill-available-width contract.
            return ImGui::Selectable(
                label,
                selected,
                ImGuiSelectableFlags_None,
                ImVec2(0.0f, height));
        }

        const char* StyleSectionLabel(DialogueStyleSection section) {
            switch (section) {
            case DialogueStyleSection::Canvas: return TEXT::LocalizeCStr("editor.ui.canvas.artwork.74a94b");
            case DialogueStyleSection::Typography: return TEXT::LocalizeCStr("editor.ui.typography.f4beb9");
            case DialogueStyleSection::Portraits: return TEXT::LocalizeCStr("editor.ui.portrait.layout.216ada");
            case DialogueStyleSection::Characters: return TEXT::LocalizeCStr("editor.ui.characters.b6f59f");
            case DialogueStyleSection::Motion: return TEXT::LocalizeCStr("editor.ui.motion.backdrop.97e967");
            default: return TEXT::LocalizeCStr("editor.ui.canvas.artwork.74a94b");
            }
        }

        const char* StyleSectionNavigationLabel(
            DialogueStyleSection section) {
            switch (section) {
            case DialogueStyleSection::Canvas:
                return TEXT::LocalizeCStr("editor.ui.1.canvas.artwork.cd3827");
            case DialogueStyleSection::Typography:
                return TEXT::LocalizeCStr("editor.ui.2.typography.40c242");
            case DialogueStyleSection::Portraits:
                return TEXT::LocalizeCStr("editor.ui.3.portrait.layout.eb1372");
            case DialogueStyleSection::Characters:
                return TEXT::LocalizeCStr("editor.ui.4.characters.0b3db3");
            case DialogueStyleSection::Motion:
                return TEXT::LocalizeCStr("editor.ui.5.motion.backdrop.44cfe9");
            default:
                return TEXT::LocalizeCStr("editor.ui.1.canvas.artwork.cd3827");
            }
        }

        const char* StyleSectionDescription(DialogueStyleSection section) {
            switch (section) {
            case DialogueStyleSection::Canvas:
                return TEXT::LocalizeCStr("editor.ui.choose.the.dialogue.name.and.choice.artwork.then.position.it.on..368953");
            case DialogueStyleSection::Typography:
                return TEXT::LocalizeCStr("editor.ui.choose.one.project.font.and.author.readable.name.body.and.choice.1524d2");
            case DialogueStyleSection::Portraits:
                return TEXT::LocalizeCStr("editor.ui.place.the.two.portrait.slots.used.by.every.dialogue.event.b88fe6");
            case DialogueStyleSection::Characters:
                return TEXT::LocalizeCStr("editor.ui.register.reusable.names.portraits.and.per.character.alignment.99128c");
            case DialogueStyleSection::Motion:
                return TEXT::LocalizeCStr("editor.ui.tune.reveal.portrait.focus.and.the.treatment.of.the.game.view.be.1b7ccb");
            default:
                return TEXT::LocalizeCStr("editor.ui.build.the.project.wide.dialogue.presentation.51ee01");
            }
        }

        const char* NodeTypeLabel(DIALOGUE::DialogueNodeType type) {
            switch (type) {
            case DIALOGUE::DialogueNodeType::Line: return TEXT::LocalizeCStr("editor.ui.line.528a45");
            case DIALOGUE::DialogueNodeType::Choice: return TEXT::LocalizeCStr("editor.ui.choice.20d9bc");
            case DIALOGUE::DialogueNodeType::Event: return TEXT::LocalizeCStr("editor.ui.signal.289e7c");
            case DIALOGUE::DialogueNodeType::End: return TEXT::LocalizeCStr("editor.ui.end.d205ab");
            default: return TEXT::LocalizeCStr("editor.ui.step.0c9b85");
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
    }
#endif

    void DialogueWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) return;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        // v3 invalidates the incomplete layout produced by the first formal
        // workspace pass. A leaf root is also repaired automatically instead
        // of requiring the user to find the global layout reset command.
        const ImGuiID dockspaceId = ImGui::GetID(
            "HIKARI_DialogueAuthoringDockSpace_v3");
        ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
        const bool needsDefaultLayout = dockNode == nullptr ||
            dockNode->IsLeafNode();
        if (needsDefaultLayout || resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
            ImGuiID center = dockspaceId;
            ImGuiID left = 0u;
            ImGuiID right = 0u;
            ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Left, 0.23f, &left, &center);
            ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Right, 0.30f, &right, &center);
            ImGui::DockBuilderDockWindow(
                "Dialogue Tools###Dialogue/Navigator", left);
            ImGui::DockBuilderDockWindow(
                "Canvas###Dialogue/Preview", center);
            ImGui::DockBuilderDockWindow(
                "Properties###Dialogue/Inspector", right);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    void DrawDialogueWorkspaceWindows(
        DialogueWorkspaceController& controller,
        DocumentSceneBase& scene,
        EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
        (void)commandRouter;
        AssetDatabase& database = scene.GetAssetDatabase();
        auto& definition = controller.document_.Definition();
        auto& style = controller.styleDocument_.Style();

        const auto recordDialogue = [&](DIALOGUE::DialogueDefinition before,
                                        std::string_view field,
                                        bool continuous = true) {
            controller.RecordMutation(
                std::move(before), continuous ? MergeGroup(field) : 0u);
        };
        const auto recordStyle = [&](DIALOGUE::DialogueProjectStyle before,
                                     std::string_view field,
                                     bool continuous = true) {
            controller.RecordStyleMutation(
                std::move(before), continuous ? MergeGroup(field) : 0u);
        };
        const auto seal = [&controller]() {
            if (!ImGui::IsItemDeactivatedAfterEdit()) return;
            controller.document_.SealMerge();
            controller.styleDocument_.SealMerge();
        };

        if (ImGui::Begin(TEXT::LocalizeWindow("editor.ui.dialogue.tools.097275", "Dialogue/Navigator").c_str())) {
            bool stylePage =
                controller.activePage_ == DialogueWorkspacePage::Style;
            ImGui::TextColored(kAccent,TEXT::Localize("editor.ui.dialogue.authoring.ca704f").c_str());
            ImGui::TextDisabled(TEXT::Localize("editor.ui.project.presentation.and.conversation.events.20b37f").c_str());
            ImGui::Spacing();
            if (ImGui::BeginTable(
                    "##DialoguePageTabs", 2,
                    ImGuiTableFlags_SizingStretchSame |
                        ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableNextColumn();
                if (IconTextButton(EditorGlyph::Material, TEXT::LocalizeCStr("editor.dialogue.style"), "stylePage", stylePage ? EditorButtonTone::Primary : EditorButtonTone::Quiet, {-1,36})) {
                    controller.activePage_ = DialogueWorkspacePage::Style;
                    stylePage = true;
                }
                ImGui::TableNextColumn();
                if (IconTextButton(EditorGlyph::Sequence, TEXT::LocalizeCStr("editor.dialogue.event"), "storyPage", !stylePage ? EditorButtonTone::Primary : EditorButtonTone::Quiet, {-1,36})) {
                    controller.activePage_ = DialogueWorkspacePage::Story;
                    stylePage = false;
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            if (ImGui::BeginTable(
                    "##DialogueDocumentActions", 3,
                    ImGuiTableFlags_SizingStretchSame |
                        ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableNextColumn();
                if (IconButton(EditorGlyph::Save, "dialogueSave", EditorButtonTone::Quiet, {ImGui::GetContentRegionAvail().x, 28}, TEXT::LocalizeCStr("ai.api.save"))) {
                    (void)controller.Save(scene, controller.statusMessage_);
                }
                const bool canUndo = stylePage
                    ? controller.styleDocument_.CanUndo()
                    : controller.document_.CanUndo();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!canUndo);
                if (IconButton(EditorGlyph::Undo, "dialogueUndo", EditorButtonTone::Quiet, {ImGui::GetContentRegionAvail().x, 28}, TEXT::LocalizeCStr("menu.edit.undo"))) {
                    (void)controller.Undo(controller.statusMessage_);
                }
                ImGui::EndDisabled();
                const bool canRedo = stylePage
                    ? controller.styleDocument_.CanRedo()
                    : controller.document_.CanRedo();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!canRedo);
                if (IconButton(EditorGlyph::Redo, "dialogueRedo", EditorButtonTone::Quiet, {ImGui::GetContentRegionAvail().x, 28}, TEXT::LocalizeCStr("menu.edit.redo"))) {
                    (void)controller.Redo(controller.statusMessage_);
                }
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            const bool activeDirty = stylePage
                ? controller.styleDocument_.IsDirty()
                : controller.document_.IsDirty();
            if (activeDirty) {
                ImGui::TextColored(
                    kModified,
                    stylePage ? TEXT::LocalizeCStr("editor.ui.ui.style.has.unsaved.changes.b18536")
                              : TEXT::LocalizeCStr("editor.ui.dialogue.event.has.unsaved.changes.b4dc55"));
            }
            ImGui::Separator();
            const char* locales[]{ "ja-JP", "en-US", "zh-CN" };
            ImGui::TextDisabled(TEXT::Localize("editor.ui.editing.language.b32616").c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo(
                    "##DialogueEditingLanguage",
                    controller.editingLocale_.c_str())) {
                for (const char* locale : locales) {
                    const bool selected = controller.editingLocale_ == locale;
                    if (ImGui::Selectable(locale, selected)) {
                        controller.editingLocale_ = locale;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const float footerHeight =
                ImGui::GetFrameHeightWithSpacing() * 2.0f;
            if (ImGui::BeginChild(
                    "##DialogueNavigatorBody",
                    ImVec2(0.0f, -footerHeight),
                    false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            if (stylePage) {
                ImGui::SeparatorText(TEXT::Localize("editor.ui.style.workflow.8f19d1").c_str());
                ImGui::TextWrapped(TEXT::Localize("editor.ui.start.with.artwork.and.typography.register.the.cast.then.tune.mo.b28632").c_str());
                ImGui::Spacing();
                for (uint8_t value = 0u;
                     value <= static_cast<uint8_t>(DialogueStyleSection::Motion);
                     ++value) {
                    const auto section = static_cast<DialogueStyleSection>(value);
                    if (DrawFullWidthSelectable(
                            StyleSectionNavigationLabel(section),
                            controller.styleSection_ == section,
                            32.0f)) {
                        controller.styleSection_ = section;
                    }
                    if (controller.styleSection_ == section) {
                        ImGui::Indent(10.0f);
                        ImGui::PushTextWrapPos();
                        ImGui::TextDisabled("%s", StyleSectionDescription(section));
                        ImGui::PopTextWrapPos();
                        ImGui::Unindent(10.0f);
                    }
                }
                if (controller.styleSection_ == DialogueStyleSection::Characters) {
                    ImGui::SeparatorText(TEXT::Localize("editor.ui.project.characters.aabbd7").c_str());
                    if (style.speakers.empty()) {
                        ImGui::TextWrapped(TEXT::Localize("editor.ui.no.characters.are.registered.yet.add.one.here.then.assign.its.lo.9c1301").c_str());
                    }
                    for (const auto& speaker : style.speakers) {
                        const std::string label = std::string(ResolveName(
                            speaker, controller.editingLocale_,
                            style.fallbackLocale));
                        ImGui::PushID(speaker.id.c_str());
                        if (DrawFullWidthSelectable(
                                label.c_str(),
                                controller.selectedSpeakerId_ == speaker.id,
                                28.0f)) {
                            controller.selectedSpeakerId_ = speaker.id;
                        }
                        ImGui::PopID();
                    }
                    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
                        (void)controller.DeleteSelectedSpeaker(controller.statusMessage_);
                    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.character.ccbd86", "+ Add Character").c_str(), ImVec2(-1.0f, 0.0f))) {
                        controller.AddSpeaker();
                    }
                }
            }
            else {
                ImGui::SeparatorText(TEXT::Localize("editor.ui.event.workflow.8209d5").c_str());
                ImGui::TextWrapped(TEXT::Localize("editor.ui.one.dialogue.asset.is.one.complete.conversation.add.steps.below..8f74ad").c_str());
                ImGui::Spacing();
                ImGui::SeparatorText(TEXT::Localize("editor.ui.dialogue.assets.8ddd00").c_str());
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##DialogueAssetSearch",TEXT::Localize("editor.ui.search.dialogue.assets.82112b").c_str(),
                    controller.assetSearch_.data(),
                    controller.assetSearch_.size());
                const std::string_view search(
                    controller.assetSearch_.data());
                const std::vector<DialogueAssetListEntry> assets =
                    controller.ListAssets(database);
                bool listedAsset = false;
                for (const DialogueAssetListEntry& asset : assets) {
                    if (!ContainsCaseInsensitive(asset.name, search) &&
                        !ContainsCaseInsensitive(
                            asset.sourcePath.generic_string(), search)) {
                        continue;
                    }
                    listedAsset = true;
                    const std::string label =
                        (asset.current ? "[OPEN]  " : "") + asset.name +
                        "##DialogueAsset/" + asset.guid.value;
                    if (DrawFullWidthSelectable(
                            label.c_str(), asset.current, 30.0f)) {
                        (void)controller.RequestOpenAsset(
                            database,
                            asset.guid,
                            controller.statusMessage_);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "%s",
                            asset.sourcePath.generic_string().c_str());
                    }
                }
                if (!listedAsset) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.no.matching.dialogue.assets.5b559b").c_str());
                }
                if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.new.dialogue.event.5e0581", "New Dialogue Event").c_str(), ImVec2(-1.0f, 0.0f))) {
                    (void)controller.RequestNewAsset(
                        controller.statusMessage_);
                }

                if (!controller.document_.IsOpen()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.open.an.existing.dialogue.asset.above.or.create.a.new.event.44bf96").c_str());
                }
                else {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.current.event.f840cb").c_str());
                ImGui::TextWrapped(
                    "%s%s",
                    definition.name.c_str(),
                    controller.document_.IsDirty() ? "  *" : "");
                if (ImGui::BeginTable(
                        "##DialogueCurrentActions",
                        2,
                        ImGuiTableFlags_SizingStretchSame |
                            ImGuiTableFlags_NoPadOuterX)) {
                    ImGui::TableNextColumn();
                    if (ImGui::Button(TEXT::LocalizeWindow("ui.assetInspector.revert", "Revert").c_str(),
                            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                        (void)controller.RequestRevert(
                            database,
                            controller.statusMessage_);
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button(TEXT::LocalizeWindow("ui.input.close", "Close").c_str(),
                            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                        (void)controller.RequestCloseAsset(
                            controller.statusMessage_);
                    }
                    ImGui::EndTable();
                }
                ImGui::SeparatorText(TEXT::Localize("editor.ui.story.steps.3bbc62").c_str());
                if (ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.auto.link.inserted.step.85d8b6", "Auto-link inserted step").c_str(),
                        &controller.autoBindNewNodes_)) {
                    controller.statusMessage_ = controller.autoBindNewNodes_
                        ? "New story steps will be linked automatically"
                        : "Automatic story-step linking disabled";
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(TEXT::Localize("editor.ui.insert.after.the.selected.step.link.the.current.continuation.to..a3c741").c_str());
                }
                if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.story.step.8e7a80", "+ Add Story Step").c_str(), ImVec2(-1.0f, 0.0f))) {
                    ImGui::OpenPopup("AddDialogueStep");
                }
                if (ImGui::BeginPopup("AddDialogueStep")) {
                    if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.line.ea9676", "Line").c_str())) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::Line);
                    }
                    if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.choice.0682be", "Choice").c_str())) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::Choice);
                    }
                    if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.event.signal.58ef83", "Event signal").c_str())) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::Event);
                    }
                    if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.vfx.text.End", "End").c_str())) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::End);
                    }
                    ImGui::EndPopup();
                }
                ImGui::Spacing();
                for (size_t index = 0u; index < definition.nodes.size(); ++index) {
                    const auto& node = definition.nodes[index];
                    const bool selected = controller.selectedNodeId_ == node.id;
                    const std::string label = std::to_string(index + 1u) +
                        (node.id == definition.entryNodeId ? "  [START]" : "") +
                        "  [" + NodeTypeLabel(node.type) + "]  " + NodeSummary(
                            node, style, controller.editingLocale_,
                            definition.fallbackLocale);
                    ImGui::PushID(node.id.c_str());
                    if (DrawFullWidthSelectable(
                            label.c_str(), selected, 38.0f)) {
                        controller.selectedNodeId_ = node.id;
                        controller.selectedChoiceIndex_.reset();
                        controller.nodeIdDraftOwner_.clear();
                    }
                    ImGui::PopID();
                }
                ImGui::Spacing();
                ImGui::BeginDisabled(
                    !controller.CanBindSelectedToNextNode());
                if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.bind.selected.to.next.step.567a41", "Bind Selected to Next Step").c_str(),
                        ImVec2(-1.0f, 0.0f))) {
                    (void)controller.BindSelectedToNextNode(
                        controller.statusMessage_);
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(TEXT::Localize("editor.ui.link.this.line.or.event.to.the.following.list.item.for.a.choice..09ae09").c_str());
                }
                ImGui::BeginDisabled(definition.nodes.size() <= 1u);
                if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.remove.step.9fcbf9", "Remove Step").c_str(), ImVec2(-1.0f, 0.0f)) ||
                    (definition.nodes.size() > 1 && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))) {
                    (void)controller.DeleteSelectedNode(controller.statusMessage_);
                }
                ImGui::EndDisabled();
                }
            }
            }
            ImGui::EndChild();
            ImGui::Separator();
            if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.back.to.scene.0e730f", "Back to Scene").c_str(), ImVec2(-1.0f, 0.0f))) {
                EditorWorkspaceOpenRequest request{};
                request.workspaceId = EditorWorkspaceId::Scene;
                (void)controller.RequestWorkspaceTransition(
                    std::move(request),
                    controller.statusMessage_);
            }
        }
        ImGui::End();

        if (ImGui::Begin(TEXT::LocalizeWindow("ui.sceneCreation.canvas", "Dialogue/Preview").c_str())) {
            ImGui::TextColored(
                kAccent,
                controller.activePage_ == DialogueWorkspacePage::Style
                    ? TEXT::LocalizeCStr("editor.ui.ui.style.canvas.c3bc93") : TEXT::LocalizeCStr("editor.ui.dialogue.event.preview.ef3e4d"));
            ImGui::SameLine();
            ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.animate.e733b2", "Animate").c_str(), &controller.previewPlaying_);
            ImGui::SameLine();
            if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.replay.c0f85d", "Replay").c_str())) controller.previewElapsed_ = 0.0f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.worldText.text", "Text").c_str(), &controller.previewRevealAmount_,
                0.0f, 1.0f, "%.2f");
            if (controller.activePage_ == DialogueWorkspacePage::Style) {
                ImGui::SameLine();
                ImGui::TextDisabled(TEXT::Localize("editor.ui.select.an.outline.to.move.it.drag.its.lower.right.handle.to.resi.81f6b1").c_str());
            }
            if (controller.previewPlaying_) {
                controller.previewElapsed_ += (std::max)(
                    ImGui::GetIO().DeltaTime, 0.0f);
            }

            DIALOGUE::DialogueNode previewSample{};
            DIALOGUE::DialogueNode* previewNode = controller.SelectedNode();
            if (controller.activePage_ == DialogueWorkspacePage::Style ||
                previewNode == nullptr) {
                previewSample.id = "preview";
                previewSample.type = DIALOGUE::DialogueNodeType::Line;
                previewSample.speakerId = controller.selectedSpeakerId_;
                previewSample.text.values["en-US"] =
                    "This is how dialogue will appear in the game.";
                previewSample.text.values["ja-JP"] =
                    "ゲーム内の会話表示をここで確認できます。";
                previewSample.text.values["zh-CN"] =
                    "可以在这里确认游戏中的实际对话效果。";
                previewNode = &previewSample;
            }

            const float referenceWidth = (std::max)(
                style.theme.referenceWidth, 1.0f);
            const float referenceHeight = (std::max)(
                style.theme.referenceHeight, 1.0f);
            const float aspect = referenceWidth / referenceHeight;
            ImVec2 available = ImGui::GetContentRegionAvail();
            available.y -= ImGui::GetTextLineHeightWithSpacing();
            // Keep the canvas inside its dock even while the user is resizing
            // panels. Large hard minimums make sibling windows appear missing.
            available.x = (std::max)(available.x, 64.0f);
            available.y = (std::max)(available.y, 64.0f);
            ImVec2 canvas = available;
            if (canvas.x / canvas.y > aspect) canvas.x = canvas.y * aspect;
            else canvas.y = canvas.x / aspect;
            const ImVec2 availableOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 origin{
                availableOrigin.x + (available.x - canvas.x) * 0.5f,
                availableOrigin.y + (available.y - canvas.y) * 0.5f };
            ImGui::SetCursorScreenPos(origin);

            bool previewReady = false;
            if (previewNode != nullptr && controller.previewRenderer_) {
                previewReady = controller.previewRenderer_->Render(
                    controller.projectRoot_, definition, style, *previewNode,
                    controller.editingLocale_,
                    controller.selectedChoiceIndex_.value_or(0u),
                    controller.previewElapsed_,
                    controller.previewRevealAmount_,
                    static_cast<uint32_t>((std::max)(std::round(canvas.x), 1.0f)),
                    static_cast<uint32_t>((std::max)(std::round(canvas.y), 1.0f)));
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE output = controller.previewRenderer_
                ? controller.previewRenderer_->GetOutputSrv()
                : D3D12_GPU_DESCRIPTOR_HANDLE{};
            if (previewReady && output.ptr != 0u) {
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(
                        static_cast<uintptr_t>(output.ptr)), canvas);
            }
            else {
                ImGui::InvisibleButton(
                    "##DialoguePreviewCanvas", canvas,
                    ImGuiButtonFlags_MouseButtonLeft);
            }
            const bool canvasHovered = ImGui::IsItemHovered();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (!previewReady) {
                drawList->AddRectFilled(
                    origin, { origin.x + canvas.x, origin.y + canvas.y },
                    IM_COL32(26, 30, 40, 255));
                const std::string message = controller.previewRenderer_
                    ? controller.previewRenderer_->GetStatus()
                    : "Preview renderer unavailable";
                const ImVec2 textSize = ImGui::CalcTextSize(message.c_str());
                drawList->AddText(
                    { origin.x + (canvas.x - textSize.x) * 0.5f,
                      origin.y + (canvas.y - textSize.y) * 0.5f },
                    IM_COL32(196, 205, 222, 255), message.c_str());
            }

            DIALOGUE::DialogueTheme& theme = style.theme;
            const PreviewRect rects[]{
                { DialoguePreviewElement::LeftPortrait, "Left portrait",
                    style.leftPortrait.x, style.leftPortrait.y,
                    style.leftPortrait.width, style.leftPortrait.height },
                { DialoguePreviewElement::RightPortrait, "Right portrait",
                    style.rightPortrait.x, style.rightPortrait.y,
                    style.rightPortrait.width, style.rightPortrait.height },
                { DialoguePreviewElement::Panel, "Dialogue panel",
                    theme.panelX, theme.panelY, theme.panelWidth, theme.panelHeight },
                { DialoguePreviewElement::SpeakerPlate, "Name plate",
                    theme.speakerPlateX, theme.speakerPlateY,
                    theme.speakerPlateWidth, theme.speakerPlateHeight },
                { DialoguePreviewElement::ChoicePanel, "Choice",
                    theme.choiceX, theme.choiceY,
                    theme.choiceWidth, theme.choiceHeight },
                { DialoguePreviewElement::SpeakerText, "Name text",
                    theme.speakerTextX, theme.speakerTextY,
                    theme.speakerTextWidth, theme.speakerTextHeight },
                { DialoguePreviewElement::BodyText, "Dialogue text",
                    theme.bodyTextX, theme.bodyTextY,
                    theme.bodyTextWidth, theme.bodyTextHeight },
                { DialoguePreviewElement::ChoiceText, "Choice text",
                    theme.choiceX + theme.choiceTextOffsetX,
                    theme.choiceY + theme.choiceTextOffsetY,
                    theme.choiceTextWidth, theme.choiceTextHeight },
            };
            const float scaleX = canvas.x / referenceWidth;
            const float scaleY = canvas.y / referenceHeight;
            const auto minimum = [&](const PreviewRect& rect) {
                return ImVec2{ origin.x + rect.x * scaleX,
                    origin.y + rect.y * scaleY };
            };
            const auto maximum = [&](const PreviewRect& rect) {
                return ImVec2{ origin.x + (rect.x + rect.width) * scaleX,
                    origin.y + (rect.y + rect.height) * scaleY };
            };
            const auto contains = [](const ImVec2& point,
                                     const ImVec2& min,
                                     const ImVec2& max) {
                return point.x >= min.x && point.y >= min.y &&
                    point.x <= max.x && point.y <= max.y;
            };
            const bool canEditCanvas =
                controller.activePage_ == DialogueWorkspacePage::Style;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (canEditCanvas && canvasHovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                DialoguePreviewElement picked = DialoguePreviewElement::None;
                bool resize = false;
                for (const PreviewRect& rect : rects) {
                    if (rect.element != controller.previewElement_) continue;
                    const ImVec2 corner = maximum(rect);
                    if (contains(
                            mouse,
                            { corner.x - 11.0f, corner.y - 11.0f },
                            { corner.x + 5.0f, corner.y + 5.0f })) {
                        picked = rect.element;
                        resize = true;
                    }
                    break;
                }
                if (picked == DialoguePreviewElement::None) {
                    for (auto it = std::rbegin(rects);
                         it != std::rend(rects); ++it) {
                        if (contains(mouse, minimum(*it), maximum(*it))) {
                            picked = it->element;
                            break;
                        }
                    }
                }
                controller.previewElement_ = picked;
                controller.previewDragResize_ = resize;
                controller.previewDragChanged_ = false;
                controller.previewDragMouseX_ = mouse.x;
                controller.previewDragMouseY_ = mouse.y;
                controller.previewStyleDragBefore_ =
                    picked == DialoguePreviewElement::None
                        ? std::optional<DIALOGUE::DialogueProjectStyle>{}
                        : std::optional<DIALOGUE::DialogueProjectStyle>{ style };
            }
            if (controller.previewStyleDragBefore_ &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float dx = (mouse.x - controller.previewDragMouseX_) /
                    (std::max)(scaleX, 1.0e-4f);
                const float dy = (mouse.y - controller.previewDragMouseY_) /
                    (std::max)(scaleY, 1.0e-4f);
                const auto& start = *controller.previewStyleDragBefore_;
                const auto moveOrResize = [&](float& x, float& y,
                                              float& width, float& height,
                                              float sx, float sy,
                                              float sw, float sh) {
                    if (controller.previewDragResize_) {
                        width = (std::max)(sw + dx, 1.0f);
                        height = (std::max)(sh + dy, 1.0f);
                    }
                    else { x = sx + dx; y = sy + dy; }
                };
                switch (controller.previewElement_) {
                case DialoguePreviewElement::Panel:
                    moveOrResize(theme.panelX, theme.panelY,
                        theme.panelWidth, theme.panelHeight,
                        start.theme.panelX, start.theme.panelY,
                        start.theme.panelWidth, start.theme.panelHeight);
                    break;
                case DialoguePreviewElement::SpeakerPlate:
                    moveOrResize(theme.speakerPlateX, theme.speakerPlateY,
                        theme.speakerPlateWidth, theme.speakerPlateHeight,
                        start.theme.speakerPlateX, start.theme.speakerPlateY,
                        start.theme.speakerPlateWidth,
                        start.theme.speakerPlateHeight);
                    break;
                case DialoguePreviewElement::SpeakerText:
                    moveOrResize(theme.speakerTextX, theme.speakerTextY,
                        theme.speakerTextWidth, theme.speakerTextHeight,
                        start.theme.speakerTextX, start.theme.speakerTextY,
                        start.theme.speakerTextWidth,
                        start.theme.speakerTextHeight);
                    break;
                case DialoguePreviewElement::BodyText:
                    moveOrResize(theme.bodyTextX, theme.bodyTextY,
                        theme.bodyTextWidth, theme.bodyTextHeight,
                        start.theme.bodyTextX, start.theme.bodyTextY,
                        start.theme.bodyTextWidth,
                        start.theme.bodyTextHeight);
                    break;
                case DialoguePreviewElement::ChoicePanel:
                    moveOrResize(theme.choiceX, theme.choiceY,
                        theme.choiceWidth, theme.choiceHeight,
                        start.theme.choiceX, start.theme.choiceY,
                        start.theme.choiceWidth, start.theme.choiceHeight);
                    break;
                case DialoguePreviewElement::ChoiceText:
                    if (controller.previewDragResize_) {
                        theme.choiceTextWidth = (std::max)(
                            start.theme.choiceTextWidth + dx, 1.0f);
                        theme.choiceTextHeight = (std::max)(
                            start.theme.choiceTextHeight + dy, 1.0f);
                    }
                    else {
                        theme.choiceTextOffsetX = start.theme.choiceTextOffsetX + dx;
                        theme.choiceTextOffsetY = start.theme.choiceTextOffsetY + dy;
                    }
                    break;
                case DialoguePreviewElement::LeftPortrait:
                    moveOrResize(
                        style.leftPortrait.x, style.leftPortrait.y,
                        style.leftPortrait.width, style.leftPortrait.height,
                        start.leftPortrait.x, start.leftPortrait.y,
                        start.leftPortrait.width, start.leftPortrait.height);
                    break;
                case DialoguePreviewElement::RightPortrait:
                    moveOrResize(
                        style.rightPortrait.x, style.rightPortrait.y,
                        style.rightPortrait.width, style.rightPortrait.height,
                        start.rightPortrait.x, start.rightPortrait.y,
                        start.rightPortrait.width, start.rightPortrait.height);
                    break;
                default: break;
                }
                controller.previewDragChanged_ =
                    std::abs(dx) > 0.01f || std::abs(dy) > 0.01f;
            }
            if (controller.previewStyleDragBefore_ &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (controller.previewDragChanged_) {
                    controller.RecordStyleMutation(
                        std::move(*controller.previewStyleDragBefore_), 0u);
                }
                controller.previewStyleDragBefore_.reset();
                controller.previewDragChanged_ = false;
            }
            if (canEditCanvas) {
                for (const PreviewRect& rect : rects) {
                    const bool selected = rect.element == controller.previewElement_;
                    const ImU32 color = selected
                        ? IM_COL32(255, 210, 64, 235)
                        : IM_COL32(48, 170, 255, 90);
                    const ImVec2 rectMin = minimum(rect);
                    const ImVec2 rectMax = maximum(rect);
                    drawList->AddRect(
                        rectMin, rectMax, color, 0.0f, 0,
                        selected ? 2.0f : 1.0f);
                    if (selected) {
                        drawList->AddRectFilled(
                            { rectMax.x - 7.0f, rectMax.y - 7.0f },
                            { rectMax.x + 3.0f, rectMax.y + 3.0f }, color);
                        drawList->AddText(
                            { rectMin.x + 4.0f, rectMin.y + 3.0f },
                            color, rect.name);
                    }
                }
            }
            ImGui::SetCursorScreenPos(
                { availableOrigin.x, availableOrigin.y + available.y });
            ImGui::Dummy({ 1.0f, 1.0f });
            if (controller.previewRenderer_) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.live.preview.s.8878fb").c_str(),
                    controller.previewRenderer_->GetStatus().c_str());
            }
        }
        ImGui::End();

        if (ImGui::Begin(TEXT::LocalizeWindow("editor.ui.properties.bc6c88", "Dialogue/Inspector").c_str())) {
            if (controller.activePage_ == DialogueWorkspacePage::Style) {
                ImGui::TextColored(
                    kAccent, "%s", StyleSectionLabel(controller.styleSection_));
                ImGui::TextWrapped(
                    "%s", StyleSectionDescription(controller.styleSection_));
                ImGui::Separator();

                if (controller.styleSection_ == DialogueStyleSection::Canvas) {
                    auto before = style;
                    bool changed = DrawTextureAssetField(TEXT::Localize("editor.ui.dialogue.panel.097823").c_str(), database,
                        style.theme.dialoguePanelAssetPath);
                    changed = DrawTextureAssetField(TEXT::Localize("editor.ui.name.plate.55efa6").c_str(), database,
                        style.theme.speakerPlateAssetPath) || changed;
                    changed = DrawTextureAssetField(TEXT::Localize("editor.ui.choice.panel.33406a").c_str(), database,
                        style.theme.choiceAssetPath) || changed;
                    changed = DrawTextureAssetField(TEXT::Localize("editor.ui.continue.gamepad.423af3").c_str(), database,
                        style.theme.advanceGamepadAssetPath) || changed;
                    changed = DrawTextureAssetField(TEXT::Localize("editor.ui.continue.mouse.433032").c_str(), database,
                        style.theme.advanceMouseAssetPath) || changed;
                    if (changed) recordStyle(std::move(before), "style.art", false);
                    ImGui::Spacing();
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.place.and.resize.artwork.directly.on.the.canvas.the.panel.is.tre.43e567").c_str());
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.reference.canvas.29bf4e", "Reference canvas").c_str())) {
                        before = style;
                        float size[2]{ style.theme.referenceWidth,
                            style.theme.referenceHeight };
                        if (ImGui::DragFloat2(TEXT::LocalizeWindow("editor.ui.design.resolution.435501", "Design resolution").c_str(), size, 1.0f,
                                320.0f, 8192.0f, "%.0f")) {
                            style.theme.referenceWidth = size[0];
                            style.theme.referenceHeight = size[1];
                            recordStyle(std::move(before), "style.canvas");
                        }
                        seal();
                    }
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.continue.prompt.fc9ef9", "Continue prompt").c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen)) {
                        before = style;
                        bool promptChanged = ImGui::DragFloat2(TEXT::LocalizeWindow("editor.ui.prompt.position.c2d53e", "Prompt position").c_str(),
                            &style.theme.advancePromptX,
                            1.0f,
                            -4096.0f,
                            4096.0f,
                            "%.0f");
                        promptChanged = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.scale.15563f", "Prompt scale").c_str(),
                            &style.theme.advancePromptScale,
                            0.20f,
                            4.0f,
                            "%.2fx") || promptChanged;
                        if (promptChanged) {
                            recordStyle(
                                std::move(before),
                                "style.advancePromptLayout");
                        }
                        seal();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Typography) {
                    auto before = style;
                    if (DrawFontAssetField(
                            database,
                            style.theme.fontAssetPath)) {
                        recordStyle(std::move(before), "style.font");
                    }
                    seal();
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.use.a.cjk.capable.ttf.otf.or.ttc.for.japanese.and.chinese.a4b049").c_str());
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.character.name.c0a090", "Character name").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        before = style;
                        bool changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.name.size.ef6b3b", "Name size").c_str(), &style.theme.speakerPixelHeight,
                            10.0f, 128.0f, "%.0f px");
                        changed = DrawTextStyle(
                            "NameStyle", style.theme.speakerTextStyle) || changed;
                        if (changed) recordStyle(std::move(before), "style.nameText");
                        seal();
                    }
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.dialogue.text.1bd98a", "Dialogue text").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        before = style;
                        bool changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.body.size.61b95f", "Body size").c_str(), &style.theme.bodyPixelHeight,
                            10.0f, 128.0f, "%.0f px");
                        changed = DrawTextStyle(
                            "BodyStyle", style.theme.bodyTextStyle) || changed;
                        if (changed) recordStyle(std::move(before), "style.bodyText");
                        seal();
                    }
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.choice.text.a9c015", "Choice text").c_str())) {
                        before = style;
                        bool changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.choice.size.ae6b83", "Choice size").c_str(), &style.theme.choicePixelHeight,
                            10.0f, 128.0f, "%.0f px");
                        changed = DrawTextStyle(
                            "ChoiceStyle", style.theme.choiceTextStyle) || changed;
                        if (changed) recordStyle(std::move(before), "style.choiceText");
                        seal();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Portraits) {
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.the.runtime.displays.at.most.two.portraits.move.the.left.and.rig.bd843e").c_str());
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.left.slot.aa2913", "Left slot").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto before = style;
                        bool changed = ImGui::DragFloat2(TEXT::LocalizeWindow("ui.vfx.text.Position", "Position##Left").c_str(), &style.leftPortrait.x,
                            1.0f, -4096.0f, 4096.0f, "%.0f");
                        changed = ImGui::DragFloat2(TEXT::LocalizeWindow("ui.inspector.worldSprite.size", "Size##Left").c_str(), &style.leftPortrait.width,
                            1.0f, 1.0f, 4096.0f, "%.0f") || changed;
                        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.rotation", "Rotation##Left").c_str(), &style.leftPortrait.rotationDegrees,
                            -45.0f, 45.0f, "%.1f deg") || changed;
                        if (changed) recordStyle(std::move(before), "style.leftPortrait");
                        seal();
                    }
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.right.slot.ad5773", "Right slot").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto before = style;
                        bool changed = ImGui::DragFloat2(TEXT::LocalizeWindow("ui.vfx.text.Position", "Position##Right").c_str(), &style.rightPortrait.x,
                            1.0f, -4096.0f, 4096.0f, "%.0f");
                        changed = ImGui::DragFloat2(TEXT::LocalizeWindow("ui.inspector.worldSprite.size", "Size##Right").c_str(), &style.rightPortrait.width,
                            1.0f, 1.0f, 4096.0f, "%.0f") || changed;
                        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.rotation", "Rotation##Right").c_str(), &style.rightPortrait.rotationDegrees,
                            -45.0f, 45.0f, "%.1f deg") || changed;
                        if (changed) recordStyle(std::move(before), "style.rightPortrait");
                        seal();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Characters) {
                    auto* speaker = DIALOGUE::FindDialogueSpeaker(
                        style, controller.selectedSpeakerId_);
                    if (speaker == nullptr) {
                        ImGui::TextWrapped(TEXT::Localize("editor.ui.add.a.character.from.the.left.panel.names.and.portraits.register.88e7d9").c_str());
                    }
                    else {
                        const std::string originalId = speaker->id;
                        auto before = style;
                        bool changed = DrawLocalizedText(
                            TEXT::LocalizeWindow("editor.ui.display.name.c7874a", "Display name").c_str(), speaker->displayName,
                            controller.editingLocale_);
                        std::string portraitGuid = speaker->portraitAssetGuid;
                        AssetFieldOptions portraitOptions{};
                        portraitOptions.label = "Portrait texture";
                        portraitOptions.requiredType = AssetType::Texture;
                        portraitOptions.allowCopyGuid = false;
                        portraitOptions.allowLocate = false;
                        if (DrawAssetField(&database, portraitOptions, portraitGuid)) {
                            speaker->portraitAssetGuid = std::move(portraitGuid);
                            changed = true;
                        }
                        if (speaker->portraitAssetGuid.empty()) {
                            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.portrait.assigned.the.canvas.will.show.dialogue.without.a.cha.41b3b8").c_str());
                        }
                        else if (speaker->resolvedPortraitAssetPath.empty()) {
                            ImGui::TextColored(
                                kError,TEXT::Localize("editor.ui.portrait.unavailable.choose.an.imported.texture.asset.again.fc38e9").c_str());
                        }
                        else {
                            ImGui::TextColored(kReady,TEXT::Localize("editor.ui.live.portrait.preview.ready.61a06d").c_str());
                            ImGui::TextDisabled(TEXT::Localize("editor.ui.the.center.canvas.updates.while.you.adjust.this.character.aec428").c_str());
                        }
                        const char* sides[]{ TEXT::LocalizeCStr("ui.quality.exposureAutomatic"), TEXT::LocalizeCStr("ui.input.directionLeft"), TEXT::LocalizeCStr("ui.input.directionRight") };
                        int side = static_cast<int>(speaker->preferredSlot);
                        changed = ImGui::Combo(TEXT::LocalizeWindow("editor.ui.preferred.side.7bd0e7", "Preferred side").c_str(), &side, sides, 3) || changed;
                        speaker->preferredSlot =
                            static_cast<DIALOGUE::DialoguePortraitSlot>(side);
                        ImGui::SeparatorText(TEXT::Localize("editor.ui.portrait.alignment.84f55e").c_str());
                        changed = ImGui::DragFloat2(TEXT::LocalizeWindow("editor.ui.offset.ce6677", "Offset").c_str(), &speaker->offsetX,
                            1.0f, -2048.0f, 2048.0f, "%.0f") || changed;
                        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.input.scale", "Scale").c_str(), &speaker->scale,
                            0.1f, 3.0f, "%.2fx") || changed;
                        changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.inspector.rotation", "Rotation").c_str(), &speaker->rotationDegrees,
                            -45.0f, 45.0f, "%.1f deg") || changed;
                        changed = ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.mirror.horizontally.3b5ec9", "Mirror horizontally").c_str(), &speaker->mirror) || changed;
                        if (changed) {
                            recordStyle(std::move(before), "style.character");
                            DIALOGUE::ResolveDialogueProjectStyleAssets(style, database);
                        }
                        seal();
                        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.advanced.identity.54f0f7", "Advanced identity").c_str())) {
                            before = style;
                            std::string id = speaker->id;
                            if (InputString(TEXT::LocalizeWindow("editor.ui.stable.character.id.c80fb2", "Stable character id").c_str(), id, false, 128u) &&
                                !id.empty() && (id == originalId ||
                                 DIALOGUE::FindDialogueSpeaker(style, id) == nullptr)) {
                                speaker->id = id;
                                controller.selectedSpeakerId_ = id;
                                auto dialogueBefore = definition;
                                bool dialogueChanged = false;
                                for (auto& node : definition.nodes) {
                                    if (node.speakerId == originalId) {
                                        node.speakerId = id;
                                        dialogueChanged = true;
                                    }
                                }
                                recordStyle(std::move(before),
                                    "style.characterId", false);
                                if (dialogueChanged) {
                                    recordDialogue(std::move(dialogueBefore),
                                        "dialogue.characterId", false);
                                }
                            }
                            seal();
                        }
                        ImGui::Spacing();
                        ImGui::PushStyleColor(
                            ImGuiCol_Button, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
                        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.remove.character.076063", "Remove Character").c_str(), ImVec2(-1.0f, 0.0f))) {
                            (void)controller.DeleteSelectedSpeaker(
                                controller.statusMessage_);
                        }
                        ImGui::PopStyleColor();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Motion) {
                    auto before = style;
                    bool changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.panel.entrance.9ed47b", "Panel entrance").c_str(), &style.theme.panelRevealSeconds,
                        0.05f, 1.5f, "%.2f s");
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.text.speed.3768e9", "Text speed").c_str(), &style.theme.charactersPerSecond,
                        5.0f, 120.0f, "%.0f chars/s") || changed;
                    ImGui::SeparatorText(TEXT::Localize("editor.ui.continue.prompt.fc9ef9").c_str());
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.cycle.9ebf34", "Prompt cycle").c_str(),
                        &style.theme.advancePromptPulseSeconds,
                        0.20f,
                        3.0f,
                        "%.2f s") || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.bob.1ebb99", "Prompt bob").c_str(),
                        &style.theme.advancePromptBobDistance,
                        0.0f,
                        32.0f,
                        "%.1f px") || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.prompt.pulse.scale.b621a2", "Prompt pulse scale").c_str(),
                        &style.theme.advancePromptScaleAmount,
                        0.0f,
                        0.20f,
                        "%.3f") || changed;
                    ImGui::SeparatorText(TEXT::Localize("editor.ui.portrait.focus.2bb526").c_str());
                    changed = DrawColor(
                        TEXT::LocalizeWindow("editor.ui.speaking.portrait.a7665b", "Speaking portrait").c_str(),
                        style.portraitTransition.speakingTint) || changed;
                    changed = DrawColor(
                        TEXT::LocalizeWindow("editor.ui.listening.portrait.a384af", "Listening portrait").c_str(),
                        style.portraitTransition.listeningTint) || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.focus.blend.bfb55a", "Focus blend").c_str(), &style.portraitTransition.focusSeconds,
                        0.05f, 0.8f, "%.2f s") || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.portrait.entrance.17093f", "Portrait entrance").c_str(), &style.portraitTransition.enterSeconds,
                        0.05f, 1.2f, "%.2f s") || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.entry.slide.dc2e87", "Entry slide").c_str(), &style.portraitTransition.slideDistance,
                        0.0f, 240.0f, "%.0f px") || changed;
                    changed = DrawColor(
                        TEXT::LocalizeWindow("editor.ui.edge.light.fa66de", "Edge light").c_str(),
                        style.portraitTransition.edgeLightColor) || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.edge.light.strength.4d86b9", "Edge light strength").c_str(),
                        &style.portraitTransition.edgeLightIntensity,
                        0.0f, 2.0f, "%.2f") || changed;
                    ImGui::SeparatorText(TEXT::Localize("editor.ui.background.game.view.e8990a").c_str());
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.darken.d7b6ff", "Darken").c_str(), &style.theme.backdropDim,
                        0.0f, 0.8f, "%.2f") || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("ui.vfx.text.Blur", "Blur").c_str(), &style.theme.backdropBlur,
                        0.0f, 1.0f, "%.2f") || changed;
                    changed = ImGui::SliderFloat(TEXT::LocalizeWindow("editor.ui.desaturate.f6d138", "Desaturate").c_str(), &style.theme.backdropDesaturation,
                        0.0f, 1.0f, "%.2f") || changed;
                    if (changed) recordStyle(std::move(before), "style.motion");
                    seal();
                }

                const auto issues = DIALOGUE::ValidateDialogueProjectStyle(style);
                if (!issues.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(kError,TEXT::Localize("editor.ui.style.needs.attention.4a82d8").c_str());
                    ImGui::TextWrapped("%s", issues.front().message.c_str());
                }
            }
            else {
                if (!controller.document_.IsOpen()) {
                    ImGui::TextColored(kAccent,TEXT::Localize("editor.ui.dialogue.event.8bbdfe").c_str());
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.no.dialogue.asset.is.open.select.an.existing.asset.or.create.a.n.e7db2a").c_str());
                }
                else {
                ImGui::TextColored(kAccent,TEXT::Localize("editor.ui.dialogue.event.8bbdfe").c_str());
                ImGui::TextWrapped(TEXT::Localize("editor.ui.edit.the.selected.story.step.here.the.center.preview.uses.the.sa.35ee76").c_str());
                ImGui::Separator();
                std::string name = definition.name;
                if (InputString(TEXT::LocalizeWindow("editor.ui.event.name.8e10d7", "Event name").c_str(), name)) {
                    auto before = definition;
                    definition.name = std::move(name);
                    recordDialogue(std::move(before), "dialogue.name");
                }
                seal();
                DIALOGUE::DialogueNode* node = controller.SelectedNode();
                if (node == nullptr) {
                    ImGui::TextColored(kModified,TEXT::Localize("editor.ui.no.story.step.selected.b5c5a3").c_str());
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.choose.a.step.in.dialogue.tools.or.add.a.new.line.choice.signal..84d62a").c_str());
                }
                else if (node->type == DIALOGUE::DialogueNodeType::Line) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.line.step.a1ebff").c_str());
                    ImGui::SeparatorText(TEXT::Localize("editor.ui.speaker.7c23b0").c_str());
                    const auto* currentSpeaker = DIALOGUE::FindDialogueSpeaker(
                        style, node->speakerId);
                    const std::string speakerPreview = currentSpeaker
                        ? std::string(ResolveName(
                            *currentSpeaker, controller.editingLocale_,
                            style.fallbackLocale))
                        : std::string("Narration / no portrait");
                    if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.character.ee9946", "Character").c_str(), speakerPreview.c_str())) {
                        if (ImGui::Selectable(TEXT::LocalizeWindow("editor.ui.narration.no.portrait.bec1a6", "Narration / no portrait").c_str(), node->speakerId.empty())) {
                            auto before = definition;
                            node->speakerId.clear();
                            recordDialogue(std::move(before), "line.speaker", false);
                        }
                        for (const auto& speaker : style.speakers) {
                            const bool selected = node->speakerId == speaker.id;
                            const std::string label = std::string(ResolveName(
                                speaker, controller.editingLocale_,
                                style.fallbackLocale));
                            ImGui::PushID(speaker.id.c_str());
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                auto before = definition;
                                node->speakerId = speaker.id;
                                recordDialogue(std::move(before),
                                    "line.speaker", false);
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    auto before = definition;
                    if (DrawLocalizedText(
                            TEXT::LocalizeWindow("ui.assetBrowser.typeDialogue", "Dialogue").c_str(), node->text,
                            controller.editingLocale_, true, 180.0f)) {
                        recordDialogue(std::move(before), "line.text");
                    }
                    seal();
                    ImGui::SeparatorText(TEXT::Localize("editor.ui.portrait.direction.960d48").c_str());
                    const char* actions[]{
                        TEXT::LocalizeCStr("ui.quality.exposureAutomatic"), TEXT::LocalizeCStr("editor.ui.show.replace.f060fd"), TEXT::LocalizeCStr("editor.ui.keep.current.786906"), TEXT::LocalizeCStr("ui.vfx.hierarchy.hide") };
                    int action = static_cast<int>(node->portraitAction);
                    before = definition;
                    bool changed = ImGui::Combo(TEXT::LocalizeWindow("editor.ui.portrait.behavior.1d8231", "Portrait behavior").c_str(), &action, actions, 4);
                    node->portraitAction =
                        static_cast<DIALOGUE::DialoguePortraitAction>(action);
                    const char* slots[]{ TEXT::LocalizeCStr("ui.quality.exposureAutomatic"), TEXT::LocalizeCStr("ui.input.directionLeft"), TEXT::LocalizeCStr("ui.input.directionRight") };
                    int slot = static_cast<int>(node->portraitSlot);
                    changed = ImGui::Combo(TEXT::LocalizeWindow("editor.ui.portrait.side.136200", "Portrait side").c_str(), &slot, slots, 3) || changed;
                    node->portraitSlot =
                        static_cast<DIALOGUE::DialoguePortraitSlot>(slot);
                    if (changed) recordDialogue(
                        std::move(before), "line.portrait", false);
                    std::string next = node->nextNodeId;
                    if (TargetCombo(
                            TEXT::LocalizeWindow("editor.ui.continue.to.02bdf3", "Continue to").c_str(), definition, style,
                            controller.editingLocale_, next)) {
                        before = definition;
                        node->nextNodeId = std::move(next);
                        recordDialogue(std::move(before), "line.next", false);
                    }
                    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.playback.options.75eab2", "Playback options").c_str())) {
                        before = definition;
                        changed = ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.auto.continue.after.89d8dd", "Auto continue after").c_str(), &node->autoAdvanceSeconds,
                            0.05f, 0.0f, 120.0f, "%.2f s");
                        changed = ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.player.can.reveal.text.instantly.7358e0", "Player can reveal text instantly").c_str(),
                            &node->skippable) || changed;
                        if (changed) recordDialogue(
                            std::move(before), "line.playback");
                        seal();
                    }
                }
                else if (node->type == DIALOGUE::DialogueNodeType::Choice) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.choice.step.1e8d95").c_str());
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.each.option.links.to.another.step.the.selected.option.uses.the.s.57f80a").c_str());
                    for (size_t index = 0u; index < node->choices.size(); ++index) {
                        ImGui::PushID(static_cast<int>(index));
                        const bool open = ImGui::CollapsingHeader(
                            ("Option " + std::to_string(index + 1u)).c_str(),
                            index == controller.selectedChoiceIndex_.value_or(0u)
                                ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                        if (open) {
                            controller.selectedChoiceIndex_ = index;
                            auto before = definition;
                            if (DrawLocalizedText(
                                    TEXT::LocalizeWindow("ui.inspector.worldText.text", "Text").c_str(), node->choices[index].text,
                                    controller.editingLocale_)) {
                                recordDialogue(std::move(before), "choice.text");
                            }
                            seal();
                            std::string target = node->choices[index].nextNodeId;
                            if (TargetCombo(
                                    TEXT::LocalizeWindow("editor.ui.go.to.8a68d9", "Go to").c_str(), definition, style,
                                    controller.editingLocale_, target)) {
                                before = definition;
                                node->choices[index].nextNodeId = std::move(target);
                                recordDialogue(std::move(before),
                                    "choice.target", false);
                            }
                            if (node->choices.size() > 1u &&
                                ImGui::Button(TEXT::LocalizeWindow("editor.ui.remove.option.17b06d", "Remove option").c_str())) {
                                before = definition;
                                node->choices.erase(node->choices.begin() + index);
                                controller.selectedChoiceIndex_.reset();
                                recordDialogue(std::move(before),
                                    "choice.remove", false);
                                ImGui::PopID();
                                break;
                            }
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.add.option.097dcf", "+ Add Option").c_str(), ImVec2(-1.0f, 0.0f))) {
                        auto before = definition;
                        DIALOGUE::DialogueChoice choice{};
                        choice.id = "choice" +
                            std::to_string(node->choices.size() + 1u);
                        choice.text.values[controller.editingLocale_] = "New option";
                        choice.nextNodeId = definition.entryNodeId;
                        node->choices.push_back(std::move(choice));
                        recordDialogue(std::move(before), "choice.add", false);
                    }
                }
                else if (node->type == DIALOGUE::DialogueNodeType::Event) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.signal.step.82a95a").c_str());
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.emit.a.named.event.then.continue.trigger.sequences.can.wait.for..49174a").c_str());
                    auto before = definition;
                    std::string eventId = node->eventId;
                    if (InputString(TEXT::LocalizeWindow("editor.ui.event.ad8919", "Event").c_str(), eventId)) {
                        node->eventId = std::move(eventId);
                        recordDialogue(std::move(before), "event.name");
                    }
                    seal();
                    std::string next = node->nextNodeId;
                    if (TargetCombo(
                            TEXT::LocalizeWindow("editor.ui.continue.to.02bdf3", "Continue to").c_str(), definition, style,
                            controller.editingLocale_, next)) {
                        before = definition;
                        node->nextNodeId = std::move(next);
                        recordDialogue(std::move(before), "event.next", false);
                    }
                }
                else {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.end.step.b2bffe").c_str());
                    ImGui::TextWrapped(TEXT::Localize("editor.ui.this.step.completes.the.dialogue.event.and.releases.the.trigger..c96704").c_str());
                }

                if (node != nullptr && ImGui::CollapsingHeader(TEXT::LocalizeWindow("ui.vfx.layer.advanced", "Advanced").c_str())) {
                    if (controller.nodeIdDraftOwner_ != node->id) {
                        controller.nodeIdDraftOwner_ = node->id;
                        controller.nodeIdDraft_ = node->id;
                    }
                    (void)InputString(
                        TEXT::LocalizeWindow("editor.ui.step.id.8d5440", "Step id").c_str(), controller.nodeIdDraft_, false, 128u);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        (void)controller.RenameSelectedNode(
                            controller.nodeIdDraft_, controller.statusMessage_);
                    }
                    int type = static_cast<int>(node->type);
                    const char* types[]{ TEXT::LocalizeCStr("editor.ui.line.ea9676"), TEXT::LocalizeCStr("editor.ui.choice.0682be"), TEXT::LocalizeCStr("editor.ui.event.ad8919"), TEXT::LocalizeCStr("ui.vfx.text.End") };
                    if (ImGui::Combo(TEXT::LocalizeWindow("editor.ui.step.type.5dd37b", "Step type").c_str(), &type, types, 4)) {
                        auto before = definition;
                        node->type = static_cast<DIALOGUE::DialogueNodeType>(type);
                        recordDialogue(std::move(before), "node.type", false);
                    }
                }

                const auto issues = DIALOGUE::ValidateDialogue(definition);
                if (!issues.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(kError,TEXT::Localize("editor.ui.dialogue.needs.attention.02e600").c_str());
                    ImGui::TextWrapped("%s", issues.front().message.c_str());
                }
                }
            }

            if (!controller.statusMessage_.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", controller.statusMessage_.c_str());
            }
        }
        ImGui::End();

        const auto unsavedTitle = TEXT::LocalizeWindow("editor.ui.unsaved.dialogue.documents.8c230c", "Dialogue/UnsavedDocuments");
        if (controller.ConsumePendingConfirmationOpenRequest()) {
            ImGui::OpenPopup(unsavedTitle.c_str());
        }
        if (ImGui::BeginPopupModal(
                unsavedTitle.c_str(),
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "%s",
                controller.GetPendingConfirmationText());
            ImGui::Spacing();
            if (controller.PendingActionSupportsSave()) {
                if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Save and Continue", "Save and Continue").c_str(), ImVec2(150.0f, 0.0f))) {
                    if (controller.ConfirmSaveThenContinue(
                            scene,
                            controller.statusMessage_)) {
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
            }
            if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Discard and Continue", "Discard and Continue").c_str(), ImVec2(170.0f, 0.0f))) {
                if (controller.ConfirmDiscardAndContinue(
                        scene,
                        controller.statusMessage_)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(90.0f, 0.0f))) {
                controller.CancelPending();
                controller.statusMessage_ = "Dialogue action cancelled";
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
#else
        (void)controller;
        (void)scene;
        (void)commandRouter;
#endif
    }

} // namespace HIKARI::EDITOR
