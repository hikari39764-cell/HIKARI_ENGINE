#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace HIKARI {

// モジュールが自分の幾何を場面の深度・面・前向き不透明と太陽の影へ描くための接口。
// 頂点は持たず、頂点番号と実例データから頂点着色器で位置を作る（地形の瓦片のような格子向け）。
// 資源の実体、PSO、root の束縛は引擎が持ち、モジュールは番号と内容だけを渡す。

using RenderSurfaceResourceId = uint32_t;
inline constexpr uint32_t kRenderSurfaceTextureSlotCount = 4u;
inline constexpr uint32_t kRenderSurfaceMaximumConstantBytes = 128u;

enum class RenderSurfaceTextureFormat : uint32_t {
    R16Unorm = 0u,
    R16G16Snorm,
    R32Uint,
    R8G8B8A8Unorm,
};

struct RenderSurfaceTextureArrayDesc {
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t layerCount = 1u;
    uint32_t mipCount = 1u;
    RenderSurfaceTextureFormat format = RenderSurfaceTextureFormat::R16Unorm;
};

struct RenderSurfaceTextureUpload {
    uint32_t layer = 0u;
    uint32_t mip = 0u;
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t rowPitchBytes = 0u;
    const void* pixels = nullptr;
};

// 光線追跡の場面でこの幾何の代わりになる不透明の粗い網（太陽の影・GI・反射の光線が当たる）。座標は世界座標で、
// 三角形は上から見て法線が +Y を向く順（裏から来た影の光線は通す）。区画ごとに色と上向きの法線を一つ持つ。
struct RenderSurfaceRayProxyRegion {
    uint32_t firstIndex = 0u;
    uint32_t indexCount = 0u;
    float albedo[3]{}; // 線形
    float normal[3]{0.0f, 1.0f, 0.0f};
};

struct RenderSurfaceRayProxy {
    std::span<const float> positions{}; // xyz
    std::span<const uint16_t> indices{};
    std::span<const RenderSurfaceRayProxyRegion> regions{};
};

// PrepareFrame の間だけ有効。番号は Release か OnRenderResourcesLost まで使える。
class IRenderSurfaceResources {
public:
    virtual ~IRenderSurfaceResources() = default;
    // 失敗は 0。
    virtual RenderSurfaceResourceId CreateTextureArray(const RenderSurfaceTextureArrayDesc& desc) = 0;
    virtual bool UploadTexture(RenderSurfaceResourceId texture, const RenderSurfaceTextureUpload& upload) = 0;
    virtual RenderSurfaceResourceId CreateIndexBuffer(std::span<const uint16_t> indices) = 0;
    // 内容は写される。変える時は作り直して古い番号を Release する。
    virtual RenderSurfaceResourceId CreateRayTracingProxy(const RenderSurfaceRayProxy& proxy) = 0;
    virtual void Release(RenderSurfaceResourceId resource) = 0;
};

enum class RenderSurfacePass : uint32_t {
    Depth = 0u,
    GeometrySurface,
    Forward,
    Shadow,
};

// 行列は引擎の列優先 m[col][row]。cameraPosition は影の視点でも主相机の位置（遠近分級を主視点に揃えるため）。
struct RenderSurfaceView {
    RenderSurfacePass pass = RenderSurfacePass::Forward;
    uint32_t shadowCascade = 0u;
    float viewProjection[16]{};
    float cameraPosition[3]{};
};

// instances と constants は Draw の中で写される。programId は引擎の内蔵着色器の番号で、
// 入口 SurfaceVS / SurfaceGeometryPS / SurfaceGeometryFeaturePS / SurfaceForwardPS を持つこと。
struct RenderSurfaceDraw {
    const char* programId = nullptr;
    RenderSurfaceResourceId textures[kRenderSurfaceTextureSlotCount]{};
    RenderSurfaceResourceId indexBuffer = 0u;
    uint32_t indexCount = 0u;
    const void* instances = nullptr;
    uint32_t instanceStride = 0u;
    uint32_t instanceCount = 0u;
    const void* constants = nullptr;
    uint32_t constantBytes = 0u;
};

class IRenderSurfaceCommands {
public:
    virtual ~IRenderSurfaceCommands() = default;
    virtual bool Draw(const RenderSurfaceDraw& draw) = 0;
};

// World に登録して使う（WorldView::AddRenderSurfaceExtension）。呼び出しは全て描画の所有スレッド。
class IRenderSurfaceExtension {
public:
    virtual ~IRenderSurfaceExtension() = default;
    // 描画の前に一度。ここだけで資源を作り、内容を送れる。
    virtual void PrepareFrame(IRenderSurfaceResources& resources) = 0;
    // 視点ごと・pass ごとに呼ばれる。
    virtual void Record(const RenderSurfaceView& view, IRenderSurfaceCommands& commands) = 0;
    // 機器の作り直しなどで資源が消えた時。番号を忘れて次の PrepareFrame で作り直す。
    virtual void OnRenderResourcesLost() {}
};

} // namespace HIKARI
