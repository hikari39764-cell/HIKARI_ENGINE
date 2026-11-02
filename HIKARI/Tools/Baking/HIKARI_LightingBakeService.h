#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Text/HIKARI_EnumNames.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
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

inline constexpr TEXT::EnumName<LightingBakeItemKind> kLightingBakeItemKindNames[] = {
    {LightingBakeItemKind::ReflectionProbe, "ReflectionProbe"},
    {LightingBakeItemKind::IrradianceVolume, "IrradianceVolume"},
    {LightingBakeItemKind::LightmapObject, "LightmapObject"},
};

inline const char* ToString(LightingBakeItemKind kind) noexcept {
    return TEXT::EnumToName(kLightingBakeItemKindNames, kind, "Unknown");
}

// 焼き込み対象ひとつの状態。名前は AI 向けの状態値と翻訳キー（ui.lightingBake.state.<名前>）を兼ねる。
enum class LightingBakeItemState : uint8_t {
    Ready,
    Disabled,
    NoManifest,
    NoRecord,
    SettingsChanged,
    SceneChanged,
    DependencyUnreadable,
    DependencyChanged,
    OutputMissing,
    // 光照贴图を作れなかった物体と、一部の面に光照贴图が無い物体（UV 生成の失敗、剪裁など）。入力が変わるまで焼き直さず、
    // その面は焼き込み探針・動的 GI・空の光の順で照らされる。
    NoLightmap,
    PartialLightmap,
};

inline constexpr TEXT::EnumName<LightingBakeItemState> kLightingBakeItemStateNames[] = {
    {LightingBakeItemState::Ready, "Ready"},
    {LightingBakeItemState::Disabled, "Disabled"},
    {LightingBakeItemState::NoManifest, "NoManifest"},
    {LightingBakeItemState::NoRecord, "NoRecord"},
    {LightingBakeItemState::SettingsChanged, "SettingsChanged"},
    {LightingBakeItemState::SceneChanged, "SceneChanged"},
    {LightingBakeItemState::DependencyUnreadable, "DependencyUnreadable"},
    {LightingBakeItemState::DependencyChanged, "DependencyChanged"},
    {LightingBakeItemState::OutputMissing, "OutputMissing"},
    {LightingBakeItemState::NoLightmap, "NoLightmap"},
    {LightingBakeItemState::PartialLightmap, "PartialLightmap"},
};

inline const char* ToString(LightingBakeItemState state) noexcept {
    return TEXT::EnumToName(kLightingBakeItemStateNames, state, "Unknown");
}

struct LightingBakeItemStatus {
    LightingBakeItemKind kind = LightingBakeItemKind::ReflectionProbe;
    uint64_t objectId = 0;
    std::string name{};
    std::string recordId{};
    bool enabled = true;
    bool dirty = true;
    LightingBakeItemState state = LightingBakeItemState::NoRecord;
    // NoLightmap / PartialLightmap の時だけ：光照贴图の無い面の理由と、その面を丸ごと覆う焼き込み探針の体積の名前
    // （空なら覆われず、動的 GI が無ければ遮られない空の光になる）。
    std::string noLightmapReason{};
    std::string coveringVolume{};
    std::string authoringHash{};
};

struct LightingBakeStatus {
    bool valid = false;
    std::string sceneHash{};
    std::string dependencyHash{};
    uint32_t totalCount = 0;
    uint32_t dirtyCount = 0;
    uint32_t readyCount = 0;
    // 光照贴图の無い面を持つ物体の数と、そのうち面が焼き込み探針の体積に覆われない物体の数。
    uint32_t noLightmapCount = 0;
    uint32_t uncoveredCount = 0;
    std::vector<LightingBakeItemStatus> items{};
    std::vector<std::string> warnings{};
};

class LightingBakeService {
public:
    // 開いている場面の焼き込みの要求（面板、AI の操作、一括の焼き込みが同じものを使う）。
    static LightingBakeRequest BuildRequest(DocumentSceneBase& scene);
    std::string GetBlockReason(const DocumentSceneBase& scene) const;
    LightingBakeReport ClearLightingBake(const LightingBakeRequest& request) const;
    LightingBakeStatus AnalyzeLightingBakeStatus(const LightingBakeRequest& request) const;
};

} // namespace TOOLS::BAKING

} // namespace HIKARI
