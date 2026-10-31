#pragma once

#include <array>
#include <string>

#include <DirectXMath.h>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxParameterSet.h"

namespace HIKARI {

// 作者値のうち、プロファイルに依らず宣言できる項目。パラメータの値はプロファイルの定義で決まるので項目表には載せず、
// 部件が valuesInitialized と paramValues（スロットごとの 4 成分）を読み書きし、検査器はプロファイルのパラメータごとに
// 欄を出す。
struct MaterialFxSettings {
    std::string profileId{};
};

inline constexpr auto kMaterialFxFields = [](auto&& field) {
    using S = MaterialFxSettings;
    using namespace JSONFIELDS;
    field("profileId", &S::profileId, Label{"Profile"}, Help{"Material FX profile that defines the parameters."},
          Common{});
};

class MaterialFxComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "MaterialFxComponent"; }

    bool SetProfileId(std::string profileId);
    const std::string& GetProfileId() const;

    const std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>& GetParamValues() const;
    bool AreValuesInitialized() const;
    void SetParamValues(const DirectX::XMFLOAT4 (&values)[VFX::kMaterialFxUserCount], bool initialized);

    bool SetFloat(const std::string& key, float value);
    bool SetFloat2(const std::string& key, const DirectX::XMFLOAT2& value);
    bool SetFloat3(const std::string& key, const DirectX::XMFLOAT3& value);
    bool SetFloat4(const std::string& key, const DirectX::XMFLOAT4& value);
    bool SetToggle(const std::string& key, bool value);
    bool GetFloat(const std::string& key, float& out) const;
    bool GetFloat2(const std::string& key, DirectX::XMFLOAT2& out) const;
    bool GetFloat3(const std::string& key, DirectX::XMFLOAT3& out) const;
    bool GetFloat4(const std::string& key, DirectX::XMFLOAT4& out) const;
    bool GetToggle(const std::string& key, bool& out) const;
    bool ResetToProfileDefaults();

#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;

private:
    bool CommitParameterState(MaterialFxParameterSet next);

    MaterialFxParameterSet parameters_{};
};

} // namespace HIKARI
