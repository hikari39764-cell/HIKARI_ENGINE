#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <vector>

#include "Render3D/Terrain/HIKARI_TerrainGpuFrameBuffer.h"
#include "Render3D/Water/HIKARI_WaterSurfaceGpuData.h"

namespace HIKARI::RENDER3D::WATER {

constexpr std::size_t kDefaultWaterSurfaceGpuDataCapacity = 8193u;

struct WaterSurfaceFrameBufferTraits {
    static constexpr std::size_t kRecordCapacity = kDefaultWaterSurfaceGpuDataCapacity;
    static constexpr std::size_t kHeightCapacity = 1u;

    WaterSurfaceGpuData MakeSentinel(std::size_t count) const noexcept {
        return BuildWaterSurfaceGpuDataSentinel(static_cast<uint32_t>(count));
    }
};

class WaterSurfaceGpuFrameBuffer final
    : public RENDER3D::TERRAIN::SurfaceGpuFrameBuffer<WaterSurfaceGpuData, WaterSurfaceFrameBufferTraits> {
    using Base = RENDER3D::TERRAIN::SurfaceGpuFrameBuffer<WaterSurfaceGpuData, WaterSurfaceFrameBufferTraits>;

public:
    bool Initialize(ID3D12Device* device) {
        const bool initialized = Base::Initialize(device);
        requiresOceanSpectrum_ = false;
        return initialized;
    }

    bool UploadFrame(uint32_t frameIndex, ID3D12GraphicsCommandList* commandList,
                     const std::vector<WaterSurfaceGpuData>* records) {
        const bool resident = Base::UploadFrame(frameIndex, commandList, records);
        // The base upload publishes a neutral sentinel on rejection. Scan
        // only an accepted source payload so a failed/empty slot cannot
        // retain an earlier ocean-spectrum request.
        requiresOceanSpectrum_ = resident && records != nullptr && RequiresOceanSpectrumInRecords(*records);
        return resident;
    }

    bool RequiresOceanSpectrum() const noexcept { return requiresOceanSpectrum_; }

private:
    static bool RequiresOceanSpectrumInRecords(const std::vector<WaterSurfaceGpuData>& records) noexcept {
        constexpr uint32_t kEnabledFlag = static_cast<uint32_t>(WaterSurfaceGpuFlags::Enabled);
        constexpr float kModelTolerance = 0.25f;
        constexpr float kStrengthEpsilon = 1.0e-4f;
        for (const WaterSurfaceGpuData& record : records) {
            if (record.schemaVersion != kWaterSurfaceGpuSchemaVersion || (record.flags & kEnabledFlag) == 0u ||
                !std::isfinite(record.surfaceParams.z) || std::abs(record.surfaceParams.z - 1.0f) > kModelTolerance) {
                continue;
            }
            if ((std::isfinite(record.microWaveParams.z) && record.microWaveParams.z > kStrengthEpsilon) ||
                (std::isfinite(record.microWaveParams.w) && record.microWaveParams.w > kStrengthEpsilon)) {
                return true;
            }
        }
        return false;
    }

    bool requiresOceanSpectrum_ = false;
};

} // namespace HIKARI::RENDER3D::WATER
