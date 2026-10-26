#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
// UTF-8 の字の途中で切らずに maximumBytes まで縮める。
void TruncateUtf8(std::string& text, size_t maximumBytes) {
    if (text.size() <= maximumBytes)
        return;
    size_t length = maximumBytes;
    while (length > 0u && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u)
        --length;
    text.resize(length);
}

// 選択肢の表（値と表示名の键）から一つ選ぶ。
template <class E>
bool ChoiceCombo(const std::string& label, E& value,
                 std::initializer_list<std::pair<std::type_identity_t<E>, const char*>> items) {
    bool edited = false;
    const char* current = items.begin()->second;
    for (const auto& [option, key] : items)
        if (option == value)
            current = key;
    if (ImGui::BeginCombo(label.c_str(), TEXT::Localize(current).c_str())) {
        for (const auto& [option, key] : items)
            if (ImGui::Selectable(TEXT::Localize(key).c_str(), option == value)) {
                value = option;
                edited = true;
            }
        ImGui::EndCombo();
    }
    return edited;
}
#endif
} // namespace

size_t CinematicsWorkspaceController::AssignTextRows(const CinematicSequence& sequence) {
    const auto& clips = sequence.textTrack.clips;
#if defined(HIKARI_WITH_EDITOR)
    // 引いている間は行を変えない（重なった途端に片段が別の行へ跳ばないように）。
    const bool dragging = draggingKind_ == SelectionKind::Text && ImGui::IsMouseDown(ImGuiMouseButton_Left);
#else
    const bool dragging = false;
#endif
    if (!dragging) {
        textRows_.clear();
        // 始まりの順に、前の片段が終わった行へ詰める（同じ始まりは轨の並び順）。
        std::vector<const SEQUENCER::SequenceTextClip*> ordered;
        for (const auto& clip : clips)
            ordered.push_back(&clip);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto* a, const auto* b) { return a->startSeconds < b->startSeconds; });
        std::vector<float> rowEnds;
        for (const auto* clip : ordered) {
            size_t row = 0u;
            while (row < rowEnds.size() && rowEnds[row] > clip->startSeconds + 1.0e-4f)
                ++row;
            if (row == rowEnds.size())
                rowEnds.push_back(0.0f);
            rowEnds[row] = clip->startSeconds + clip->durationSeconds;
            textRows_[clip->id] = row;
        }
    }
    size_t rows = 0u;
    for (const auto& clip : clips)
        rows = (std::max)(rows, textRows_.try_emplace(clip.id, 0u).first->second + 1u);
    return rows;
}

bool CinematicsWorkspaceController::AddTextClip(CinematicSequence& sequence, float seconds) {
    auto& clips = sequence.textTrack.clips;
    if (clips.size() >= SEQUENCER::kMaximumSequenceTexts)
        return false;
    // 見た目（样式・置き場所・現れ方・長さ）は選んでいた文字段か、轨の最後の片段から引き継ぐ。
    const SEQUENCER::SequenceTextClip* source = clips.empty() ? nullptr : &clips.back();
    if (selectionKind_ == SelectionKind::Text)
        for (const auto& clip : clips)
            if (clip.id == selectedId_)
                source = &clip;
    SEQUENCER::SequenceTextClip clip = source ? *source : SEQUENCER::SequenceTextClip{};
    clip.id = 1u;
    for (const auto& other : clips)
        clip.id = (std::max)(clip.id, other.id + 1u);
    clip.text = TEXT::Localize("cine.textTrack");
    clip.startSeconds = (std::max)(0.0f, seconds);
    clips.push_back(clip);
    selectionKind_ = SelectionKind::Text;
    selectedId_ = clip.id;
    return true;
}

