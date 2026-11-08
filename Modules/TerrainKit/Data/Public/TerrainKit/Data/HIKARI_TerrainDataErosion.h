#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"

// 侵蝕の CPU 参照実装。GPU 版（雕刻与生成の段）はこれと同じ式で、結果を突き合わせる。
// 水力：Mei, Decaudin, Hu 2007「Fast Hydraulic Erosion Simulation and Visualization on GPU」の仮想パイプ模型
//       （水位 → 管の流量 → 水深 → 流速 → 運搬能力で侵蝕/堆積 → 堆積物の半ラグランジュ移流 → 蒸発）。
// 熱力：安息角を超えた高低差を 8 近傍へ配る（Musgrave 1989 の熱風化、総量は保存）。
// どちらも Jacobi 型（前の状態だけを読む）なので、行を並列に処理しても結果は決定的。
namespace HIKARI::TERRAIN::DATA {

struct HydraulicErosionSettings {
    uint32_t iterations = 240u;
    float timeStep = 0.02f;           // s
    float rainRate = 0.012f;          // m / s（毎反復の降雨）
    float sedimentCapacity = 1.0f;    // Kc
    float dissolveRate = 0.5f;        // Ks（1/s）
    float depositRate = 1.0f;         // Kd（1/s）
    float evaporationRate = 0.5f;     // Ke（1/s）
    float minimumTilt = 0.05f;        // 平地でも少し運ぶ下限（sin）
    float maximumErosionDepth = 4.0f; // 1 標本が削られる総量の上限（m）
    bool operator==(const HydraulicErosionSettings&) const = default;
};

inline constexpr auto kHydraulicErosionFields = [](auto&& field) {
    using S = HydraulicErosionSettings;
    using namespace JSONFIELDS;
    field("iterations", &S::iterations, Range{1.0, 20000.0}, Label{"Iterations"});
    field("timeStep", &S::timeStep, Range{0.001, 0.1}, Label{"Time Step"}, Unit{"s"});
    field("rainRate", &S::rainRate, Range{0.0, 1.0}, Label{"Rain"}, Unit{"m/s"});
    field("sedimentCapacity", &S::sedimentCapacity, Range{0.0, 10.0}, Label{"Sediment Capacity"});
    field("dissolveRate", &S::dissolveRate, Range{0.0, 10.0}, Label{"Dissolve Rate"});
    field("depositRate", &S::depositRate, Range{0.0, 10.0}, Label{"Deposit Rate"});
    field("evaporationRate", &S::evaporationRate, Range{0.0, 10.0}, Label{"Evaporation"});
    field("minimumTilt", &S::minimumTilt, Range{0.0, 1.0}, Label{"Minimum Tilt"});
    field("maximumErosionDepth", &S::maximumErosionDepth, Range{0.0, 1000.0}, Label{"Max Erosion Depth"}, Unit{"m"});
};

struct ThermalErosionSettings {
    uint32_t iterations = 60u;
    float talusAngleDegrees = 34.0f;
    float rate = 0.5f; // 1 反復で超過分の何割を動かすか
    bool operator==(const ThermalErosionSettings&) const = default;
};

inline constexpr auto kThermalErosionFields = [](auto&& field) {
    using S = ThermalErosionSettings;
    using namespace JSONFIELDS;
    field("iterations", &S::iterations, Range{1.0, 20000.0}, Label{"Iterations"});
    field("talusAngleDegrees", &S::talusAngleDegrees, Range{1.0, 89.0}, Label{"Talus Angle"}, Unit{"deg"});
    field("rate", &S::rate, Slider(0.0, 1.0), Label{"Rate"});
};

// 侵蝕の入出力の場（行優先、x が内側）。
struct ErosionField {
    uint32_t width = 0u;
    uint32_t height = 0u;
    float spacingMeters = 1.0f;
    std::vector<float> heights{};
    bool Valid() const noexcept;
};

// 出力図（場と同じ寸法）。flow は流量の時間積分、deposition は堆積の総量、wear は侵蝕と崩落の総量（m）。
struct ErosionMaps {
    std::vector<float> flow{};
    std::vector<float> deposition{};
    std::vector<float> wear{};
};

bool RunHydraulicErosion(ErosionField& field, const HydraulicErosionSettings& settings, ErosionMaps* maps,
                         std::string& error);
bool RunThermalErosion(ErosionField& field, const ThermalErosionSettings& settings, ErosionMaps* maps,
                       std::string& error);

struct ErosionRequest {
    bool hydraulic = true;
    bool thermal = true;
    HydraulicErosionSettings hydraulicSettings{};
    ThermalErosionSettings thermalSettings{};
    // 矩形の外周で結果を 0 → 1 へ混ぜる幅（標本）。周りの地形との段差を作らない。
    uint32_t featherSamples = 16u;
    bool operator==(const ErosionRequest&) const = default;
};

inline constexpr auto kErosionRequestFields = [](auto&& field) {
    using S = ErosionRequest;
    using namespace JSONFIELDS;
    field("hydraulic", &S::hydraulic, Label{"Hydraulic"});
    field("thermal", &S::thermal, Label{"Thermal"});
    field("hydraulicSettings", &S::hydraulicSettings, Nested{kHydraulicErosionFields}, Group{"Hydraulic"});
    field("thermalSettings", &S::thermalSettings, Nested{kThermalErosionFields}, Group{"Thermal"});
    field("featherSamples", &S::featherSamples, Range{0.0, 4096.0}, Label{"Feather"});
};

// 侵蝕の入力：矩形の合成後の高さ（手彫り層 layerId まで）と、差分を層へ戻すための不透明度。
struct ErosionInput {
    ErosionField field{};
    std::vector<float> alpha{};
};

bool PrepareErosionInput(const TerrainData& data, uint32_t layerId, const SampleRect& rect, ErosionInput& out,
                         std::string& error);
// eroded と input の差を手彫り層へ足す（CPU でも GPU でも結果の入れ方は同じ）。
bool CommitErosionResult(TerrainData& data, uint32_t layerId, const SampleRect& rect, const ErosionRequest& request,
                         const ErosionInput& input, const std::vector<float>& eroded, TerrainEditRecorder* recorder,
                         std::string& error);

// 矩形の合成後の高さ（手彫り層 layerId まで）を侵蝕し、変化量を手彫り層へ足す（加算の層だけ）。
// 底図を替えても侵蝕の結果は差分として残る。maps があれば矩形の出力図を返す。
bool ApplyErosionToLayer(TerrainData& data, uint32_t layerId, const SampleRect& rect, const ErosionRequest& request,
                         TerrainEditRecorder* recorder, ErosionMaps* maps, std::string& error);

} // namespace HIKARI::TERRAIN::DATA
