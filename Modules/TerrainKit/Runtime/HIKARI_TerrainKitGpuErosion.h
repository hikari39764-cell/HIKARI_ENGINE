#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "HIKARI/Project/RenderSurface.h"
#include "TerrainKit/Data/HIKARI_TerrainDataErosion.h"

// 侵蝕を GPU で数フレームに分けて走らせる。場を送り、反復し、読み戻すと完了の手続きを呼ぶ。
// 呼び出しは全て描画の所有スレッド（PrepareFrame）。CPU の参照実装と同じ順番（熱力 → 水力）。
namespace HIKARI::TERRAINKIT {

struct GpuErosionResult {
    bool ok = false;
    std::string error{};
    std::vector<float> heights{};
    TERRAIN::DATA::ErosionMaps maps{};
    double seconds = 0.0;
};

class GpuErosionJob {
public:
    using Completion = std::function<void(GpuErosionResult)>;

    // 動いている間は false。heights は行優先、x が内側。
    bool Start(uint32_t width, uint32_t height, float spacingMeters, std::vector<float> heights,
               const TERRAIN::DATA::ErosionRequest& request, Completion completion);
    bool Running() const noexcept { return stage_ != Stage::Idle; }
    float Progress() const noexcept;
    // 一フレーム分進める。
    void Update(IRenderSurfaceResources& resources);
    // 機器の作り直しなどで資源が消えた時。番号を忘れて失敗で終える。
    void OnRenderResourcesLost();

private:
    enum class Stage : uint8_t { Idle, Create, Thermal, Hydraulic, Finish, Read };
    enum Program : uint32_t {
        ProgramFlux = 0u,
        ProgramWater,
        ProgramErode,
        ProgramTransport,
        ProgramSettle,
        ProgramClearLanes,
        ProgramThermalFlow,
        ProgramThermalApply,
        ProgramCount,
    };

    bool EnsurePrograms(IRenderSurfaceResources& resources);
    bool CreateBuffers(IRenderSurfaceResources& resources);
    void RunPass(IRenderSurfaceResources& resources, Program program);
    void Finish(IRenderSurfaceResources& resources, GpuErosionResult result);
    void ReleaseBuffers(IRenderSurfaceResources& resources);

    Stage stage_ = Stage::Idle;
    uint32_t width_ = 0u;
    uint32_t height_ = 0u;
    float spacing_ = 1.0f;
    std::vector<float> heights_{};
    TERRAIN::DATA::ErosionRequest request_{};
    Completion completion_{};
    uint32_t iteration_ = 0u;
    std::array<RenderSurfaceResourceId, ProgramCount> programs_{};
    bool programsFailed_ = false;
    std::array<RenderSurfaceResourceId, 2> state_{};
    uint32_t stateRead_ = 0u; // state_ のどちらが最新か
    RenderSurfaceResourceId lanesA_ = 0u;
    RenderSurfaceResourceId lanesB_ = 0u;
    RenderSurfaceResourceId maps_ = 0u;
    std::chrono::steady_clock::time_point started_{};
};

} // namespace HIKARI::TERRAINKIT
