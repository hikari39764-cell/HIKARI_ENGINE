#pragma once

#include <string>
#include <vector>

#include "Assets/HIKARI_AssetTypeEnum.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

struct AuthoringFieldDescriptor;

// 作者値（シーン文書の properties と同じ形）。形状（ProceduralMeshSettings）にマテリアルの選択を足したもの。項目表
// （kProceduralMeshFields）が保存・読み取り・検査器・説明を決める。切り抜き（cuts）は項目表に載せず、部件が
// WriteProceduralMeshCuts / ReadProceduralMeshCuts で読み書きする（視点側の形状編集も同じ関数を使う）。
struct ProceduralMeshComponentSettings : ProceduralMeshSettings {
    // "" = 既定の白、または マテリアルアセットの GUID（内蔵のチェッカーなども ID で持つ）。
    std::string materialSelection{};
};

// マテリアル選択の編集欄（kProceduralMeshFields の Draw）。「なし」「チェッカー（デバッグ）」と、プロジェクトのマテリアル
// アセットから選ぶ。
bool DrawProceduralMaterialSelection(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field,
                                     std::string& value, const ProceduralMeshComponentSettings& owner);

inline constexpr auto kProceduralMeshFields = [](auto&& field) {
    using S = ProceduralMeshComponentSettings;
    using B = ProceduralMeshSettings;
    using namespace JSONFIELDS;
    // 形ごとに意味を持つ項目だけを出す。
    constexpr auto hasHeight = [](const S& s) {
        return s.kind != ProceduralMeshKind::Plane && s.kind != ProceduralMeshKind::GridPlane &&
               s.kind != ProceduralMeshKind::Sphere;
    };
    constexpr auto hasDepth = [](const S& s) {
        return s.kind != ProceduralMeshKind::Sphere && s.kind != ProceduralMeshKind::Cylinder &&
               s.kind != ProceduralMeshKind::Capsule;
    };
    constexpr auto isGrid = [](const S& s) { return s.kind == ProceduralMeshKind::GridPlane; };
    constexpr auto hasRows = [](const S& s) {
        return s.kind == ProceduralMeshKind::GridPlane || s.kind == ProceduralMeshKind::Cylinder ||
               s.kind == ProceduralMeshKind::Stairs;
    };
    constexpr auto hasRadialSegments = [](const S& s) {
        return s.kind == ProceduralMeshKind::Sphere || s.kind == ProceduralMeshKind::Cylinder ||
               s.kind == ProceduralMeshKind::Capsule;
    };
    constexpr auto hasStacks = [](const S& s) {
        return s.kind == ProceduralMeshKind::Sphere || s.kind == ProceduralMeshKind::Capsule;
    };

    field("kind", &B::kind, kProceduralMeshKindNames, Label{"Shape"}, Common{});
    field("width", &B::width, Range{0.001, 100000.0}, Label{"Width"},
          Help{"Size along local X. Sphere, cylinder and capsule use it as the diameter."});
    field("height", &B::height, Range{0.001, 100000.0}, Label{"Height"},
          Help{"Size along local Y. A capsule's height includes both end caps."}, Show{hasHeight});
    field("depth", &B::depth, Range{0.001, 100000.0}, Label{"Depth"}, Show{hasDepth});
    field("segmentsX", &B::segmentsX, Range{1.0, 512.0}, Label{"Columns"}, Show{isGrid});
    field("segmentsY", &B::segmentsY, Range{1.0, 512.0}, Label{"Rows / Height Segments / Steps"}, Show{hasRows});
    // どの形も使わない。保存だけする。
    field("segmentsZ", &B::segmentsZ, Range{1.0, 64.0}, Label{"Segments Z"}, Hidden{});
    field("sphereSlices", &B::sphereSlices, Range{3.0, 512.0}, Label{"Radial Segments"}, Show{hasRadialSegments});
    field("sphereStacks", &B::sphereStacks, Range{2.0, 256.0}, Label{"Vertical Segments"}, Show{hasStacks});
    field("doubleSided", &B::doubleSided, Label{"Double Sided"});
    field("generateTangents", &B::generateTangents, Label{"Generate Tangents"});
    field("materialSelection", &S::materialSelection, Label{"Material"},
          Help{"Material asset GUID assigned to the procedural surface; empty selects the default material."},
          AssetRef{AssetType::Material}, Draw{&DrawProceduralMaterialSelection});
    // 地形が当てはめる UV の繰り返し数。保存だけする。
    field("uvTilesX", &B::uvTilesX, Range{0.0, 4096.0}, Label{"UV Tiles X"}, Hidden{});
    field("uvTilesY", &B::uvTilesY, Range{0.0, 4096.0}, Label{"UV Tiles Y"}, Hidden{});
};

// 形ごとの規則で丸める（SanitizeProceduralMeshSettings）。
ProceduralMeshComponentSettings NormalizeProceduralMeshSettings(ProceduralMeshComponentSettings settings);

class ProceduralMeshComponent final : public IComponent, public IGeometryFitProvider {
public:
    std::string_view GetTypeName() const override { return "ProceduralMeshComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const ProceduralMeshSettings& GetSettings() const noexcept;
    void SetSettings(const ProceduralMeshSettings& settings);
    // マテリアル選択: "" = 既定の白、またはマテリアルアセットの GUID。
    const std::string& GetMaterialSelection() const noexcept;
    void SetMaterialSelection(std::string selection);
    int GetGeometryFitPriority() const noexcept override;
    bool QueryGeometryFit(GeometryFitDesc& outFit) const noexcept override;
    // 切り抜いた箱は、残りを重ならない箱の組で返す（衝突を箱だけで正確に作る）。
    void QueryGeometryFitParts(std::vector<GeometryFitDesc>& outParts) const override;

private:
    ProceduralMeshComponentSettings settings_{};
};

} // namespace HIKARI
