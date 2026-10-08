#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphData.h"
#include "Assets/Material/HIKARI_MaterialAssetTypes.h"
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {

struct MaterialTextureSlotData {
    bool useTexture = false;
    AssetGuid textureAssetGuid{};
    int texCoord = 0;
    MATH::Vec2 uvScale{1.0f, 1.0f};
    MATH::Vec2 uvOffset{0.0f, 0.0f};
    float uvRotation = 0.0f;
};

struct PbrMaterialAssetData {
    uint32_t version = 3;
    std::string materialName = "New Material";

    MaterialTextureSlotData baseColorTexture{};
    MaterialTextureSlotData normalTexture{};
    MaterialTextureSlotData metallicRoughnessTexture{};
    MaterialTextureSlotData occlusionTexture{};
    MaterialTextureSlotData emissiveTexture{};
    MaterialTextureSlotData specularTexture{};
    MaterialTextureSlotData specularColorTexture{};

    MATH::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float specularFactor = 1.0f;
    MATH::Vec3 specularColorFactor{1.0f, 1.0f, 1.0f};
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    MATH::Vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 1.0f;
    // クリアコート：F0 0.04 の第二の誘電体鏡面層。基底との配分は Fresnel で決まる。
    float clearCoatFactor = 0.0f;
    float clearCoatRoughnessFactor = 0.1f;
    // 透過：Blend 材質が透明 pass で不透明の場面色を屈折して透かす割合と屈折率。
    float transmissionFactor = 0.0f;
    float ior = 1.5f;
    // 次表面散乱：拡散光のうち散る割合と散乱半径 [m]。0 で散らない（画面空間の分離ぼかしが読む）。
    float subsurfaceFactor = 0.0f;
    float subsurfaceRadius = 0.02f;

    bool doubleSided = false;
    bool unlit = false;
};

struct MaterialSurfaceData {
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    MaterialAlphaRepresentation alphaRepresentation = MaterialAlphaRepresentation::Straight;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool unlit = false;
};

struct MaterialFunctionData {
    std::vector<MaterialGraphParameterData> inputs{};
    std::vector<MaterialGraphParameterData> outputs{};
    MaterialGraphData graph{};
};

struct MaterialInstanceParameterOverrideData {
    std::string parameterId{};
    MaterialValueData value{};
};

struct MaterialInstanceResourceOverrideData {
    std::string resourceId{};
    AssetGuid assetGuid{};
};

struct MaterialInstanceData {
    AssetGuid parentMaterialGuid{};
    std::vector<MaterialInstanceParameterOverrideData> parameterOverrides{};
    std::vector<MaterialInstanceResourceOverrideData> resourceOverrides{};
};

struct MaterialProfileData {
    MaterialTemplateId baseTemplate = MaterialTemplateId::GenericToon;
    std::vector<AssetGuid> functionAssetGuids{};
    std::vector<AssetGuid> rampAssetGuids{};
    std::vector<MaterialFeatureRequest> featureRequests{};
};

struct MaterialRampPointData {
    std::string id{};
    MATH::Vec2 position{0.0f, 0.0f};
    MATH::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MaterialRampData {
    MaterialRampDimension dimension = MaterialRampDimension::OneD;
    MaterialRampInterpolation interpolation = MaterialRampInterpolation::Linear;
    uint32_t width = 256u;
    uint32_t height = 1u;
    std::vector<MaterialRampPointData> points{};
};

struct MaterialAssetData {
    uint32_t version = kMaterialAssetSourceVersion;
    MaterialAssetKind kind = MaterialAssetKind::Material;
    std::string materialName = "New Material";
    MaterialDomain domain = MaterialDomain::Surface;
    MaterialTemplateId templateId = MaterialTemplateId::StandardPbr;
    AssetGuid profileAssetGuid{};
    std::vector<MaterialFeatureRequest> featureRequests{};
    MaterialSurfaceData surface{};
    PbrMaterialAssetData pbr{};
    MaterialGraphData graph{};
    MaterialFunctionData function{};
    MaterialInstanceData instance{};
    MaterialProfileData profile{};
    MaterialRampData ramp{};
};

MaterialAssetData MakeStandardPbrMaterialAssetData(const PbrMaterialAssetData& pbr = {});

// One-way graph -> PBR compatibility sync. Called on material commit so
// node-graph edits (including AI edits) keep the legacy pbr factor block
// consistent for the runtime compatibility path and save/reopen rounds.
void SyncPbrFromGraphParameters(MaterialAssetData& material);

PbrMaterialAssetData ProjectPbrMaterialAssetData(const MaterialAssetData& data);

bool LoadMaterialAssetData(const std::filesystem::path& path, MaterialAssetData& outData, std::string& outError,
                           std::vector<std::string>* outDiagnostics = nullptr);

bool SaveMaterialAssetData(const std::filesystem::path& path, const MaterialAssetData& data, std::string& outError);

// v1-v3 の固定 PBR API は既存 consumer の移行窓口として保持する。
bool LoadPbrMaterialAssetData(const std::filesystem::path& path, PbrMaterialAssetData& outData, std::string& outError);

bool SavePbrMaterialAssetData(const std::filesystem::path& path, const PbrMaterialAssetData& data,
                              std::string& outError);

} // namespace HIKARI
