#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

namespace ModelComponentPropertyIds {
inline constexpr char AssetId[] = "assetId";
inline constexpr char Visible[] = "visible";
inline constexpr char CastShadow[] = "castShadow";
inline constexpr char ReceiveShadow[] = "receiveShadow";
inline constexpr char RenderStatic[] = "renderStatic";
inline constexpr char IncludeInLightingBake[] = "includeInLightingBake";
inline constexpr char MaterialSetAssetGuid[] = "materialSetAssetGuid";
inline constexpr char PostGroupMask[] = "postGroupMask";
} // namespace ModelComponentPropertyIds

class ModelAsset;

enum class ModelRenderDebugMode {
    Normal,
    WireOverlay,
    WireOnly,
    BoundsOnly,
};

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
    void NotifyRenderStateDirty();
    MaterialSlotId ResolveCurrentSlotId(uint32_t slotIndex) const noexcept;

    ModelAsset* asset_ = nullptr;
    std::string assetId_{};
    bool visible_ = true;
    bool showSkeletonDebug_ = false;
    bool skeletonDebugXRay_ = false;
    bool castShadow_ = true;
    bool receiveShadow_ = true;
    bool renderStatic_ = false;
    bool includeInLightingBake_ = true;
    ModelRenderDebugMode debugRenderMode_ = ModelRenderDebugMode::Normal;
    uint32_t wireColor_ = 0x00FFAAFF;
    uint32_t maxWireLines_ = 20000;
    bool wirePerPrimitiveColor_ = true;
    uint32_t postGroupMask_ = 0;
    std::optional<uint32_t> runtimePostGroupMask_{};
    AssetGuid materialSetAssetGuid_{};
    bool materialSetSelectionAuthored_ = false;
    std::vector<ModelMaterialOverrideSlot> materialOverrides_{};
    std::vector<RuntimeMaterialOverrideSlot> runtimeMaterialOverrides_{};
    std::vector<RuntimeModelNodeVisibilityOverride> runtimeNodeVisibilityOverrides_{};
};

} // namespace HIKARI
