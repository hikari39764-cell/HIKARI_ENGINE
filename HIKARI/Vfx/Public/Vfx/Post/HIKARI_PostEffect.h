#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <memory>
#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include "RenderContracts/Shader/HIKARI_ShaderAssetResolver.h"
#include "Vfx/Post/HIKARI_PostTypes.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
class AssetRegistry;
namespace POST {

class QuadDrawer;

class PostEffect {
public:
    PostEffect();
    ~PostEffect();
    static void UpdateContext(const GFX::Context& ctx);

    bool LoadPixelShader(const PostEffectPass& pass, const AssetRegistry* registry = nullptr,
                         bool surfaceFeatureMask = false);

    void ApplyCommonParams(const CommonParams& p);

    void SetTime(float t);

    bool BindAndDraw(QuadDrawer& drawer, bool additive = false);
    // 同じ定数で、矩形ごとに切り抜きを絞って描く（一フレームの定数の枠は一つだけ使う）。視口は呼び手のまま。
    // 描いた後の切り抜きは最後の矩形のままなので、呼び手が戻す。rects が空なら何もしない。
    bool BindAndDrawScissored(QuadDrawer& drawer, const std::vector<D3D12_RECT>& rects, bool additive = false);
    static constexpr uint32_t DrawCapacityPerFrame() noexcept { return kConstantDrawCapacityPerFrame; }

private:
    // 定数を一フレームの枠へ書き、描き方と定数を四角の描き手へ結ぶ。
    bool BindConstants(QuadDrawer& drawer, bool additive);
    bool CreateConstantBuffer();
    void ReleaseConstantBuffer() noexcept;

private:
    static constexpr uint32_t kConstantDrawCapacityPerFrame = 32u;

    static GFX::Context context_;
    ASSETS::SHADER::ShaderAssetEntryLease psLease_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    GFX::Context constantBufferContext_{};
    uint8_t* mappedPtr_ = nullptr;
    uint32_t constantStride_ = 0u;
    std::array<uint64_t, GFX::kFrameResourceCount> constantFrameSerials_{};
    std::array<uint32_t, GFX::kFrameResourceCount> constantDrawCursors_{};

    CommonParams params_{};
};

bool BuildPostEffectPasses(const PostEffectDefinition& definition, const AssetRegistry* registry,
                           std::vector<std::unique_ptr<PostEffect>>& passes);

} // namespace POST
} // namespace HIKARI
