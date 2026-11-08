#include "HIKARI_TerrainKitGpuErosion.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace TERRAIN::DATA;

constexpr uint32_t kGroupSize = 32u;
// 一フレームに進める量（標本数 × 反復）。編集器が動き続ける範囲に抑える。
constexpr uint64_t kCellIterationsPerFrame = 8000000u;
constexpr char kShaderFile[] = "Shaders/TerrainKit_ErosionCS.hlsl";
constexpr const char* kProgramEntries[] = {"CSFlux",   "CSWater",      "CSErode",       "CSTransport",
                                           "CSSettle", "CSClearLanes", "CSThermalFlow", "CSThermalApply"};

struct Cell {
    float ground = 0.0f;
    float water = 0.0f;
    float sediment = 0.0f;
    float eroded = 0.0f;
};

struct ErosionConstants {
    uint32_t width = 0u;
    uint32_t height = 0u;
    float spacing = 1.0f;
    float timeStep = 0.02f;
    float rainStep = 0.0f;
    float capacity = 1.0f;
    float dissolve = 0.5f;
    float deposit = 1.0f;
    float keepWater = 1.0f;
    float minimumTilt = 0.05f;
    float maximumErosionDepth = 4.0f;
    float thermalRate = 0.5f;
    float talusAxis = 1.0f;
    float talusDiagonal = 1.0f;
    float reserved0 = 0.0f;
    float reserved1 = 0.0f;
};

