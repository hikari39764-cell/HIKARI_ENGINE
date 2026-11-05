#pragma once

#include <array>
#include <cstdint>

namespace HIKARI::GFX::UPLOAD_TRAFFIC {

// CPU が upload 堆へ書いて GPU へ渡した量の所有者別の記帳。共有の upload 道具（DynamicUploadBuffer、
// FencedUploadBuffers、加速構造の instance）は書いた所で自動に記録し、upload 堆を自分で写像して書く所有者は Record を
// 呼ぶ。固定の大きさで常駐写像した小さな定数 buffer（空・SSAO・後処理などの pass 定数）は数えない。
// 毎フレーム Publish すると、登録表の upload.<所有者>.bytes、upload.sceneData.bytes、upload.total.bytes に出る。
enum class Kind : uint8_t {
    // 毎フレーム出るのが正しいもの（相机・pass の定数、その場で作る頂点、粒子）。
    PerFrame,
    // 場面の内容。止まった場面では 0 であるべき（常駐させて変わった所だけ書く）。基準はこれを守門する。
    SceneData,
};

// 所有者の表は HIKARI_UploadTraffic.cpp の kOwners に 1 つだけ。
enum class Owner : uint8_t {
    ViewConstants,
    PassConstants,
    Ui2D,
    TransientGeometry,
    Particles,
    Fluids,
    GpuScene,
    MassScene,
    SurfaceFeatures,
    WorldSprites,
    Materials,
    JointPalettes,
    LocalLights,
    RayTracingLights,
    RayTracingInstances,
    RayTracingHitRecords,
    DrawCommands,
    Geometry,
    Textures,
    LightProbes,
    ColorGrading,
    Water,
    Count
};
inline constexpr size_t kOwnerCount = static_cast<size_t>(Owner::Count);
using FrameBytes = std::array<uint64_t, kOwnerCount>;

// どのスレッドからでも呼べる。
void Record(Owner owner, uint64_t bytes) noexcept;
// 前回の Publish から記録した量を 1 フレーム分として出し、数え直す。主スレッドだけが呼ぶ。
void Publish(uint64_t frameIndex);
// 最後に Publish したフレームの所有者別の量（基準の集計用）。
const FrameBytes& LastFrame() noexcept;
const char* Name(Owner owner) noexcept;
Kind KindOf(Owner owner) noexcept;

} // namespace HIKARI::GFX::UPLOAD_TRAFFIC