bool CinematicsWorkspaceController::DrawTextInspector(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    const auto found = std::find_if(sequence.textTrack.clips.begin(), sequence.textTrack.clips.end(),
                                    [this](const SequenceTextClip& clip) { return clip.id == selectedId_; });
    if (found == sequence.textTrack.clips.end())
        return false;
    SequenceTextClip& clip = *found;
    const auto L = [](const char* key) { return TEXT::Localize(key).c_str(); };
    const bool narrow = ImGui::GetContentRegionAvail().x < 300.0f * ImGui::GetFontSize() / 16.0f;
    const auto fieldLabel = [narrow](const char* label) {
        if (!narrow)
            return std::string(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1);
        return "##" + std::string(label);
    };
    PanelTitle(L("cine.textClip"), L("cine.textClipHint"));
    // 文字：改行がそのまま行（縦書きは列）になる。
    std::vector<char> buffer(kMaximumSequenceTextBytes + 1u, '\0');
    std::copy_n(clip.text.begin(), (std::min)(clip.text.size(), kMaximumSequenceTextBytes), buffer.begin());
    ImGui::TextUnformatted(L("cine.textContent"));
    if (ImGui::InputTextMultiline("##textContent", buffer.data(), buffer.size(),
                                  {-1.0f, ImGui::GetTextLineHeight() * 6.0f})) {
        clip.text = buffer.data();
        changed = true;
    }
    std::array<char, kMaximumSequenceTextTagBytes + 1u> tag{};
    std::copy_n(clip.tag.begin(), (std::min)(clip.tag.size(), kMaximumSequenceTextTagBytes), tag.begin());
    if (ImGui::InputText(fieldLabel(L("cine.textTag")).c_str(), tag.data(), tag.size())) {
        clip.tag = tag.data();
        TruncateUtf8(clip.tag, kMaximumSequenceTextTagBytes);
        changed = true;
    }
    ImGui::SetItemTooltip("%s", L("cine.textTagTip"));
    AssetFieldOptions options;
    options.label = L("cine.textStyle");
    options.requiredType = AssetType::TextStyle;
    changed |= DrawAssetField(&scene.GetAssetDatabase(), options, clip.style.value);
    if (!clip.style.IsValid())
        ImGui::TextColored({0.95f, 0.72f, 0.35f, 1.0f}, "%s", L("cine.textNoStyle"));
    changed |= ImGui::DragFloat(fieldLabel(TEXT::Localize("ui.vfx.text.Start").c_str()).c_str(), &clip.startSeconds,
                                0.02f, 0.0f, 10000.0f, "%.2f s");
    changed |= ImGui::DragFloat(fieldLabel(L("cine.duration")).c_str(), &clip.durationSeconds, 0.02f, 0.05f, 10000.0f,
                                "%.2f s");
    changed |= ChoiceCombo(
        fieldLabel(L("cine.textLayout")), clip.layout,
        {{SequenceTextLayout::Horizontal, "cine.textHorizontal"}, {SequenceTextLayout::Vertical, "cine.textVertical"}});
    changed |= ChoiceCombo(fieldLabel(L("ui.inspector.worldText.horizontalAlign")), clip.alignX,
                           {{SequenceTextAlign::Start, "uix.alignLeft"},
                            {SequenceTextAlign::Center, "uix.alignCenter"},
                            {SequenceTextAlign::End, "uix.alignRight"}});
    changed |= ChoiceCombo(fieldLabel(L("ui.inspector.worldText.verticalAlign")), clip.alignY,
                           {{SequenceTextAlign::Start, "uix.alignTop"},
                            {SequenceTextAlign::Center, "uix.alignMiddle"},
                            {SequenceTextAlign::End, "uix.alignBottom"}});
    float position[2]{clip.x, clip.y};
    if (ImGui::DragFloat2(fieldLabel(L("cine.textPosition")).c_str(), position, 0.002f, -1.0f, 2.0f, "%.3f")) {
        clip.x = position[0];
        clip.y = position[1];
        changed = true;
    }
    if (clip.layout == SequenceTextLayout::Horizontal)
        changed |= ImGui::DragFloat(fieldLabel(L("cine.textWidth")).c_str(), &clip.width, 0.005f, 0.01f, 4.0f, "%.3f");
    else
        changed |= ImGui::DragFloat(fieldLabel(L("cine.textColumnSpacing")).c_str(), &clip.columnSpacing, 0.01f, 0.5f,
                                    8.0f, "%.2f");
    changed |= ImGui::DragFloat(fieldLabel(L("cine.textScale")).c_str(), &clip.scale, 0.01f, 0.05f, 16.0f, "%.2f");
    changed |= ImGui::ColorEdit4(fieldLabel(L("cine.textTint")).c_str(), &clip.tint.r, ImGuiColorEditFlags_Float);
    changed |= ChoiceCombo(fieldLabel(L("cine.textReveal")), clip.reveal,
                           {{SequenceTextReveal::None, "cine.textRevealNone"},
                            {SequenceTextReveal::Typewriter, "cine.textRevealTypewriter"}});
    if (clip.reveal == SequenceTextReveal::Typewriter)
        changed |= ImGui::DragFloat(fieldLabel(L("cine.textRevealSeconds")).c_str(), &clip.revealSeconds, 0.02f, 0.0f,
                                    clip.durationSeconds, "%.2f s");
    changed |= ImGui::DragFloat(fieldLabel(L("cine.fadeIn")).c_str(), &clip.fadeInSeconds, 0.02f, 0.0f,
                                clip.durationSeconds, "%.2f s");
    changed |= ImGui::DragFloat(fieldLabel(L("cine.fadeOut")).c_str(), &clip.fadeOutSeconds, 0.02f, 0.0f,
                                clip.durationSeconds, "%.2f s");
    changed |= ImGui::DragInt(fieldLabel(L("cine.textOrder")).c_str(), &clip.order, 1.0f, -100000, 100000);
    // 保存の検証と同じ範囲に収める（手で打った値でも保存が失敗しないように）。
    clip.startSeconds = std::clamp(clip.startSeconds, 0.0f, kMaxSequenceDurationSeconds - 0.05f);
    clip.durationSeconds = std::clamp(clip.durationSeconds, 0.05f, kMaxSequenceDurationSeconds - clip.startSeconds);
    clip.x = std::clamp(clip.x, -1.0f, 2.0f);
    clip.y = std::clamp(clip.y, -1.0f, 2.0f);
    clip.width = std::clamp(clip.width, 0.01f, 4.0f);
    clip.columnSpacing = std::clamp(clip.columnSpacing, 0.5f, 8.0f);
    clip.scale = std::clamp(clip.scale, 0.05f, 16.0f);
    for (float* channel : {&clip.tint.r, &clip.tint.g, &clip.tint.b})
        *channel = std::clamp(*channel, 0.0f, 4.0f);
    clip.tint.a = std::clamp(clip.tint.a, 0.0f, 1.0f);
    clip.revealSeconds = (std::max)(0.0f, clip.revealSeconds);
    clip.fadeInSeconds = (std::max)(0.0f, clip.fadeInSeconds);
    clip.fadeOutSeconds = (std::max)(0.0f, clip.fadeOutSeconds);
    clip.order = std::clamp(clip.order, -100000, 100000);
    if (ImGui::Button(L("cine.deleteClip")))
        return DeleteSelectedClip(sequence);
#else
    (void)scene;
    (void)sequence;
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
