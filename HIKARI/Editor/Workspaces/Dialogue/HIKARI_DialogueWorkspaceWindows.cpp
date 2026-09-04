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
        constexpr const char* kUnsavedDialoguePopup =
            "Unsaved Dialogue Documents###Dialogue/UnsavedDocuments";

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
            if (node.type == DIALOGUE::DialogueNodeType::End) return "End";
            if (node.type == DIALOGUE::DialogueNodeType::Event) {
                return node.eventId.empty()
                    ? "Event"
                    : "Event  |  " + node.eventId;
            }
            if (node.type == DIALOGUE::DialogueNodeType::Choice) {
                return "Choice  |  " +
                    std::to_string(node.choices.size()) + " branches";
            }
            std::string prefix{ "Narration" };
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
                : std::string("Choose next step");
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
            if (!ImGui::BeginCombo("Font", preview)) return false;

            static std::array<char, 192u> search{};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##DialogueFontSearch",
                "Search TTF, OTF or TTC",
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
                ImGui::TextDisabled("No matching font in Assets");
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
            changed = DrawColor("Text", style.textColor) || changed;
            changed = DrawColor("Outline", style.outlineColor) || changed;
            changed = ImGui::SliderFloat(
                "Outline thickness", &style.outlineWidth,
                0.0f, 8.0f, "%.2f") || changed;
            if (ImGui::CollapsingHeader("Shadow and glow")) {
                changed = DrawColor("Shadow", style.shadowColor) || changed;
                changed = ImGui::DragFloat2(
                    "Shadow offset", &style.shadowOffsetX,
                    0.1f, -32.0f, 32.0f, "%.1f") || changed;
                changed = DrawColor("Glow", style.glowColor) || changed;
                changed = ImGui::SliderFloat(
                    "Glow size", &style.glowWidth, 0.0f, 12.0f) || changed;
                changed = ImGui::SliderFloat(
                    "Glow strength", &style.glowIntensity,
                    0.0f, 4.0f) || changed;
            }
            if (ImGui::CollapsingHeader("Spacing")) {
                changed = ImGui::DragFloat(
                    "Letter spacing", &style.letterSpacing,
                    0.1f, -8.0f, 32.0f) || changed;
                changed = ImGui::SliderFloat(
                    "Line height", &style.lineHeightMultiplier,
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
            case DialogueStyleSection::Canvas: return "Canvas & Artwork";
            case DialogueStyleSection::Typography: return "Typography";
            case DialogueStyleSection::Portraits: return "Portrait Layout";
            case DialogueStyleSection::Characters: return "Characters";
            case DialogueStyleSection::Motion: return "Motion & Backdrop";
            default: return "Canvas & Artwork";
            }
        }

        const char* StyleSectionNavigationLabel(
            DialogueStyleSection section) {
            switch (section) {
            case DialogueStyleSection::Canvas:
                return "1  Canvas & Artwork";
            case DialogueStyleSection::Typography:
                return "2  Typography";
            case DialogueStyleSection::Portraits:
                return "3  Portrait Layout";
            case DialogueStyleSection::Characters:
                return "4  Characters";
            case DialogueStyleSection::Motion:
                return "5  Motion & Backdrop";
            default:
                return "1  Canvas & Artwork";
            }
        }

        const char* StyleSectionDescription(DialogueStyleSection section) {
            switch (section) {
            case DialogueStyleSection::Canvas:
                return "Choose the dialogue, name and choice artwork, then position it on the canvas.";
            case DialogueStyleSection::Typography:
                return "Choose one project font and author readable name, body and choice treatments.";
            case DialogueStyleSection::Portraits:
                return "Place the two portrait slots used by every dialogue event.";
            case DialogueStyleSection::Characters:
                return "Register reusable names, portraits and per-character alignment.";
            case DialogueStyleSection::Motion:
                return "Tune reveal, portrait focus and the treatment of the game view behind dialogue.";
            default:
                return "Build the project-wide dialogue presentation.";
            }
        }

        const char* NodeTypeLabel(DIALOGUE::DialogueNodeType type) {
            switch (type) {
            case DIALOGUE::DialogueNodeType::Line: return "LINE";
            case DIALOGUE::DialogueNodeType::Choice: return "CHOICE";
            case DIALOGUE::DialogueNodeType::Event: return "SIGNAL";
            case DIALOGUE::DialogueNodeType::End: return "END";
            default: return "STEP";
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
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
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

        if (ImGui::Begin("Dialogue Tools###Dialogue/Navigator")) {
            bool stylePage =
                controller.activePage_ == DialogueWorkspacePage::Style;
            ImGui::TextColored(kAccent, "DIALOGUE AUTHORING");
            ImGui::TextDisabled("Project presentation and conversation events");
            ImGui::Spacing();
            if (ImGui::BeginTable(
                    "##DialoguePageTabs", 2,
                    ImGuiTableFlags_SizingStretchSame |
                        ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableNextColumn();
                if (DrawFullWidthSelectable("UI STYLE", stylePage, 36.0f)) {
                    controller.activePage_ = DialogueWorkspacePage::Style;
                    stylePage = true;
                }
                ImGui::TableNextColumn();
                if (DrawFullWidthSelectable(
                        "DIALOGUE EVENT", !stylePage, 36.0f)) {
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
                if (ImGui::Button(
                        "Save", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                    (void)controller.Save(scene, controller.statusMessage_);
                }
                const bool canUndo = stylePage
                    ? controller.styleDocument_.CanUndo()
                    : controller.document_.CanUndo();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!canUndo);
                if (ImGui::Button(
                        "Undo", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                    (void)controller.Undo(controller.statusMessage_);
                }
                ImGui::EndDisabled();
                const bool canRedo = stylePage
                    ? controller.styleDocument_.CanRedo()
                    : controller.document_.CanRedo();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!canRedo);
                if (ImGui::Button(
                        "Redo", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
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
                    stylePage ? "UI style has unsaved changes"
                              : "Dialogue event has unsaved changes");
            }
            ImGui::Separator();
            const char* locales[]{ "ja-JP", "en-US", "zh-CN" };
            ImGui::TextDisabled("EDITING LANGUAGE");
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
                ImGui::SeparatorText("STYLE WORKFLOW");
                ImGui::TextWrapped(
                    "Start with artwork and typography, register the cast, "
                    "then tune motion. The center canvas is always the visual source of truth.");
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
                    ImGui::SeparatorText("Project Characters");
                    if (style.speakers.empty()) {
                        ImGui::TextWrapped(
                            "No characters are registered yet. Add one here, "
                            "then assign its localized name and portrait in Properties.");
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
                    if (ImGui::Button("+ Add Character", ImVec2(-1.0f, 0.0f))) {
                        controller.AddSpeaker();
                    }
                }
            }
            else {
                ImGui::SeparatorText("EVENT WORKFLOW");
                ImGui::TextWrapped(
                    "One Dialogue asset is one complete conversation. Add steps below, "
                    "then connect Continue and Choice targets in Properties.");
                ImGui::Spacing();
                ImGui::SeparatorText("DIALOGUE ASSETS");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##DialogueAssetSearch",
                    "Search Dialogue assets",
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
                    ImGui::TextDisabled("No matching Dialogue assets");
                }
                if (ImGui::Button(
                        "New Dialogue Event", ImVec2(-1.0f, 0.0f))) {
                    (void)controller.RequestNewAsset(
                        controller.statusMessage_);
                }

                if (!controller.document_.IsOpen()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped(
                        "Open an existing Dialogue asset above, or create a new event.");
                }
                else {
                ImGui::TextDisabled("CURRENT EVENT");
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
                    if (ImGui::Button(
                            "Revert",
                            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                        (void)controller.RequestRevert(
                            database,
                            controller.statusMessage_);
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button(
                            "Close",
                            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                        (void)controller.RequestCloseAsset(
                            controller.statusMessage_);
                    }
                    ImGui::EndTable();
                }
                ImGui::SeparatorText("STORY STEPS");
                if (ImGui::Checkbox(
                        "Auto-link inserted step",
                        &controller.autoBindNewNodes_)) {
                    controller.statusMessage_ = controller.autoBindNewNodes_
                        ? "New story steps will be linked automatically"
                        : "Automatic story-step linking disabled";
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Insert after the selected step, link the current "
                        "continuation to it, and preserve the old target.");
                }
                if (ImGui::Button("+ Add Story Step", ImVec2(-1.0f, 0.0f))) {
                    ImGui::OpenPopup("AddDialogueStep");
                }
                if (ImGui::BeginPopup("AddDialogueStep")) {
                    if (ImGui::MenuItem("Line")) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::Line);
                    }
                    if (ImGui::MenuItem("Choice")) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::Choice);
                    }
                    if (ImGui::MenuItem("Event signal")) {
                        controller.AddNode(DIALOGUE::DialogueNodeType::Event);
                    }
                    if (ImGui::MenuItem("End")) {
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
                if (ImGui::Button(
                        "Bind Selected to Next Step",
                        ImVec2(-1.0f, 0.0f))) {
                    (void)controller.BindSelectedToNextNode(
                        controller.statusMessage_);
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(
                        "Link this Line or Event to the following list item. "
                        "For a Choice, select the option first.");
                }
                ImGui::BeginDisabled(definition.nodes.size() <= 1u);
                if (ImGui::Button("Remove Step", ImVec2(-1.0f, 0.0f))) {
                    (void)controller.DeleteSelectedNode(controller.statusMessage_);
                }
                ImGui::EndDisabled();
                }
            }
            }
            ImGui::EndChild();
            ImGui::Separator();
            if (ImGui::Button("Back to Scene", ImVec2(-1.0f, 0.0f))) {
                EditorWorkspaceOpenRequest request{};
                request.workspaceId = EditorWorkspaceId::Scene;
                (void)controller.RequestWorkspaceTransition(
                    std::move(request),
                    controller.statusMessage_);
            }
        }
        ImGui::End();

        if (ImGui::Begin("Canvas###Dialogue/Preview")) {
            ImGui::TextColored(
                kAccent,
                controller.activePage_ == DialogueWorkspacePage::Style
                    ? "UI STYLE CANVAS" : "DIALOGUE EVENT PREVIEW");
            ImGui::SameLine();
            ImGui::Checkbox("Animate", &controller.previewPlaying_);
            ImGui::SameLine();
            if (ImGui::Button("Replay")) controller.previewElapsed_ = 0.0f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat(
                "Text", &controller.previewRevealAmount_,
                0.0f, 1.0f, "%.2f");
            if (controller.activePage_ == DialogueWorkspacePage::Style) {
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "Select an outline to move it; drag its lower-right handle to resize");
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
                ImGui::TextDisabled(
                    "LIVE PREVIEW  %s",
                    controller.previewRenderer_->GetStatus().c_str());
            }
        }
        ImGui::End();

        if (ImGui::Begin("Properties###Dialogue/Inspector")) {
            if (controller.activePage_ == DialogueWorkspacePage::Style) {
                ImGui::TextColored(
                    kAccent, "%s", StyleSectionLabel(controller.styleSection_));
                ImGui::TextWrapped(
                    "%s", StyleSectionDescription(controller.styleSection_));
                ImGui::Separator();

                if (controller.styleSection_ == DialogueStyleSection::Canvas) {
                    auto before = style;
                    bool changed = DrawTextureAssetField(
                        "Dialogue panel", database,
                        style.theme.dialoguePanelAssetPath);
                    changed = DrawTextureAssetField(
                        "Name plate", database,
                        style.theme.speakerPlateAssetPath) || changed;
                    changed = DrawTextureAssetField(
                        "Choice panel", database,
                        style.theme.choiceAssetPath) || changed;
                    changed = DrawTextureAssetField(
                        "Continue - Gamepad", database,
                        style.theme.advanceGamepadAssetPath) || changed;
                    changed = DrawTextureAssetField(
                        "Continue - Mouse", database,
                        style.theme.advanceMouseAssetPath) || changed;
                    if (changed) recordStyle(std::move(before), "style.art", false);
                    ImGui::Spacing();
                    ImGui::TextWrapped(
                        "Place and resize artwork directly on the canvas. "
                        "The panel is treated as one complete sprite.");
                    if (ImGui::CollapsingHeader("Reference canvas")) {
                        before = style;
                        float size[2]{ style.theme.referenceWidth,
                            style.theme.referenceHeight };
                        if (ImGui::DragFloat2(
                                "Design resolution", size, 1.0f,
                                320.0f, 8192.0f, "%.0f")) {
                            style.theme.referenceWidth = size[0];
                            style.theme.referenceHeight = size[1];
                            recordStyle(std::move(before), "style.canvas");
                        }
                        seal();
                    }
                    if (ImGui::CollapsingHeader(
                            "Continue prompt",
                            ImGuiTreeNodeFlags_DefaultOpen)) {
                        before = style;
                        bool promptChanged = ImGui::DragFloat2(
                            "Prompt position",
                            &style.theme.advancePromptX,
                            1.0f,
                            -4096.0f,
                            4096.0f,
                            "%.0f");
                        promptChanged = ImGui::SliderFloat(
                            "Prompt scale",
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
                    ImGui::TextDisabled(
                        "Use a CJK-capable TTF, OTF or TTC for Japanese and Chinese.");
                    if (ImGui::CollapsingHeader(
                            "Character name", ImGuiTreeNodeFlags_DefaultOpen)) {
                        before = style;
                        bool changed = ImGui::SliderFloat(
                            "Name size", &style.theme.speakerPixelHeight,
                            10.0f, 128.0f, "%.0f px");
                        changed = DrawTextStyle(
                            "NameStyle", style.theme.speakerTextStyle) || changed;
                        if (changed) recordStyle(std::move(before), "style.nameText");
                        seal();
                    }
                    if (ImGui::CollapsingHeader(
                            "Dialogue text", ImGuiTreeNodeFlags_DefaultOpen)) {
                        before = style;
                        bool changed = ImGui::SliderFloat(
                            "Body size", &style.theme.bodyPixelHeight,
                            10.0f, 128.0f, "%.0f px");
                        changed = DrawTextStyle(
                            "BodyStyle", style.theme.bodyTextStyle) || changed;
                        if (changed) recordStyle(std::move(before), "style.bodyText");
                        seal();
                    }
                    if (ImGui::CollapsingHeader("Choice text")) {
                        before = style;
                        bool changed = ImGui::SliderFloat(
                            "Choice size", &style.theme.choicePixelHeight,
                            10.0f, 128.0f, "%.0f px");
                        changed = DrawTextStyle(
                            "ChoiceStyle", style.theme.choiceTextStyle) || changed;
                        if (changed) recordStyle(std::move(before), "style.choiceText");
                        seal();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Portraits) {
                    ImGui::TextWrapped(
                        "The runtime displays at most two portraits. Move the "
                        "left and right slots on the canvas, then apply per-character "
                        "alignment in Characters.");
                    if (ImGui::CollapsingHeader(
                            "Left slot", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto before = style;
                        bool changed = ImGui::DragFloat2(
                            "Position##Left", &style.leftPortrait.x,
                            1.0f, -4096.0f, 4096.0f, "%.0f");
                        changed = ImGui::DragFloat2(
                            "Size##Left", &style.leftPortrait.width,
                            1.0f, 1.0f, 4096.0f, "%.0f") || changed;
                        changed = ImGui::SliderFloat(
                            "Rotation##Left", &style.leftPortrait.rotationDegrees,
                            -45.0f, 45.0f, "%.1f deg") || changed;
                        if (changed) recordStyle(std::move(before), "style.leftPortrait");
                        seal();
                    }
                    if (ImGui::CollapsingHeader(
                            "Right slot", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto before = style;
                        bool changed = ImGui::DragFloat2(
                            "Position##Right", &style.rightPortrait.x,
                            1.0f, -4096.0f, 4096.0f, "%.0f");
                        changed = ImGui::DragFloat2(
                            "Size##Right", &style.rightPortrait.width,
                            1.0f, 1.0f, 4096.0f, "%.0f") || changed;
                        changed = ImGui::SliderFloat(
                            "Rotation##Right", &style.rightPortrait.rotationDegrees,
                            -45.0f, 45.0f, "%.1f deg") || changed;
                        if (changed) recordStyle(std::move(before), "style.rightPortrait");
                        seal();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Characters) {
                    auto* speaker = DIALOGUE::FindDialogueSpeaker(
                        style, controller.selectedSpeakerId_);
                    if (speaker == nullptr) {
                        ImGui::TextWrapped(
                            "Add a character from the left panel. Names and portraits "
                            "registered here become selectable in every dialogue event.");
                    }
                    else {
                        const std::string originalId = speaker->id;
                        auto before = style;
                        bool changed = DrawLocalizedText(
                            "Display name", speaker->displayName,
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
                            ImGui::TextDisabled(
                                "No portrait assigned. The canvas will show dialogue without a character image.");
                        }
                        else if (speaker->resolvedPortraitAssetPath.empty()) {
                            ImGui::TextColored(
                                kError,
                                "Portrait unavailable: choose an imported Texture asset again.");
                        }
                        else {
                            ImGui::TextColored(kReady, "Live portrait preview ready");
                            ImGui::TextDisabled(
                                "The center canvas updates while you adjust this character.");
                        }
                        const char* sides[]{ "Automatic", "Left", "Right" };
                        int side = static_cast<int>(speaker->preferredSlot);
                        changed = ImGui::Combo(
                            "Preferred side", &side, sides, 3) || changed;
                        speaker->preferredSlot =
                            static_cast<DIALOGUE::DialoguePortraitSlot>(side);
                        ImGui::SeparatorText("Portrait alignment");
                        changed = ImGui::DragFloat2(
                            "Offset", &speaker->offsetX,
                            1.0f, -2048.0f, 2048.0f, "%.0f") || changed;
                        changed = ImGui::SliderFloat(
                            "Scale", &speaker->scale,
                            0.1f, 3.0f, "%.2fx") || changed;
                        changed = ImGui::SliderFloat(
                            "Rotation", &speaker->rotationDegrees,
                            -45.0f, 45.0f, "%.1f deg") || changed;
                        changed = ImGui::Checkbox(
                            "Mirror horizontally", &speaker->mirror) || changed;
                        if (changed) {
                            recordStyle(std::move(before), "style.character");
                            DIALOGUE::ResolveDialogueProjectStyleAssets(style, database);
                        }
                        seal();
                        if (ImGui::CollapsingHeader("Advanced identity")) {
                            before = style;
                            std::string id = speaker->id;
                            if (InputString("Stable character id", id, false, 128u) &&
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
                        if (ImGui::Button(
                                "Remove Character", ImVec2(-1.0f, 0.0f))) {
                            (void)controller.DeleteSelectedSpeaker(
                                controller.statusMessage_);
                        }
                        ImGui::PopStyleColor();
                    }
                }
                else if (controller.styleSection_ == DialogueStyleSection::Motion) {
                    auto before = style;
                    bool changed = ImGui::SliderFloat(
                        "Panel entrance", &style.theme.panelRevealSeconds,
                        0.05f, 1.5f, "%.2f s");
                    changed = ImGui::SliderFloat(
                        "Text speed", &style.theme.charactersPerSecond,
                        5.0f, 120.0f, "%.0f chars/s") || changed;
                    ImGui::SeparatorText("Continue prompt");
                    changed = ImGui::SliderFloat(
                        "Prompt cycle",
                        &style.theme.advancePromptPulseSeconds,
                        0.20f,
                        3.0f,
                        "%.2f s") || changed;
                    changed = ImGui::SliderFloat(
                        "Prompt bob",
                        &style.theme.advancePromptBobDistance,
                        0.0f,
                        32.0f,
                        "%.1f px") || changed;
                    changed = ImGui::SliderFloat(
                        "Prompt pulse scale",
                        &style.theme.advancePromptScaleAmount,
                        0.0f,
                        0.20f,
                        "%.3f") || changed;
                    ImGui::SeparatorText("Portrait focus");
                    changed = DrawColor(
                        "Speaking portrait",
                        style.portraitTransition.speakingTint) || changed;
                    changed = DrawColor(
                        "Listening portrait",
                        style.portraitTransition.listeningTint) || changed;
                    changed = ImGui::SliderFloat(
                        "Focus blend", &style.portraitTransition.focusSeconds,
                        0.05f, 0.8f, "%.2f s") || changed;
                    changed = ImGui::SliderFloat(
                        "Portrait entrance", &style.portraitTransition.enterSeconds,
                        0.05f, 1.2f, "%.2f s") || changed;
                    changed = ImGui::SliderFloat(
                        "Entry slide", &style.portraitTransition.slideDistance,
                        0.0f, 240.0f, "%.0f px") || changed;
                    changed = DrawColor(
                        "Edge light",
                        style.portraitTransition.edgeLightColor) || changed;
                    changed = ImGui::SliderFloat(
                        "Edge light strength",
                        &style.portraitTransition.edgeLightIntensity,
                        0.0f, 2.0f, "%.2f") || changed;
                    ImGui::SeparatorText("Background game view");
                    changed = ImGui::SliderFloat(
                        "Darken", &style.theme.backdropDim,
                        0.0f, 0.8f, "%.2f") || changed;
                    changed = ImGui::SliderFloat(
                        "Blur", &style.theme.backdropBlur,
                        0.0f, 1.0f, "%.2f") || changed;
                    changed = ImGui::SliderFloat(
                        "Desaturate", &style.theme.backdropDesaturation,
                        0.0f, 1.0f, "%.2f") || changed;
                    if (changed) recordStyle(std::move(before), "style.motion");
                    seal();
                }

                const auto issues = DIALOGUE::ValidateDialogueProjectStyle(style);
                if (!issues.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(kError, "Style needs attention");
                    ImGui::TextWrapped("%s", issues.front().message.c_str());
                }
            }
            else {
                if (!controller.document_.IsOpen()) {
                    ImGui::TextColored(kAccent, "DIALOGUE EVENT");
                    ImGui::TextWrapped(
                        "No Dialogue asset is open. Select an existing asset "
                        "or create a new event in Dialogue Tools.");
                }
                else {
                ImGui::TextColored(kAccent, "DIALOGUE EVENT");
                ImGui::TextWrapped(
                    "Edit the selected story step here. The center preview uses "
                    "the same project style and character registry as runtime.");
                ImGui::Separator();
                std::string name = definition.name;
                if (InputString("Event name", name)) {
                    auto before = definition;
                    definition.name = std::move(name);
                    recordDialogue(std::move(before), "dialogue.name");
                }
                seal();
                DIALOGUE::DialogueNode* node = controller.SelectedNode();
                if (node == nullptr) {
                    ImGui::TextColored(kModified, "NO STORY STEP SELECTED");
                    ImGui::TextWrapped(
                        "Choose a step in Dialogue Tools, or add a new Line, "
                        "Choice, Signal or End step there.");
                }
                else if (node->type == DIALOGUE::DialogueNodeType::Line) {
                    ImGui::TextDisabled("LINE STEP");
                    ImGui::SeparatorText("Speaker");
                    const auto* currentSpeaker = DIALOGUE::FindDialogueSpeaker(
                        style, node->speakerId);
                    const std::string speakerPreview = currentSpeaker
                        ? std::string(ResolveName(
                            *currentSpeaker, controller.editingLocale_,
                            style.fallbackLocale))
                        : std::string("Narration / no portrait");
                    if (ImGui::BeginCombo("Character", speakerPreview.c_str())) {
                        if (ImGui::Selectable(
                                "Narration / no portrait", node->speakerId.empty())) {
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
                            "Dialogue", node->text,
                            controller.editingLocale_, true, 180.0f)) {
                        recordDialogue(std::move(before), "line.text");
                    }
                    seal();
                    ImGui::SeparatorText("Portrait direction");
                    const char* actions[]{
                        "Automatic", "Show / replace", "Keep current", "Hide" };
                    int action = static_cast<int>(node->portraitAction);
                    before = definition;
                    bool changed = ImGui::Combo(
                        "Portrait behavior", &action, actions, 4);
                    node->portraitAction =
                        static_cast<DIALOGUE::DialoguePortraitAction>(action);
                    const char* slots[]{ "Automatic", "Left", "Right" };
                    int slot = static_cast<int>(node->portraitSlot);
                    changed = ImGui::Combo(
                        "Portrait side", &slot, slots, 3) || changed;
                    node->portraitSlot =
                        static_cast<DIALOGUE::DialoguePortraitSlot>(slot);
                    if (changed) recordDialogue(
                        std::move(before), "line.portrait", false);
                    std::string next = node->nextNodeId;
                    if (TargetCombo(
                            "Continue to", definition, style,
                            controller.editingLocale_, next)) {
                        before = definition;
                        node->nextNodeId = std::move(next);
                        recordDialogue(std::move(before), "line.next", false);
                    }
                    if (ImGui::CollapsingHeader("Playback options")) {
                        before = definition;
                        changed = ImGui::DragFloat(
                            "Auto continue after", &node->autoAdvanceSeconds,
                            0.05f, 0.0f, 120.0f, "%.2f s");
                        changed = ImGui::Checkbox(
                            "Player can reveal text instantly",
                            &node->skippable) || changed;
                        if (changed) recordDialogue(
                            std::move(before), "line.playback");
                        seal();
                    }
                }
                else if (node->type == DIALOGUE::DialogueNodeType::Choice) {
                    ImGui::TextDisabled("CHOICE STEP");
                    ImGui::TextWrapped(
                        "Each option links to another step. The selected option "
                        "uses the shader treatment from UI Style.");
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
                                    "Text", node->choices[index].text,
                                    controller.editingLocale_)) {
                                recordDialogue(std::move(before), "choice.text");
                            }
                            seal();
                            std::string target = node->choices[index].nextNodeId;
                            if (TargetCombo(
                                    "Go to", definition, style,
                                    controller.editingLocale_, target)) {
                                before = definition;
                                node->choices[index].nextNodeId = std::move(target);
                                recordDialogue(std::move(before),
                                    "choice.target", false);
                            }
                            if (node->choices.size() > 1u &&
                                ImGui::Button("Remove option")) {
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
                    if (ImGui::Button("+ Add Option", ImVec2(-1.0f, 0.0f))) {
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
                    ImGui::TextDisabled("SIGNAL STEP");
                    ImGui::TextWrapped(
                        "Emit a named event, then continue. Trigger sequences can "
                        "wait for the dialogue to finish before running their next action.");
                    auto before = definition;
                    std::string eventId = node->eventId;
                    if (InputString("Event", eventId)) {
                        node->eventId = std::move(eventId);
                        recordDialogue(std::move(before), "event.name");
                    }
                    seal();
                    std::string next = node->nextNodeId;
                    if (TargetCombo(
                            "Continue to", definition, style,
                            controller.editingLocale_, next)) {
                        before = definition;
                        node->nextNodeId = std::move(next);
                        recordDialogue(std::move(before), "event.next", false);
                    }
                }
                else {
                    ImGui::TextDisabled("END STEP");
                    ImGui::TextWrapped(
                        "This step completes the dialogue event and releases the "
                        "trigger sequence to its next action.");
                }

                if (node != nullptr && ImGui::CollapsingHeader("Advanced")) {
                    if (controller.nodeIdDraftOwner_ != node->id) {
                        controller.nodeIdDraftOwner_ = node->id;
                        controller.nodeIdDraft_ = node->id;
                    }
                    (void)InputString(
                        "Step id", controller.nodeIdDraft_, false, 128u);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        (void)controller.RenameSelectedNode(
                            controller.nodeIdDraft_, controller.statusMessage_);
                    }
                    int type = static_cast<int>(node->type);
                    const char* types[]{ "Line", "Choice", "Event", "End" };
                    if (ImGui::Combo("Step type", &type, types, 4)) {
                        auto before = definition;
                        node->type = static_cast<DIALOGUE::DialogueNodeType>(type);
                        recordDialogue(std::move(before), "node.type", false);
                    }
                }

                const auto issues = DIALOGUE::ValidateDialogue(definition);
                if (!issues.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(kError, "Dialogue needs attention");
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

        if (controller.ConsumePendingConfirmationOpenRequest()) {
            ImGui::OpenPopup(kUnsavedDialoguePopup);
        }
        if (ImGui::BeginPopupModal(
                kUnsavedDialoguePopup,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "%s",
                controller.GetPendingConfirmationText());
            ImGui::Spacing();
            if (controller.PendingActionSupportsSave()) {
                if (ImGui::Button(
                        "Save and Continue", ImVec2(150.0f, 0.0f))) {
                    if (controller.ConfirmSaveThenContinue(
                            scene,
                            controller.statusMessage_)) {
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
            }
            if (ImGui::Button(
                    "Discard and Continue", ImVec2(170.0f, 0.0f))) {
                if (controller.ConfirmDiscardAndContinue(
                        scene,
                        controller.statusMessage_)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
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
