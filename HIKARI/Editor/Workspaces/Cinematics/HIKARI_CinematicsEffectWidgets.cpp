#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include <algorithm>
#include <array>
#include <cmath>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawCurve(
    SEQUENCER::PresentationParameter &parameter, float seconds) {
  bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
  float value = parameter.Evaluate(seconds);
  const auto writeKey = [&](float next) {
    auto it = std::find_if(
        parameter.keys.begin(), parameter.keys.end(),
        [&](const auto &k) { return std::abs(k.time - seconds) < 0.0001f; });
    if (it == parameter.keys.end())
      parameter.keys.push_back({seconds, next, 0, 0});
    else
      it->value = next;
    std::stable_sort(
        parameter.keys.begin(), parameter.keys.end(),
        [](const auto &a, const auto &b) { return a.time < b.time; });
    changed = true;
  };
  if (ImGui::DragFloat("数值", &value, 0.01f)) {
    if (autoKey_ || !parameter.keys.empty())
      writeKey(value);
    else {
      parameter.value = value;
      changed = true;
    }
  }
  ImGui::SameLine();
  if (IconButton(EditorGlyph::Keyframe, "addParameterKey",
                 EditorButtonTone::Quiet, {22, 22}, "在当前时刻记录关键帧")) {
    writeKey(value);
  }
  if (!parameter.keys.empty() && ImGui::TreeNode("曲线")) {
    float end = (std::max)(seconds, 1.0f), low = parameter.value,
          high = parameter.value;
    for (const auto &k : parameter.keys) {
      end = (std::max)(end, k.time);
      low = (std::min)(low, k.value);
      high = (std::max)(high, k.value);
    }
    const float margin = (std::max)(0.1f, (high - low) * 0.15f);
    low -= margin;
    high += margin;
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{(std::max)(100.0f, ImGui::GetContentRegionAvail().x), 90};
    ImGui::InvisibleButton("curveCanvas", size);
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                        IM_COL32(16, 22, 30, 255), 4);
    const auto point = [&](float t, float v) {
      return ImVec2{origin.x + t / end * size.x,
                    origin.y + size.y - (v - low) / (high - low) * size.y};
    };
    for (int i = 1; i <= 100; ++i) {
      float a = end * (i - 1) / 100, b = end * i / 100;
      draw->AddLine(point(a, parameter.Evaluate(a)),
                    point(b, parameter.Evaluate(b)),
                    IM_COL32(98, 206, 222, 255), 2);
    }
    for (const auto &k : parameter.keys)
      draw->AddCircleFilled(point(k.time, k.value), 3,
                            IM_COL32(241, 202, 112, 255));
    int remove = -1;
    for (size_t i = 0; i < parameter.keys.size(); ++i) {
      auto &k = parameter.keys[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(65);
      changed |= ImGui::DragFloat("秒", &k.time, 0.01f, 0, 10000);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(75);
      changed |= ImGui::DragFloat("值", &k.value, 0.01f);
      ImGui::SameLine();
      if (IconButton(EditorGlyph::Delete, "removeKey", EditorButtonTone::Quiet,
                     {20, 20}, "删除关键帧"))
        remove = static_cast<int>(i);
      ImGui::SetNextItemWidth(70);
      changed |= ImGui::DragFloat("入切线", &k.inSlope, 0.05f);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70);
      changed |= ImGui::DragFloat("出切线", &k.outSlope, 0.05f);
      ImGui::PopID();
    }
    if (remove >= 0) {
      parameter.keys.erase(parameter.keys.begin() + remove);
      changed = true;
    }
    std::stable_sort(
        parameter.keys.begin(), parameter.keys.end(),
        [](const auto &a, const auto &b) { return a.time < b.time; });
    ImGui::TreePop();
  }
#endif
  return changed;
}

