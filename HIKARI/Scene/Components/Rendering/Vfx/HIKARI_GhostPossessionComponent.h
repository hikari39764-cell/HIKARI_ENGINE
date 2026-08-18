#pragma once

#include <string>

#include <DirectXMath.h>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {

    struct GhostPossessionSettings {
        bool enabled = true;
        bool possessed = true;
        bool previewInEditor = true;
        std::string materialFxProfileId{ "ghost_possession" };
        float intensity = 1.0f;
        float blendInSeconds = 0.45f;
        float blendOutSeconds = 0.65f;
        float rimStrength = 2.25f;
        float rimPower = 2.4f;
        float dissolveAmount = 0.18f;
        float edgeWidth = 0.09f;
        float pulseSpeed = 3.2f;
        float edgeBoost = 1.65f;
        float noiseScale = 3.0f;
        float flowSpeed = 0.85f;
        float flowStrength = 0.75f;
        float distortionStrength = 0.008f;
        float sceneColorMix = 0.24f;
        float sceneColorNoiseScale = 2.3f;
        VfxSlotId enterSlotId{};
        VfxSlotId loopSlotId{};
        VfxSlotId exitSlotId{};
    };

    GhostPossessionSettings SanitizeGhostPossessionSettings(
        GhostPossessionSettings settings) noexcept;

    void ResolveGhostPossessionMaterialValues(
        const GhostPossessionSettings& settings,
        float blendWeight,
        DirectX::XMFLOAT4
            (&outValues)[VFX::kMaterialFxUserCount]) noexcept;

    class GhostPossessionComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "GhostPossessionComponent";
        }

        const GhostPossessionSettings& GetSettings() const noexcept;
        void SetSettings(GhostPossessionSettings settings) noexcept;

        bool IsEnabled() const noexcept;
        void SetEnabled(bool enabled) noexcept;
        bool IsPossessed() const noexcept;
        void SetPossessed(bool possessed) noexcept;
        bool IsEditorPreviewEnabled() const noexcept;

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        bool TryDeserialize(
            const nlohmann::json& in,
            std::string& outDiagnostic) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        GhostPossessionSettings settings_{};
    };

} // namespace HIKARI
