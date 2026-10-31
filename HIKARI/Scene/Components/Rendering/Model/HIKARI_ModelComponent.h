#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

struct AuthoringFieldDescriptor;

class ModelAsset;

enum class ModelRenderDebugMode {
    Normal,
    WireOverlay,
    WireOnly,
    BoundsOnly,
};

inline constexpr TEXT::EnumName<ModelRenderDebugMode> kModelRenderDebugModeNames[] = {
    {ModelRenderDebugMode::Normal, "Normal"},
    {ModelRenderDebugMode::WireOverlay, "WireOverlay"},
    {ModelRenderDebugMode::WireOnly, "WireOnly"},
    {ModelRenderDebugMode::BoundsOnly, "BoundsOnly"},
};

// 作者値（シーン文書の properties と同じ形）のうち、項目表（kModelFields）で宣言できる項目。材質セットの選択
// （materialSetAssetGuid。項目の有無が「作者が選んだか」を表す）と材質の上書き（materialOverrides）は、モデルの材質
// スロットごとに決まるので、部件が読み書きし、検査器が欄を出す。
struct ModelSettings {
    std::string assetId{};
    bool visible = true;
    bool castShadow = true;
    bool receiveShadow = true;
    bool renderStatic = false;
    bool includeInLightingBake = true;
    // 最上位ビットは実行時専用（SetRuntimePostGroupMask）なので保存しない。
    uint32_t postGroupMask = 0;
    ModelRenderDebugMode debugRenderMode = ModelRenderDebugMode::Normal;
    uint32_t wireColor = 0x00FFAAFF;
    uint32_t maxWireLines = 20000;
    bool wirePerPrimitiveColor = true;
};

// wireColor（パックした RGBA）の編集欄（kModelFields の Draw）。色として編集し、アルファは保つ。
bool DrawModelWireColor(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field, uint32_t& value,
                        const ModelSettings& owner);

inline constexpr auto kModelFields = [](auto&& field) {
    using S = ModelSettings;
    using namespace JSONFIELDS;
    constexpr auto wired = [](const S& s) { return s.debugRenderMode != ModelRenderDebugMode::Normal; };
    constexpr auto lined = [](const S& s) {
        return s.debugRenderMode == ModelRenderDebugMode::WireOverlay ||
               s.debugRenderMode == ModelRenderDebugMode::WireOnly;
    };
    field("assetId", &S::assetId, Label{"Model Asset"}, Help{"Model asset GUID assigned to this component."},
          AssetRef{AssetType::Model}, Common{});
    field("visible", &S::visible, Label{"Visible"}, Help{"Controls whether the model is submitted for rendering."},
          Common{});
    field("castShadow", &S::castShadow, Label{"Cast Shadow"}, Help{"Controls whether the model casts shadows."},
          Common{});
    field("receiveShadow", &S::receiveShadow, Label{"Receive Shadow"},
          Help{"Controls whether the model receives shadows."}, Common{});
    field("renderStatic", &S::renderStatic, Label{"Render Static"},
          Help{"Marks the render surface as static authoring data."}, Common{});
    field("includeInLightingBake", &S::includeInLightingBake, Label{"Include In Lighting Bake"},
          Help{"Includes the model surface in authored lighting bakes. Surface GI needs a unique UV1 inside 0-1; if it "
               "is invalid, the Probe / Sky fallback stays active."},
          Common{});
    field("postGroupMask", &S::postGroupMask, Label{"Post Group Mask"}, Help{"Post-process group bit mask."});

    constexpr const char* kDebug = "Debug Display";
    field("debugRenderMode", &S::debugRenderMode, kModelRenderDebugModeNames, Label{"Render Debug Mode"},
          Help{"Draw the model normally, with a wireframe overlay, as wireframe only, or as its bounding box."},
          Group{kDebug});
    field("wireColor", &S::wireColor, Label{"Wire Color"}, Draw{&DrawModelWireColor}, Show{wired}, Group{kDebug});
    field("maxWireLines", &S::maxWireLines, Range{1.0, 2147483647.0}, Label{"Max Wire Lines"},
          Help{"Line budget for this model; lines beyond it are skipped."}, Show{lined}, Group{kDebug});
    field("wirePerPrimitiveColor", &S::wirePerPrimitiveColor, Label{"Color Per Primitive"},
          Help{"Give each mesh primitive its own wire colour instead of Wire Color."}, Show{lined}, Group{kDebug});
};