bool CinematicsWorkspaceController::DrawEffects(
    std::vector<SEQUENCER::SequenceScreenEffect> &effects, float seconds) {
  bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
  using namespace SEQUENCER;
  constexpr const char *names[]{
      "Bloom 强度", "Bloom 阈值", "曝光", "饱和度",          "对比度", "色温",
      "暗角",       "黑白",       "染色", "淡入淡出 / 闪光", "像素化", "色散"};
  if (IconTextButton(EditorGlyph::Add, "添加效果", "addEffect"))
    ImGui::OpenPopup("effectMenu");
  if (ImGui::BeginPopup("effectMenu")) {
    static std::array<char, 96> filter{};
    SearchField("effectFilter", "搜索效果", filter.data(), filter.size());
    for (int i = 0; i < 12; ++i) {
      if (i == 0)
        ImGui::SeparatorText("基础画面 · 覆盖场景参数");
      if (i == 6)
        ImGui::SeparatorText("风格与转场 · 顺序处理");
      if (filter[0] &&
          std::string(names[i]).find(filter.data()) == std::string::npos)
        continue;
      if (ImGui::Selectable(names[i])) {
        SequenceScreenEffect e;
        for (const auto &old : effects)
          e.id = (std::max)(e.id, old.id + 1);
        e.kind = static_cast<ScreenEffectKind>(i);
        e.name = names[i];
        e.amount.value = i == 5                       ? 6500.0f
                         : i == 10                    ? 8.0f
                         : i == 2 || i == 3 || i == 4 ? 1.0f
                                                      : 0.5f;
        if (i == 9)
          e.color = {0, 0, 0};
        effects.push_back(e);
        changed = true;
      }
    }
    ImGui::EndPopup();
  }
  int remove = -1, move = -1;
  for (size_t i = 0; i < effects.size(); ++i) {
    auto &e = effects[i];
    ImGui::PushID(static_cast<int>(e.id));
    changed |= ImGui::Checkbox("##enabled", &e.enabled);
    ImGui::SameLine();
    const bool open =
        ImGui::TreeNodeEx("effect", ImGuiTreeNodeFlags_DefaultOpen, "%s",
                          names[static_cast<int>(e.kind)]);
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Delete, "deleteEffect", EditorButtonTone::Quiet,
                   {20, 20}, "删除效果"))
      remove = static_cast<int>(i);
    if (i) {
      ImGui::SameLine();
      if (ImGui::SmallButton("上移"))
        move = static_cast<int>(i);
    }
    if (open) {
      ImGui::TextDisabled(static_cast<int>(e.kind) < 6
                              ? "覆盖同一渲染阶段；关闭后继承场景"
                              : "作用于演出画面；保留 HDR");
      changed |= ImGui::DragFloat("开始", &e.start, 0.02f, 0, 10000);
      changed |= ImGui::DragFloat("时长", &e.duration, 0.02f, 0.01f, 10000);
      changed |= ImGui::DragFloat("淡入", &e.fadeIn, 0.02f, 0, e.duration);
      changed |= ImGui::DragFloat("淡出", &e.fadeOut, 0.02f, 0, e.duration);
      const float local = (std::max)(0.0f, seconds - e.start);
      ImGui::PushID("amount");
      ImGui::TextUnformatted("强度 / 目标值");
      changed |= DrawCurve(e.amount, local);
      ImGui::PopID();
      ImGui::PushID("weight");
      ImGui::TextUnformatted("混合权重");
      changed |= DrawCurve(e.weight, local);
      ImGui::PopID();
      if (e.kind == ScreenEffectKind::Tint || e.kind == ScreenEffectKind::Fade)
        changed |= ImGui::ColorEdit3("颜色", &e.color.x,
                                     ImGuiColorEditFlags_Float |
                                         ImGuiColorEditFlags_HDR);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    effects.erase(effects.begin() + remove);
    changed = true;
  } else if (move > 0) {
    std::swap(effects[move], effects[move - 1]);
    changed = true;
  }
#endif
  return changed;
}
} // namespace HIKARI::EDITOR
