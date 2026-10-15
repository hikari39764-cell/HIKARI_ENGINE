#include "Editor/Workspaces/Vfx/HIKARI_VfxPathEditing.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
std::vector<VfxPathKnot> ReadVfxBezierPath(const VFX::VfxEmitterData& layer, float span) {
    const auto& value = layer.position.value;
    const auto& c = value.curve;
    if (value.mode != VFX::VfxAnimatedValueMode::Curve || c.x.keys.size() < 2 || c.x.keys.size() != c.y.keys.size() ||
        c.x.keys.size() != c.z.keys.size() || c.x.interpolation != VFX::VfxCurveInterpolation::Smooth ||
        c.y.interpolation != c.x.interpolation || c.z.interpolation != c.x.interpolation)
        return {};
    std::vector<VfxPathKnot> result;
    for (size_t i = 0; i < c.x.keys.size(); ++i) {
        const auto& x = c.x.keys[i];
        const auto& y = c.y.keys[i];
        const auto& z = c.z.keys[i];
        if (std::abs(x.time - y.time) > 0.00001f || std::abs(x.time - z.time) > 0.00001f)
            return {};
        const float before = i ? (x.time - c.x.keys[i - 1].time) / 3 : 0;
        const float after = i + 1 < c.x.keys.size() ? (c.x.keys[i + 1].time - x.time) / 3 : 0;
        const MATH::Vec3 position{x.value, y.value, z.value};
        result.push_back({layer.startDelay + layer.animationTiming.LayerSeconds(x.time, span), position,
                          position - MATH::Vec3{x.inTangent, y.inTangent, z.inTangent} * before,
                          position + MATH::Vec3{x.outTangent, y.outTangent, z.outTangent} * after});
    }
    return result;
}
bool VfxWorkspaceController::SetLayerBezierPath(std::string_view layerId, std::span<const VfxPathKnot> knots,
                                                std::string& message, uint64_t mergeGroup) {
    auto next = document_.Asset();
    auto* layer = FindEmitterMutable(next, layerId);
    const auto finite = [](const MATH::Vec3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    if (!layer || layer->kind == VFX::VfxLayerKind::Particles || layer->kind == VFX::VfxLayerKind::Adjustment ||
        knots.size() < 2 || knots.size() > VFX::kMaxVfxLayerCurveKeys) {
        message = "Bezier motion paths require a Shape/Group and 2..256 knots";
        return false;
    }
    std::vector<float> times;
    for (const auto& knot : knots) {
        float t = 0;
        if (!finite(knot.position) || !finite(knot.incoming) || !finite(knot.outgoing) ||
            !ResolveVfxLayerKeyTime(next, *layer, knot.seconds, t, message) ||
            (!times.empty() && t - times.back() < 0.00001f)) {
            message = "Path knots must be finite, strictly ordered and inside the layer interval";
            return false;
        }
        times.push_back(t);
    }
    auto& value = layer->position.value;
    VFX::VfxCurveData* curves[]{&value.curve.x, &value.curve.y, &value.curve.z};
    for (size_t axis = 0; axis < 3; ++axis) {
        auto& curve = *curves[axis];
        const auto previous = curve.keys;
        curve.keys.clear();
        curve.interpolation = VFX::VfxCurveInterpolation::Smooth;
        curve.randomOffset = VFX::MakeVfxFloatConstant(0);
        for (size_t i = 0; i < knots.size(); ++i) {
            const auto& knot = knots[i];
            const auto incoming = i ? (knot.position - knot.incoming) * (3 / (times[i] - times[i - 1])) : MATH::Vec3{};
            const auto outgoing =
                i + 1 < knots.size() ? (knot.outgoing - knot.position) * (3 / (times[i + 1] - times[i])) : MATH::Vec3{};
            const float values[]{knot.position.x, knot.position.y, knot.position.z};
            const float in[]{incoming.x, incoming.y, incoming.z}, out[]{outgoing.x, outgoing.y, outgoing.z};
            const auto old = std::find_if(previous.begin(), previous.end(),
                                          [&](const auto& k) { return std::abs(k.time - times[i]) < 0.00001f; });
            curve.keys.push_back({old == previous.end() ? AllocateStableId("path.key") : old->id, times[i],
                                  values[axis], in[axis], out[axis]});
        }
    }
    value.mode = VFX::VfxAnimatedValueMode::Curve;
    return ApplyAsset(std::move(next), message, mergeGroup);
}
bool VfxWorkspaceController::SetLayerPathControl(std::string_view layerId, float seconds, bool incoming,
                                                 const MATH::Vec3& point, std::string& message, uint64_t mergeGroup) {
    const auto* layer = FindEmitter(layerId);
    if (!layer)
        return false;
    const float span =
        layer->activeDuration > 0 ? layer->activeDuration : document_.Asset().durationSeconds - layer->startDelay;
    auto knots = ReadVfxBezierPath(*layer, span);
    const auto found = std::find_if(knots.begin(), knots.end(),
                                    [&](const auto& k) { return std::abs(k.seconds - seconds) < 0.0001f; });
    if (found == knots.end()) {
        message = "Bezier path knot not found";
        return false;
    }
    (incoming ? found->incoming : found->outgoing) = point;
    return SetLayerBezierPath(layerId, knots, message, mergeGroup);
}
void VfxWorkspaceController::DrawLayerPath(const VFX::VfxEmitterData& layer) {
    if (layer.kind != VFX::VfxLayerKind::Shape && layer.kind != VFX::VfxLayerKind::Group)
        return;
    if (!ImGui::TreeNode(VfxUiText("Bezier motion path").c_str()))
        return;
    const float span =
        layer.activeDuration > 0 ? layer.activeDuration : document_.Asset().durationSeconds - layer.startDelay;
    auto knots = ReadVfxBezierPath(layer, span);
    if (knots.empty()) {
        if (layer.position.value.mode == VFX::VfxAnimatedValueMode::Fixed) {
            if (ImGui::Button(VfxUiText("Create path").c_str())) {
                const auto p = layer.position.value.fixed.minimum;
                const auto end = p + MATH::Vec3{0, 1, 0};
                const VfxPathKnot initial[]{{layer.startDelay, p, p, p + MATH::Vec3{0, 0.333333f, 0}},
                                            {layer.startDelay + span, end, p + MATH::Vec3{0, 0.666667f, 0}, end}};
                (void)SetLayerBezierPath(layer.id, initial, statusMessage_);
                timelineEdit_.showMotionPath = true;
            }
        } else
            ImGui::TextWrapped("%s", VfxUiText("Bezier editing requires synchronized smooth XYZ keys.").c_str());
    } else {
        ImGui::Checkbox(VfxUiText("Show motion path").c_str(), &timelineEdit_.showMotionPath);
        bool orient = layer.position.orientAlongPath;
        if (ImGui::Checkbox(TEXT::Localize("ui.vfx.path.orient").c_str(), &orient))
            (void)SetProperty(layer.position.id, "orientAlongPath", orient, statusMessage_);
        bool changed = false, seal = false;
        size_t split = knots.size(), remove = knots.size();
        for (size_t i = 0; i < knots.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& knot = knots[i];
            if (ImGui::TreeNode("knot", TEXT::Localize("editor.ui.s.zu.3f.s.5a219d").c_str(),
                                VfxUiText("Point").c_str(), i + 1, knot.seconds)) {
                auto position = knot.position;
                if (ImGui::DragFloat3(VfxUiText("Position").c_str(), &position.x, 0.01f)) {
                    knot.incoming = knot.incoming + (position - knot.position);
                    knot.outgoing = knot.outgoing + (position - knot.position);
                    knot.position = position;
                    changed = true;
                }
                seal |= ImGui::IsItemDeactivatedAfterEdit();
                if (i) {
                    changed |= ImGui::DragFloat3(VfxUiText("Incoming handle").c_str(), &knot.incoming.x, 0.01f);
                    seal |= ImGui::IsItemDeactivatedAfterEdit();
                }
                if (i + 1 < knots.size()) {
                    changed |= ImGui::DragFloat3(VfxUiText("Outgoing handle").c_str(), &knot.outgoing.x, 0.01f);
                    seal |= ImGui::IsItemDeactivatedAfterEdit();
                }
                if (i + 1 < knots.size() && knots.size() < VFX::kMaxVfxLayerCurveKeys &&
                    ImGui::SmallButton(VfxUiText("Split segment").c_str()))
                    split = i;
                if (knots.size() > 2 && i > 0 && i + 1 < knots.size() &&
                    ImGui::SmallButton(VfxUiText("Remove point").c_str()))
                    remove = i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (split + 1 < knots.size()) {
            auto& a = knots[split];
            auto& b = knots[split + 1];
            const auto p = (a.position + a.outgoing) * 0.5f;
            const auto q = (a.outgoing + b.incoming) * 0.5f;
            const auto r = (b.incoming + b.position) * 0.5f;
            const auto left = (p + q) * 0.5f, right = (q + r) * 0.5f;
            const VfxPathKnot middle{(a.seconds + b.seconds) * 0.5f, (left + right) * 0.5f, left, right};
            a.outgoing = p;
            b.incoming = r;
            knots.insert(knots.begin() + split + 1, middle);
            changed = true;
        } else if (remove < knots.size()) {
            knots.erase(knots.begin() + remove);
            changed = true;
        }
        if (changed)
            (void)SetLayerBezierPath(layer.id, knots, statusMessage_,
                                     VfxInspectorPropertyEditor::MergeGroup(layer.id, "path"));
        if (seal)
            SealPropertyEdit();
    }
    ImGui::TreePop();
}
} // namespace HIKARI::EDITOR
