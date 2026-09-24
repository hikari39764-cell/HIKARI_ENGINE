#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI {

class DocumentSceneBase;
class AssetDatabase;
struct SceneDocument;

namespace TOOLS::BAKING {

struct LightingBakeRequest {
    std::filesystem::path projectRoot{};
    std::string sceneGuid{};
    std::string sceneName{};
    std::string scenePath{};
    const SceneDocument* sceneDocument = nullptr;
    const AssetDatabase* assetDatabase = nullptr;
    LightingBakeTarget target = LightingBakeTarget::All;
    bool force = false;
};

enum class LightingBakeItemKind {
    ReflectionProbe,
    IrradianceVolume,
    LightmapObject,
};

struct LightingBakeItemStatus {
    LightingBakeItemKind kind = LightingBakeItemKind::ReflectionProbe;
    uint64_t objectId = 0;
    std::string name{};
    std::string recordId{};
    bool enabled = true;
    bool dirty = true;
    std::string dirtyReason{};
    std::string authoringHash{};
};

struct LightingBakeStatus {
    bool valid = false;
    std::string sceneHash{};
    std::string dependencyHash{};
    uint32_t totalCount = 0;
    uint32_t dirtyCount = 0;
    uint32_t readyCount = 0;
    std::vector<LightingBakeItemStatus> items{};
    std::vector<std::string> warnings{};
};

class LightingBakeService {
public:
    LightingBakeReport BakeDirtyLighting(DocumentSceneBase& scene) const;
    LightingBakeReport BakeAllLighting(DocumentSceneBase& scene) const;
    LightingBakeReport BakeSelectedLighting(DocumentSceneBase& scene) const;
    LightingBakeReport ClearLightingBake(const LightingBakeRequest& request) const;
    LightingBakeStatus AnalyzeLightingBakeStatus(const LightingBakeRequest& request) const;
};

} // namespace TOOLS::BAKING

} // namespace HIKARI
