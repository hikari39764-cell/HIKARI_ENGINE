#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"

#include <array>
#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

std::string ChildPath(std::string_view parent, std::string_view child) {
    return std::string(parent) + "." + std::string(child);
}

} // namespace

void VfxInspectorPropertyEditor::FloatDistribution(const char* label, std::string_view propertyPrefix,
                                                   const VFX::VfxFloatDistributionData& value, float speed,
                                                   float minimum, float maximum) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::PushID(VfxUiText(label).c_str());
    ImGui::SeparatorText(VfxUiText(label).c_str());
    constexpr std::array<std::string_view, 2u> modes{"Constant", "RandomRange"};
    Choice("Mode", ChildPath(propertyPrefix, "mode"), VFX::ToString(value.mode), modes.data(), modes.size());
    Float(value.mode == VFX::VfxDistributionMode::Constant ? "Value" : "Minimum", ChildPath(propertyPrefix, "minimum"),
          value.minimum, speed, minimum, maximum);
    if (value.mode == VFX::VfxDistributionMode::RandomRange) {
        Float("Maximum", ChildPath(propertyPrefix, "maximum"), value.maximum, speed, minimum, maximum);
        Unsigned("Random Group", ChildPath(propertyPrefix, "randomGroup"), value.randomGroup);
    }
    ImGui::PopID();
#else
    (void)label;
    (void)propertyPrefix;
    (void)value;
    (void)speed;
    (void)minimum;
    (void)maximum;
#endif
}

void VfxInspectorPropertyEditor::Vec3Distribution(const char* label, std::string_view propertyPrefix,
                                                  const VFX::VfxVec3DistributionData& value, float speed) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::PushID(VfxUiText(label).c_str());
    ImGui::SeparatorText(VfxUiText(label).c_str());
    constexpr std::array<std::string_view, 2u> modes{"Constant", "RandomRange"};
    Choice("Mode", ChildPath(propertyPrefix, "mode"), VFX::ToString(value.mode), modes.data(), modes.size());
    Vec3(value.mode == VFX::VfxDistributionMode::Constant ? "Value" : "Minimum", ChildPath(propertyPrefix, "minimum"),
         value.minimum, speed);
    if (value.mode == VFX::VfxDistributionMode::RandomRange) {
        Vec3("Maximum", ChildPath(propertyPrefix, "maximum"), value.maximum, speed);
        Unsigned("Random Group", ChildPath(propertyPrefix, "randomGroup"), value.randomGroup);
    }
    ImGui::PopID();
#else
    (void)label;
    (void)propertyPrefix;
    (void)value;
    (void)speed;
#endif
}

void VfxInspectorPropertyEditor::ColorDistribution(const char* label, std::string_view propertyPrefix,
                                                   const VFX::VfxColorDistributionData& value) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::PushID(VfxUiText(label).c_str());
    ImGui::SeparatorText(VfxUiText(label).c_str());
    constexpr std::array<std::string_view, 2u> modes{"Constant", "RandomRange"};
    Choice("Mode", ChildPath(propertyPrefix, "mode"), VFX::ToString(value.mode), modes.data(), modes.size());
    Color(value.mode == VFX::VfxDistributionMode::Constant ? "Color" : "Minimum", ChildPath(propertyPrefix, "minimum"),
          value.minimum);
    if (value.mode == VFX::VfxDistributionMode::RandomRange) {
        Color("Maximum", ChildPath(propertyPrefix, "maximum"), value.maximum);
        Unsigned("Random Group", ChildPath(propertyPrefix, "randomGroup"), value.randomGroup);
    }
    ImGui::PopID();
#else
    (void)label;
    (void)propertyPrefix;
    (void)value;
#endif
}

void VfxInspectorPropertyEditor::AnimatedVec3(const char* label, std::string_view propertyPrefix,
                                              const VFX::VfxAnimatedVec3Data& value, float speed) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::PushID(VfxUiText(label).c_str());
    constexpr std::array<std::string_view, 4u> modes{"Fixed", "Pva", "Easing", "Curve"};
    Choice("Mode", ChildPath(propertyPrefix, "mode"), VFX::ToString(value.mode), modes.data(), modes.size());
    switch (value.mode) {
    case VFX::VfxAnimatedValueMode::Fixed:
        Vec3Distribution("Value", ChildPath(propertyPrefix, "fixed"), value.fixed, speed);
        break;
    case VFX::VfxAnimatedValueMode::Pva:
        Vec3Distribution("Initial", ChildPath(propertyPrefix, "pva.initial"), value.pva.initial, speed);
        Vec3Distribution("Velocity", ChildPath(propertyPrefix, "pva.velocity"), value.pva.velocity, speed);
        Vec3Distribution("Acceleration", ChildPath(propertyPrefix, "pva.acceleration"), value.pva.acceleration, speed);
        break;
    case VFX::VfxAnimatedValueMode::Easing: {
        constexpr std::array<std::string_view, 4u> easing{"Linear", "EaseIn", "EaseOut", "EaseInOut"};
        Choice("Function", ChildPath(propertyPrefix, "easing.function"), VFX::ToString(value.easing.function),
               easing.data(), easing.size());
        Bool("Use Middle", ChildPath(propertyPrefix, "easing.useMiddle"), value.easing.useMiddle);
        Vec3Distribution("Start", ChildPath(propertyPrefix, "easing.start"), value.easing.start, speed);
        if (value.easing.useMiddle) {
            Vec3Distribution("Middle", ChildPath(propertyPrefix, "easing.middle"), value.easing.middle, speed);
        }
        Vec3Distribution("End", ChildPath(propertyPrefix, "easing.end"), value.easing.end, speed);
        break;
    }
    case VFX::VfxAnimatedValueMode::Curve: {
        constexpr std::array<std::string_view, 3u> interpolation{"Linear", "Smooth", "Step"};
        const VFX::VfxCurveData* curves[] = {&value.curve.x, &value.curve.y, &value.curve.z};
        constexpr const char* channels[] = {"X", "Y", "Z"};
        for (size_t index = 0u; index < 3u; ++index) {
            const std::string prefix =
                ChildPath(propertyPrefix, std::string("curve.") + static_cast<char>('x' + index));
            ImGui::PushID(channels[index]);
            Choice("Interpolation", ChildPath(prefix, "interpolation"), VFX::ToString(curves[index]->interpolation),
                   interpolation.data(), interpolation.size());
            FloatDistribution("Random Offset", ChildPath(prefix, "randomOffset"), curves[index]->randomOffset, speed);
            ImGui::TextDisabled(VfxUiText("%s channel: %zu key(s); edit in Curves below").c_str(), channels[index],
                                curves[index]->keys.size());
            ImGui::PopID();
        }
        break;
    }
    }
    ImGui::PopID();
