#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <json.hpp>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::EDITOR {

// 表示文言のみを翻訳し、保存するプロパティ名/列挙値は変更しない。
std::string VfxUiText(std::string_view text);

class VfxWorkspaceController;

class VfxInspectorPropertyEditor final {
public:
    VfxInspectorPropertyEditor(VfxWorkspaceController& controller, AssetDatabase& assetDatabase,
                               std::string_view targetId, std::string& statusMessage) noexcept;

    void Text(const char* label, std::string_view propertyId, const std::string& value);
    void Bool(const char* label, std::string_view propertyId, bool value);
    void Float(const char* label, std::string_view propertyId, float value, float speed = 0.01f, float minimum = 0.0f,
               float maximum = 0.0f);
    void Unsigned(const char* label, std::string_view propertyId, uint32_t value, uint32_t minimum = 0u,
                  uint32_t maximum = 0xffffffffu);
    void Vec3(const char* label, std::string_view propertyId, const MATH::Vec3& value, float speed = 0.01f);
    void Color(const char* label, std::string_view propertyId, const MATH::Vec4& value);
    void TypedValue(const char* label, std::string_view propertyId, const VFX::VfxValue& value);
    void Choice(const char* label, std::string_view propertyId, std::string_view current,
                const std::string_view* choices, size_t choiceCount);

    void FloatDistribution(const char* label, std::string_view propertyPrefix,
                           const VFX::VfxFloatDistributionData& value, float speed = 0.01f, float minimum = 0.0f,
                           float maximum = 0.0f);
    void Vec3Distribution(const char* label, std::string_view propertyPrefix, const VFX::VfxVec3DistributionData& value,
                          float speed = 0.01f);
    void ColorDistribution(const char* label, std::string_view propertyPrefix,
                           const VFX::VfxColorDistributionData& value);
    void AnimatedVec3(const char* label, std::string_view propertyPrefix, const VFX::VfxAnimatedVec3Data& value,
                      float speed = 0.01f);
    void AnimatedColor(const char* label, std::string_view propertyPrefix, const VFX::VfxAnimatedColorData& value);

    static uint64_t MergeGroup(std::string_view targetId, std::string_view propertyId) noexcept;

private:
    void Set(std::string_view propertyId, const nlohmann::json& value, bool continuous);

    VfxWorkspaceController& controller_;
    AssetDatabase& assetDatabase_;
    std::string targetId_{};
    std::string& statusMessage_;
};

} // namespace HIKARI::EDITOR