std::string ReadShaderSource() {
    std::ifstream file(TerrainModuleDirectory() / kShaderFile, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

ErosionConstants MakeConstants(uint32_t width, uint32_t height, float spacing, const ErosionRequest& request) {
    const HydraulicErosionSettings& water = request.hydraulicSettings;
    const ThermalErosionSettings& thermal = request.thermalSettings;
    const float talus = std::tan(thermal.talusAngleDegrees * 3.14159265358979f / 180.0f);
    ErosionConstants constants{};
    constants.width = width;
    constants.height = height;
    constants.spacing = spacing;
    constants.timeStep = water.timeStep;
    constants.rainStep = water.timeStep * water.rainRate;
    constants.capacity = water.sedimentCapacity;
    constants.dissolve = water.dissolveRate;
    constants.deposit = water.depositRate;
    constants.keepWater = (std::max)(0.0f, 1.0f - water.evaporationRate * water.timeStep);
    constants.minimumTilt = water.minimumTilt;
    constants.maximumErosionDepth = water.maximumErosionDepth;
    constants.thermalRate = thermal.rate;
    constants.talusAxis = talus * spacing;
    constants.talusDiagonal = talus * spacing * 1.41421356f;
    return constants;
}

} // namespace

bool GpuErosionJob::Start(uint32_t width, uint32_t height, float spacingMeters, std::vector<float> heights,
                          const ErosionRequest& request, Completion completion) {
    if (Running() || width < 3u || height < 3u || heights.size() != static_cast<size_t>(width) * height) {
        return false;
    }
    width_ = width;
    height_ = height;
    spacing_ = spacingMeters;
    heights_ = std::move(heights);
    request_ = JSONFIELDS::Sanitize(request, kErosionRequestFields);
    completion_ = std::move(completion);
    iteration_ = 0u;
    stage_ = Stage::Create;
    started_ = std::chrono::steady_clock::now();
    return true;
}

float GpuErosionJob::Progress() const noexcept {
    const uint32_t thermal = request_.thermal ? request_.thermalSettings.iterations : 0u;
    const uint32_t hydraulic = request_.hydraulic ? request_.hydraulicSettings.iterations : 0u;
    const float total = static_cast<float>((std::max)(thermal + hydraulic, 1u));
    switch (stage_) {
    case Stage::Thermal: return static_cast<float>(iteration_) / total;
    case Stage::Hydraulic: return static_cast<float>(thermal + iteration_) / total;
    case Stage::Finish:
    case Stage::Read: return 1.0f;
    default: return 0.0f;
    }
}

bool GpuErosionJob::EnsurePrograms(IRenderSurfaceResources& resources) {
    if (programs_[0] != 0u) {
        return true;
    }
    if (programsFailed_) {
        return false;
    }
    const std::string source = ReadShaderSource();
    programsFailed_ = source.empty();
    for (uint32_t program = 0u; program < ProgramCount && !programsFailed_; ++program) {
        programs_[program] = resources.CreateComputeProgram(source, kProgramEntries[program]);
        programsFailed_ = programs_[program] == 0u;
    }
    return !programsFailed_;
}

bool GpuErosionJob::CreateBuffers(IRenderSurfaceResources& resources) {
    const size_t count = static_cast<size_t>(width_) * height_;
    std::vector<Cell> cells(count);
    for (size_t index = 0u; index < count; ++index) {
        cells[index].ground = heights_[index];
    }
    const uint64_t cellBytes = count * sizeof(Cell);
    const uint64_t laneBytes = count * sizeof(float) * 4u;
    state_[0] = resources.CreateBuffer(cellBytes, cells.data());
    state_[1] = resources.CreateBuffer(cellBytes, nullptr);
    const std::vector<float> zeros(count * 4u, 0.0f);
    lanesA_ = resources.CreateBuffer(laneBytes, zeros.data());
    lanesB_ = resources.CreateBuffer(laneBytes, zeros.data());
    maps_ = resources.CreateBuffer(laneBytes, zeros.data());
    stateRead_ = 0u;
    return state_[0] != 0u && state_[1] != 0u && lanesA_ != 0u && lanesB_ != 0u && maps_ != 0u;
}

void GpuErosionJob::RunPass(IRenderSurfaceResources& resources, Program program) {
    const ErosionConstants constants = MakeConstants(width_, height_, spacing_, request_);
    RenderSurfaceDispatch dispatch{};
    dispatch.program = programs_[program];
    dispatch.readBuffers[0] = state_[stateRead_];
    dispatch.writeBuffers[0] = state_[1u - stateRead_];
    dispatch.writeBuffers[1] = lanesA_;
    dispatch.writeBuffers[2] = lanesB_;
    dispatch.writeBuffers[3] = maps_;
    dispatch.constants = &constants;
    dispatch.constantBytes = sizeof(constants);
    dispatch.groupsX = (width_ + kGroupSize - 1u) / kGroupSize;
    dispatch.groupsY = (height_ + kGroupSize - 1u) / kGroupSize;
    (void)resources.Dispatch(dispatch);
    stateRead_ = 1u - stateRead_;
}

void GpuErosionJob::Update(IRenderSurfaceResources& resources) {
    if (stage_ == Stage::Idle) {
        return;
    }
    if (stage_ == Stage::Create) {
        if (!EnsurePrograms(resources) || !CreateBuffers(resources)) {
            GpuErosionResult failed{};
            failed.error = "The erosion compute shader or its buffers could not be created.";
            Finish(resources, std::move(failed));
            return;
        }
        heights_ = {};
        iteration_ = 0u;
        stage_ = request_.thermal ? Stage::Thermal : Stage::Hydraulic;
        if (stage_ == Stage::Hydraulic) {
            RunPass(resources, ProgramClearLanes);
        }
    }
    const uint64_t cells = static_cast<uint64_t>(width_) * height_;
    const uint32_t perFrame = static_cast<uint32_t>((std::max<uint64_t>)(kCellIterationsPerFrame / cells, 1u));
    if (stage_ == Stage::Thermal) {
        const uint32_t total = request_.thermalSettings.iterations;
        for (uint32_t step = 0u; step < perFrame && iteration_ < total; ++step, ++iteration_) {
            RunPass(resources, ProgramThermalFlow);
            RunPass(resources, ProgramThermalApply);
        }
        if (iteration_ >= total) {
            iteration_ = 0u;
            stage_ = request_.hydraulic ? Stage::Hydraulic : Stage::Finish;
            if (stage_ == Stage::Hydraulic) {
                RunPass(resources, ProgramClearLanes);
            }
        }
        return;
    }
    if (stage_ == Stage::Hydraulic) {
        const uint32_t total = request_.hydraulicSettings.iterations;
        for (uint32_t step = 0u; step < perFrame && iteration_ < total; ++step, ++iteration_) {
            RunPass(resources, ProgramFlux);
            RunPass(resources, ProgramWater);
            RunPass(resources, ProgramErode);
            RunPass(resources, ProgramTransport);
        }
        if (iteration_ >= total) {
            RunPass(resources, ProgramSettle);
            stage_ = Stage::Finish;
        }
        return;
    }
    if (stage_ == Stage::Finish) {
        (void)resources.RequestBufferRead(state_[stateRead_]);
        (void)resources.RequestBufferRead(maps_);
        stage_ = Stage::Read;
        return;
    }
    // Stage::Read：GPU が書き終えて写しが読めるまで待つ。
    if (!resources.IsBufferReadReady(state_[stateRead_]) || !resources.IsBufferReadReady(maps_)) {
        return;
    }
    const size_t count = static_cast<size_t>(width_) * height_;
    std::vector<Cell> finalCells(count);
    std::vector<float> maps(count * 4u);
    GpuErosionResult result{};
    result.ok = resources.ReadBuffer(state_[stateRead_], finalCells.data(), count * sizeof(Cell)) &&
                resources.ReadBuffer(maps_, maps.data(), count * sizeof(float) * 4u);
    if (!result.ok) {
        result.error = "The erosion result could not be read back.";
    } else {
        result.heights.resize(count);
        result.maps.flow.resize(count);
        result.maps.deposition.resize(count);
        result.maps.wear.resize(count);
        for (size_t index = 0u; index < count; ++index) {
            result.heights[index] = finalCells[index].ground;
            result.maps.flow[index] = maps[index * 4u];
            result.maps.deposition[index] = maps[index * 4u + 1u];
            result.maps.wear[index] = maps[index * 4u + 2u];
        }
    }
    Finish(resources, std::move(result));
}

void GpuErosionJob::ReleaseBuffers(IRenderSurfaceResources& resources) {
    for (RenderSurfaceResourceId* buffer : {&state_[0], &state_[1], &lanesA_, &lanesB_, &maps_}) {
        if (*buffer != 0u) {
            resources.Release(*buffer);
            *buffer = 0u;
        }
    }
}

void GpuErosionJob::Finish(IRenderSurfaceResources& resources, GpuErosionResult result) {
    ReleaseBuffers(resources);
    heights_ = {};
    stage_ = Stage::Idle;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    Completion completion = std::move(completion_);
    completion_ = {};
    if (completion) {
        completion(std::move(result));
    }
}

void GpuErosionJob::OnRenderResourcesLost() {
    programs_ = {};
    programsFailed_ = false;
    state_ = {};
    lanesA_ = 0u;
    lanesB_ = 0u;
    maps_ = 0u;
    if (Running()) {
        GpuErosionResult failed{};
        failed.error = "The render device was reset during erosion.";
        stage_ = Stage::Idle;
        Completion completion = std::move(completion_);
        completion_ = {};
        if (completion) {
            completion(std::move(failed));
        }
    }
}

} // namespace HIKARI::TERRAINKIT