#else
    (void)label;
    (void)propertyPrefix;
    (void)value;
    (void)speed;
#endif
}

void VfxInspectorPropertyEditor::AnimatedColor(const char* label, std::string_view propertyPrefix,
                                               const VFX::VfxAnimatedColorData& value) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::PushID(VfxUiText(label).c_str());
    constexpr std::array<std::string_view, 5u> modes{"Fixed", "RandomRange", "Easing", "Curve", "Gradient"};
    if (ImGui::BeginCombo(VfxUiText("Mode").c_str(), VfxUiText(VFX::ToString(value.mode)).c_str())) {
        for (const std::string_view mode : modes) {
            if (!ImGui::Selectable(VfxUiText(mode).c_str(), mode == VFX::ToString(value.mode))) {
                continue;
            }
            if (mode == "Fixed" || mode == "RandomRange") {
                Set(ChildPath(propertyPrefix, "fixed.mode"), mode == "Fixed" ? "Constant" : "RandomRange", false);
            }
            Set(ChildPath(propertyPrefix, "mode"), mode, false);
        }
        ImGui::EndCombo();
    }
    switch (value.mode) {
    case VFX::VfxAnimatedColorMode::Fixed:
    case VFX::VfxAnimatedColorMode::RandomRange:
        ColorDistribution("Color", ChildPath(propertyPrefix, "fixed"), value.fixed);
        break;
    case VFX::VfxAnimatedColorMode::Easing: {
        constexpr std::array<std::string_view, 4u> easing{"Linear", "EaseIn", "EaseOut", "EaseInOut"};
        Choice("Function", ChildPath(propertyPrefix, "easing.function"), VFX::ToString(value.easing.function),
               easing.data(), easing.size());
        Bool("Use Middle", ChildPath(propertyPrefix, "easing.useMiddle"), value.easing.useMiddle);
        ColorDistribution("Start", ChildPath(propertyPrefix, "easing.start"), value.easing.start);
        if (value.easing.useMiddle) {
            ColorDistribution("Middle", ChildPath(propertyPrefix, "easing.middle"), value.easing.middle);
        }
        ColorDistribution("End", ChildPath(propertyPrefix, "easing.end"), value.easing.end);
        break;
    }
    case VFX::VfxAnimatedColorMode::Curve: {
        constexpr std::array<std::string_view, 3u> interpolation{"Linear", "Smooth", "Step"};
        const VFX::VfxCurveData* curves[] = {&value.curve.r, &value.curve.g, &value.curve.b, &value.curve.a};
        constexpr const char* channels[] = {"R", "G", "B", "A"};
        constexpr const char* ids[] = {"r", "g", "b", "a"};
        for (size_t index = 0u; index < 4u; ++index) {
            const std::string prefix = ChildPath(propertyPrefix, std::string("curve.") + ids[index]);
            ImGui::PushID(channels[index]);
            Choice("Interpolation", ChildPath(prefix, "interpolation"), VFX::ToString(curves[index]->interpolation),
                   interpolation.data(), interpolation.size());
            FloatDistribution("Random Offset", ChildPath(prefix, "randomOffset"), curves[index]->randomOffset);
            ImGui::TextDisabled(VfxUiText("%s channel: %zu key(s); edit in Curves below").c_str(), channels[index],
                                curves[index]->keys.size());
            ImGui::PopID();
        }
        break;
    }
    case VFX::VfxAnimatedColorMode::Gradient:
        ImGui::TextDisabled(VfxUiText("%zu stop(s); edit the gradient in Curves below").c_str(),
                            value.gradient.stops.size());
        break;
    }
    ImGui::PopID();
#else
    (void)label;
    (void)propertyPrefix;
    (void)value;
#endif
}

} // namespace HIKARI::EDITOR