// 最上位ビットを落とし、範囲へ丸める。
ModelSettings NormalizeModelSettings(ModelSettings settings);

struct ModelMaterialOverrideSlot {
    MaterialSlotId slotId{};
    AssetGuid materialAssetGuid{};
};

struct RuntimeMaterialOverrideSlot {
    uint32_t materialIndex = 0;
    std::unique_ptr<Material> material{};
};

struct RuntimeModelNodeVisibilityOverride {
    std::string nodeName{};
    bool visible = true;
    bool includeDescendants = true;
};

class ModelComponent final : public IComponent, public IGeometryFitProvider {
public:
    ~ModelComponent() override;

    std::string_view GetTypeName() const override { return "ModelComponent"; }

    void SetModelAsset(ModelAsset* asset);
    ModelAsset* GetModelAsset();
    const ModelAsset* GetModelAsset() const;

    void SetVisible(bool visible);
    bool IsVisible() const;
    bool GetCastShadow() const;
    bool GetReceiveShadow() const;
    void SetRenderStatic(bool enabled);
    bool IsRenderStatic() const;
    void SetIncludeInLightingBake(bool enabled);
    bool GetIncludeInLightingBake() const;
    ModelRenderDebugMode GetRenderDebugMode() const;
    uint32_t GetWireColor() const;
    uint32_t GetMaxWireLines() const;
    bool GetWirePerPrimitiveColor() const;

#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    bool QueryGeometryFit(GeometryFitDesc& outFit) const noexcept override;

    const std::string& GetAssetId() const;
    void SetAssetId(std::string assetId);

    const AssetGuid& GetMaterialSetAssetGuid() const noexcept;
    bool HasAuthoredMaterialSetSelection() const noexcept;
    void SetMaterialSetAssetGuid(AssetGuid materialSetGuid);
    void UseImportedModelMaterials();
    void ClearMaterialSetAssetGuid();

    uint32_t GetPostGroupMask() const;
    bool SetRuntimePostGroupMask(uint32_t mask);
    void ClearRuntimePostGroupMask();

    const std::vector<ModelMaterialOverrideSlot>& GetMaterialOverrides() const;
    void SetMaterialOverride(MaterialSlotId slotId, AssetGuid materialGuid);
    void ClearMaterialOverride(MaterialSlotId slotId);
    Material* GetRuntimeMaterialOverride(uint32_t slotIndex);
    const Material* GetRuntimeMaterialOverride(uint32_t slotIndex) const;
    void SetRuntimeMaterialOverride(uint32_t slotIndex, std::unique_ptr<Material> material);
    void ClearRuntimeMaterialOverride(uint32_t slotIndex);
    void ClearRuntimeMaterialOverride(MaterialSlotId slotId);
    void ClearRuntimeMaterialOverrides();
    const std::vector<RuntimeMaterialOverrideSlot>& GetRuntimeMaterialOverrides() const noexcept;
    bool SetRuntimeNodeVisibilityOverride(std::string_view nodeName, bool visible, bool includeDescendants);
    bool ClearRuntimeNodeVisibilityOverride(std::string_view nodeName);
    void ClearRuntimeNodeVisibilityOverrides();
    const std::vector<RuntimeModelNodeVisibilityOverride>& GetRuntimeNodeVisibilityOverrides() const noexcept;
    // Resolves the stored exact-name overrides against one concrete model
    // snapshot. Empty means all nodes are visible.
    std::vector<uint8_t> ResolveRuntimeNodeVisibilityMask(const ModelAsset& model) const;

private:
    ModelAsset* asset_ = nullptr;
    ModelSettings settings_{};
    std::optional<uint32_t> runtimePostGroupMask_{};
    AssetGuid materialSetAssetGuid_{};
    bool materialSetSelectionAuthored_ = false;
    std::vector<ModelMaterialOverrideSlot> materialOverrides_{};
    std::vector<RuntimeMaterialOverrideSlot> runtimeMaterialOverrides_{};
    std::vector<RuntimeModelNodeVisibilityOverride> runtimeNodeVisibilityOverrides_{};
};

} // namespace HIKARI
