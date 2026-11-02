#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightingBakePolicy.h"
#include "RenderContracts/Lighting/HIKARI_LightmapSurfaceContract.h"

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::ASSETS::LIGHTING {

constexpr uint32_t kLightingBakeManifestVersion = 8;
constexpr uint32_t kReflectionProbeBakePolicyVersion = 28;
// 15: 探針を光照贴图と同じ DXR の経路で焼く（光栅の cubemap 撮影をやめた）。
constexpr uint32_t kIrradianceVolumeBakePolicyVersion = 15;

struct ReflectionProbeBakeRecord {
    std::string id{};
    std::string name{};
    MATH::Vec3 position{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float intensity = 1.0f;
    std::string influenceShape{"Sphere"};
    MATH::Vec3 influenceBoxCenter{0.0f, 0.0f, 0.0f};
    MATH::Vec3 influenceBoxSize{0.0f, 0.0f, 0.0f};
    std::string projectionShape{"Infinite"};
    MATH::Vec3 projectionBoxCenter{0.0f, 0.0f, 0.0f};
    MATH::Vec3 projectionBoxSize{0.0f, 0.0f, 0.0f};
    float blendDistance = 1.0f;
    int priority = 0;
    std::string captureCubemapPath{};
    std::string prefilteredCubemapPath{};
    std::string brdfLutPath{};
    uint32_t prefilteredMipCount = 1;
    std::string authoringHash{};
    std::string captureSceneHash{};
    std::string dependencyHash{};
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

struct LightProbeBakeRecord {
    std::string id{};
    std::string name{};
    std::string type{"VolumeGrid"};
    MATH::Vec3 position{0.0f, 0.0f, 0.0f};
    MATH::Vec3 origin{0.0f, 0.0f, 0.0f};
    MATH::Vec3 size{0.0f, 0.0f, 0.0f};
    uint32_t countX = 0;
    uint32_t countY = 0;
    uint32_t countZ = 0;
    uint32_t shOrder = 3;
    uint32_t probeCount = 0;
    float blendDistance = 1.0f;
    int priority = 0;
    std::string shDataPath{};
    std::string authoringHash{};
    std::string captureSceneHash{};
    std::string dependencyHash{};
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

struct LightmapBakePage {
    std::string texturePath{};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t uvSet = kSurfaceLightmapUvSet;
    std::vector<LightmapSurfaceBinding> surfaceBindings{};
};

struct LightmapBakeRecord {
    std::string id{};
    std::string name{};
    uint64_t objectId = 0u;
    std::vector<LightmapBakePage> pages{};
    // 光照贴图の無い面（UV 生成の失敗、剪裁・半透明、未対応の頂点形式）の理由。物体全体なら pages は空。
    // 入力が変わるまで同じ結果なので焼き直さない。
    std::string noLightmapReason{};
    // 光照贴图の無い面の世界空間の範囲。焼き込み探針の体積がそこを覆うかを編集器が判定する。
    MATH::Vec3 noLightmapBoundsMin{};
    MATH::Vec3 noLightmapBoundsMax{};
    std::string encoding{"RGB16F_Irradiance"};
    std::string authoringHash{};
    std::string captureSceneHash{};
    std::string dependencyHash{};
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

inline std::string BuildLightmapPageResourceKey(const std::string& recordId, size_t pageIndex) {
    return "lightmap/" + recordId + "/page_" + std::to_string(pageIndex);
}

struct LightingBakeManifest {
    uint32_t version = kLightingBakeManifestVersion;
    std::string sceneGuid{};
    std::string bakeGuid{};
    uint32_t bakeVersion = 1;
    std::string generatedRoot{};
    std::string sceneHash{};
    std::string dependencyHash{};
    std::string activeScenario{"Default"};
    std::vector<ReflectionProbeBakeRecord> reflectionProbes{};
    std::vector<LightProbeBakeRecord> lightProbes{};
    std::vector<LightmapBakeRecord> lightmaps{};
};

bool LoadLightingBakeManifest(const std::filesystem::path& path, LightingBakeManifest& outManifest,
                              std::string* outMessage = nullptr);

bool SaveLightingBakeManifest(const std::filesystem::path& path, const LightingBakeManifest& manifest,
                              std::string* outMessage = nullptr);

std::filesystem::path BuildLightingBakeRoot(const std::filesystem::path& projectRoot, const std::string& sceneGuid);

std::filesystem::path BuildLightingBakeManifestPath(const std::filesystem::path& projectRoot,
                                                    const std::string& sceneGuid);

// 探針の撮影用の中間ファイル（captures ディレクトリの下、または *_capture.dds）か。ランタイム向けの出力には含めない。
// path は / 区切りの相対パス。大文字小文字は区別しない。
bool IsLightingBakeCaptureFile(std::string_view path);

} // namespace HIKARI::ASSETS::LIGHTING
