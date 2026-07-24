#pragma once

#include <string>

#include <DirectXMath.h>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {

    class MaterialFxComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "MaterialFxComponent";
        }

        void SetProfileId(std::string profileId);
        const std::string& GetProfileId() const;

        const DirectX::XMFLOAT4 (&GetParamValues() const)
            [VFX::kMaterialFxUserCount];
        bool AreValuesInitialized() const;
        void SetParamValues(
            const DirectX::XMFLOAT4
                (&values)[VFX::kMaterialFxUserCount],
            bool initialized);

        bool SetFloat(const std::string& key, float value);
        bool SetFloat2(
            const std::string& key,
            const DirectX::XMFLOAT2& value);
        bool SetFloat3(
            const std::string& key,
            const DirectX::XMFLOAT3& value);
        bool SetFloat4(
            const std::string& key,
            const DirectX::XMFLOAT4& value);
        bool GetFloat(const std::string& key, float& out) const;
        void ResetToProfileDefaults();

        void RenderImGui() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        void NotifyRenderStateDirty();

        std::string profileId_{};
        DirectX::XMFLOAT4
            paramValues_[VFX::kMaterialFxUserCount]{};
        bool valuesInitialized_ = false;
    };

} // namespace HIKARI
