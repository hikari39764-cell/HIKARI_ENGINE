#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI/Project/EditorModuleUi.h"
#include "HIKARI/Project/EditorOperations.h"
#include "HIKARI_TerrainKitStore.h"
#include "TerrainKit/Data/HIKARI_TerrainDataBrush.h"
#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"
#include "TerrainKit/Data/HIKARI_TerrainDataErosion.h"
#include "TerrainKit/Data/HIKARI_TerrainDataStamp.h"

// 地形編集器の工具・窓・操作で共有する状態と手続き。人の操作と智能体の操作は同じ手続きを通る。
namespace HIKARI::TERRAINKIT {

inline constexpr char kSculptToolId[] = "terrainkit.sculpt";
inline constexpr char kMaskToolId[] = "terrainkit.mask";
inline constexpr char kStampToolId[] = "terrainkit.stamp";
inline constexpr char kPaintToolId[] = "terrainkit.paint";
inline constexpr char kSplineToolId[] = "terrainkit.spline";

// 編集器の所有スレッドだけで触る。
struct EditorShared {
    EDITOR_PROJECT::IEditorSceneServices* services = nullptr;
    EDITOR_PROJECT::IEditorWorkspaceCommands* commands = nullptr;
    TERRAIN::DATA::HeightBrushSettings brush{};
    TERRAIN::DATA::MaskBrushSettings maskBrush{};
    TERRAIN::DATA::ControlBrushSettings paintBrush{};
    TERRAIN::DATA::StampSettings stamp{};
    std::string stampName = "hill";
    TERRAIN::DATA::ErosionRequest erosion{};
    uint32_t selectedLayer = 0u;
    uint32_t strokes = 0u;
    // 筆の一度分（筆刷の適用と焼き直し）の手間。describe で読む。
    double brushFrameTotalMs = 0.0;
    double brushFrameMaxMs = 0.0;
    uint32_t brushFrames = 0u;
    // 最後に終わった GPU 侵蝕の結果（JSON 文本）。
    std::string lastErosion{};
};

// 筆や層の操作を書き込む先：選んでいる層が手彫り層ならそれ、でなければ一番上の手彫り層。
uint32_t TargetSculptLayer(const EditorShared& shared);

// 撤销へ載せる：瓦片の前後の画素と層の記述の差分。
void RecordTerrainEdit(EditorShared& shared, TERRAIN::DATA::TerrainEdit edit);

TERRAIN::DATA::NoiseLayerSettings DefaultNoise();
bool RegenerateTerrain(EditorShared& shared, const TERRAIN::DATA::NoiseLayerSettings& noise, std::string& error);

// 層の操作。どれも撤销一件で、終わると焼き直す。
bool AddTerrainLayer(EditorShared& shared, TERRAIN::DATA::HeightLayer layer, std::size_t index, uint32_t& outId,
                     std::string& error);
bool RemoveTerrainLayer(EditorShared& shared, uint32_t id, std::string& error);
bool MoveTerrainLayer(EditorShared& shared, uint32_t id, std::size_t index, std::string& error);
bool UpdateTerrainLayer(EditorShared& shared, const TERRAIN::DATA::HeightLayer& layer, std::string& error);

// 自動の材質規則を差し替える（撤销一件、全世界を背景で計算し直す）。
void ChangeSurfaceRules(EditorShared& shared, TERRAIN::DATA::SurfaceRuleSet rules);

// 積雪と湿りを変える（撤销一件）。
void ChangeWeather(EditorShared& shared, const TerrainWeather& weather);

// 曲線の層。既定の幅と材質はここだけが持つ。
float DefaultSplineWidth(TERRAIN::DATA::SplineKind kind);
TERRAIN::DATA::HeightLayer MakeSplineLayer(TERRAIN::DATA::SplineKind kind);
// その層より下の合成の高さ（層が無ければ全部の合成）。
std::optional<float> HeightBelowLayer(uint32_t layerId, float worldX, float worldZ);
// 曲線の層を差し替えて変わった範囲だけ焼き直す。撤销には載せない（引いている間）。
bool ApplySplineLayer(const TERRAIN::DATA::HeightLayer& next);
// 同じく、撤销一件で。
bool ChangeSplineLayer(EditorShared& shared, const TERRAIN::DATA::HeightLayer& next, std::string& error);

// 印章の名前（内蔵 + プロジェクトの Assets/TerrainStamps の画像）。
std::vector<std::string> ListStampNames();
bool RunStamp(EditorShared& shared, std::string_view stampName, const TERRAIN::DATA::StampSettings& settings,
              std::string& error);

// 矩形を侵蝕して手彫り層へ差分を足す。maps があれば流量・堆積・磨耗の図を返す。
bool RunErosion(EditorShared& shared, const TERRAIN::DATA::SampleRect& rect,
                const TERRAIN::DATA::ErosionRequest& request, TERRAIN::DATA::ErosionMaps* maps, std::string& error);
// GPU で侵蝕を始める（数フレームかけて走り、終わると層へ書いて撤销に載せ、図を書き出す）。
bool StartGpuErosion(const std::shared_ptr<EditorShared>& shared, const TERRAIN::DATA::SampleRect& rect,
                     const TERRAIN::DATA::ErosionRequest& request, std::string& error);
// 流量・堆積・磨耗の図を Library/TerrainKit へ書き、書いた先と値の範囲を JSON 文本で返す。
std::string WriteErosionMaps(const TerrainStore& store, const TERRAIN::DATA::SampleRect& rect,
                             const TERRAIN::DATA::ErosionMaps& maps);
// 地形全体を覆う標本の矩形。
TERRAIN::DATA::SampleRect WholeTerrainRect(const TerrainStore& store);

// 書き出し先（<root>/Library/TerrainKit）。
std::filesystem::path TerrainOutputDirectory(const TerrainStore& store);
bool WriteGrayPng(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<float>& values,
                  float& outMinimum, float& outMaximum);
bool WriteOverviewPng(const TerrainStore& store, uint32_t size, std::filesystem::path& outPath,
                      float& outMetersPerPixel, std::string& error);
// 焼いた高さを一枚の画像にして EXR と R16（格子の高さ範囲で 0..1）に書く。
bool ExportBakedHeight(const TerrainStore& store, std::filesystem::path& outExr, std::filesystem::path& outRaw,
                       uint32_t& outWidth, std::string& error);

std::unique_ptr<EDITOR_PROJECT::IEditorViewportTool> MakeSculptTool(std::shared_ptr<EditorShared> shared);
std::unique_ptr<EDITOR_PROJECT::IEditorViewportTool> MakeMaskTool(std::shared_ptr<EditorShared> shared);
std::unique_ptr<EDITOR_PROJECT::IEditorViewportTool> MakeStampTool(std::shared_ptr<EditorShared> shared);
std::unique_ptr<EDITOR_PROJECT::IEditorViewportTool> MakePaintTool(std::shared_ptr<EditorShared> shared);
std::unique_ptr<EDITOR_PROJECT::IEditorViewportTool> MakeSplineTool(std::shared_ptr<EditorShared> shared);
std::unique_ptr<EDITOR_PROJECT::IEditorPanel> MakeTerrainPanel(std::shared_ptr<EditorShared> shared);

// 操作の登録。handlers は登録の間生きている必要がある（呼び手が持つ）。
void RegisterTerrainOperations(EDITOR_PROJECT::IEditorOperationRegistrar& registrar,
                               const std::shared_ptr<EditorShared>& shared,
                               std::vector<std::unique_ptr<EDITOR_PROJECT::IEditorOperationHandler>>& handlers);

} // namespace HIKARI::TERRAINKIT
