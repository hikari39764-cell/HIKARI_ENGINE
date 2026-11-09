#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace HIKARI {

// モジュールが自分の幾何を場面の深度・面・前向き不透明と太陽の影へ描くための接口。
// 頂点は持たず、頂点番号と実例データから頂点着色器で位置を作る（地形の瓦片のような格子向け）。
// 資源の実体、PSO、root の束縛は引擎が持ち、モジュールは番号と内容だけを渡す。

using RenderSurfaceResourceId = uint32_t;
inline constexpr uint32_t kRenderSurfaceTextureSlotCount = 8u;
inline constexpr uint32_t kRenderSurfaceMaximumConstantBytes = 128u;

enum class RenderSurfaceTextureFormat : uint32_t {
    R16Unorm = 0u,
    R16G16Snorm,
    R32Uint,
    R8G8B8A8Unorm,
    Bc7Unorm,
    Bc7UnormSrgb,
    R32G32B32A32Float,
};

struct RenderSurfaceTextureArrayDesc {
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t layerCount = 1u;
    uint32_t mipCount = 1u;
    RenderSurfaceTextureFormat format = RenderSurfaceTextureFormat::R16Unorm;
};

// BC 形式の width / height は画素数のまま、rowPitchBytes は 4 画素の行あたりのバイト数。
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

inline constexpr uint32_t kRenderSurfaceComputeSlotCount = 4u;
inline constexpr uint32_t kRenderSurfaceMaximumComputeConstantBytes = 256u;

// モジュールの compute を一回走らせる。readBuffers は t0.. の StructuredBuffer、writeBuffers は u0.. の
// RWStructuredBuffer、constants は b0。同じ緩衝を読みと書きの両方に指定しないこと。次の Dispatch の前に
// 書いた緩衝には自動で UAV の barrier が入る。
struct RenderSurfaceDispatch {
    RenderSurfaceResourceId program = 0u;
    RenderSurfaceResourceId readBuffers[kRenderSurfaceComputeSlotCount]{};
    RenderSurfaceResourceId writeBuffers[kRenderSurfaceComputeSlotCount]{};
    const void* constants = nullptr;
    uint32_t constantBytes = 0u;
    uint32_t groupsX = 1u;
    uint32_t groupsY = 1u;
    uint32_t groupsZ = 1u;
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
    // compute。hlslSource は HLSL の本文（#include は使えない）、entry は入口の名前。失敗は 0。
    virtual RenderSurfaceResourceId CreateComputeProgram(std::string_view hlslSource, std::string_view entry) = 0;
    // GPU だけが持つ緩衝。initialData が無ければ中身は不定。
    virtual RenderSurfaceResourceId CreateBuffer(uint64_t bytes, const void* initialData) = 0;
    virtual bool Dispatch(const RenderSurfaceDispatch& dispatch) = 0;
    // 今のフレームの末尾時点の緩衝の写しを取る依頼。GPU が終えると IsBufferReadReady が true になり、
    // ReadBuffer で受け取れる（受け取ると写しは消える）。
    virtual bool RequestBufferRead(RenderSurfaceResourceId buffer) = 0;
    virtual bool IsBufferReadReady(RenderSurfaceResourceId buffer) = 0;
    virtual bool ReadBuffer(RenderSurfaceResourceId buffer, void* out, uint64_t bytes) = 0;
    virtual void Release(RenderSurfaceResourceId resource) = 0;
};

enum class RenderSurfacePass : uint32_t {
    Depth = 0u,
    GeometrySurface,
    Forward,
    Shadow,
    // 不透明の後に場面の深度図を読みながら場面色へ混ぜる（水面など）。深度へは書かない。色は事前乗算のアルファで出す。
    DepthAware,
};

// 行列は引擎の列優先 m[col][row]。cameraPosition は影の視点でも主相机の位置（遠近分級を主視点に揃えるため）。
struct RenderSurfaceView {
    RenderSurfacePass pass = RenderSurfacePass::Forward;
    uint32_t shadowCascade = 0u;
    float viewProjection[16]{};
    float cameraPosition[3]{};
};

// instances と constants は Draw の中で写される。programId は引擎の内蔵着色器の番号で、
// 使う pass に応じて入口 SurfaceVS / SurfaceGeometryPS / SurfaceGeometryFeaturePS / SurfaceForwardPS /
// SurfaceDepthAwarePS を持つこと。
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
    // DepthAware の pass で描くものがあるか（無ければ引擎はその段を省ける）。
    virtual bool HasDepthAwareWork() const { return false; }
    // 機器の作り直しなどで資源が消えた時。番号を忘れて次の PrepareFrame で作り直す。
    virtual void OnRenderResourcesLost() {}
};

} // namespace HIKARI
