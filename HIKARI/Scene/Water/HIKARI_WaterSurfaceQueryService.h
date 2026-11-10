#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Terrain/HIKARI_TerrainSnapshotService.h"
#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"

namespace HIKARI::WATER {

enum class WaterSurfaceSampleField : uint32_t {
    None = 0u,
    Surface = 1u << 0u,
    Velocity = 1u << 1u,
    ColumnDepth = 1u << 2u,
};

constexpr WaterSurfaceSampleField operator|(WaterSurfaceSampleField left, WaterSurfaceSampleField right) noexcept {
    return static_cast<WaterSurfaceSampleField>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr bool HasSampleField(WaterSurfaceSampleField fields, WaterSurfaceSampleField expected) noexcept {
    return (static_cast<uint32_t>(fields) & static_cast<uint32_t>(expected)) != 0u;
}

struct WaterSurfaceQuery {
    MATH::Vec3 positionWS{};
    float timeSeconds = 0.0f;
    RuntimeObjectHandle preferredSurface{};
    WaterSurfaceSampleField requestedFields = WaterSurfaceSampleField::Surface | WaterSurfaceSampleField::Velocity;
    float maximumColumnDepth = 1000.0f;
};

struct WaterSurfaceSample {
    bool valid = false;
    RuntimeObjectHandle surfaceObject{};
    MATH::Vec3 surfacePositionWS{};
    MATH::Vec3 normalWS{0.0f, 1.0f, 0.0f};
    MATH::Vec3 velocityWS{};
    float signedDistanceToSurface = 0.0f;
    float submergenceDepth = 0.0f;

    // bathymetry provider が有効な距離を返した場合だけ
    // ColumnDepth bit と共に値を有効化する。
    float waterColumnDepth = 0.0f;
    WaterSurfaceSampleField validFields = WaterSurfaceSampleField::None;
    uint64_t snapshotRevision = 0u;
    float evaluatedTimeSeconds = 0.0f;
};

struct WaterBathymetryRay {
    RuntimeObjectHandle surfaceObject{};
    MATH::Vec3 originWS{};
    MATH::Vec3 directionWS{0.0f, -1.0f, 0.0f};
    float maximumDistance = 1000.0f;
};

// Water は底面の実装を所有しない。provider はこの細い read-only port を
// 実装し、surfaceObject を自己衝突の除外に利用できる。
class IWaterBathymetryProvider {
public:
    virtual ~IWaterBathymetryProvider() = default;

    virtual bool RaycastBathymetry(const WaterBathymetryRay& ray, float& outDistance) const noexcept = 0;
};

// WaterSurfaceSystem が World から値スナップショットを構築する。
// query は World や Component を参照せず、この値だけを読む。
struct WaterSurfaceSnapshotEntry {
    RuntimeObjectHandle object{};
    MATH::Mat4 world = MATH::Mat4::Identity();
    MATH::Mat4 inverseWorld = MATH::Mat4::Identity();
    MATH::Mat4 normalMatrix = MATH::Mat4::Identity();
    WaterSurfaceSettings settings{};
    // FiniteAuthored uses these as hard bounds. CameraCenteredOcean is an
    // infinite query domain and keeps them only as authored mesh metadata.
    MATH::Vec2 halfExtents{};
    float maximumHorizontalDisplacement = 0.0f;
};

class WaterSurfaceQueryService final {
public:
    // binding は atomic に更新され、次の query から反映される。
    // provider は借用参照なので、旧 provider の破棄前に進行中 query を止める。
    void SetBathymetryProvider(const IWaterBathymetryProvider* provider) noexcept;

    bool SampleNearest(const WaterSurfaceQuery& query, WaterSurfaceSample& outSample) const noexcept;
    // 相机を追う海の水面の高さ（物体の原点の高さ）と物体。無ければ false。
    bool FindOcean(float& outLevel, RuntimeObjectHandle& outObject) const noexcept;

    uint64_t GetSnapshotRevision() const noexcept;
    std::size_t GetSurfaceCount() const noexcept;
    void PublishSnapshot(std::vector<WaterSurfaceSnapshotEntry> surfaces);
    void Clear();

private:
    TERRAIN::TerrainSnapshotService<WaterSurfaceSnapshotEntry> snapshotStore_{};
    std::atomic<const IWaterBathymetryProvider*> bathymetryProvider_{nullptr};
};

} // namespace HIKARI::WATER
