#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI {
class RenderTarget2D;
namespace ASSETS::COLOR {
struct ColorLutAsset;
}
} // namespace HIKARI

namespace HIKARI::POST {

class QuadDrawer;

// 色調整の 3D LUT の GPU 側。設定・.cube・表示の gamma が変わった時だけ CPU で焼き直し、フレームの命令列で写す。
class ColorGradingLut {
public:
    void SetSource(const ColorGradingSettings& settings, std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> lut);
    // 焼き直しが要れば複写を記録し、引く LUT を返す。調整が恒等なら nullptr。
    ID3D12Resource* Prepare(const GFX::Context& context, float gamma);
    // 最後に Prepare した LUT（同じ環境を描く別のビューが借りる）。
    ID3D12Resource* GetCurrent() const noexcept { return active_ ? texture_.Get() : nullptr; }
    void Release(const GFX::Context& context);

private:
    bool Upload(const GFX::Context& context);

    ColorGradingSettings settings_{};
    std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> lut_{};
    std::vector<uint16_t> texels_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_{};
    ID3D12Device* device_ = nullptr;
    uint64_t deviceGeneration_ = 0u;
    float bakedGamma_ = 0.0f;
    bool dirty_ = true;
    bool active_ = false;
};

// トーンマッピングの入力（t0 = 場面の HDR、t1 = 色調整の LUT か空、t2 = 空）を描画へ束ね、記述子はフレームの
// 終わりに返す。LUT が無くても補助表を必ず束ね、shader が宣言する全レジスターを有効にしておく。
class ToneMappingInputs {
public:
    ToneMappingInputs(const GFX::Context& context, QuadDrawer& quad, RenderTarget2D& source,
                      ID3D12Resource* gradingLut);
    ~ToneMappingInputs();
    ToneMappingInputs(const ToneMappingInputs&) = delete;
    ToneMappingInputs& operator=(const ToneMappingInputs&) = delete;

    explicit operator bool() const noexcept { return views_.size() == 3u; }

private:
    GFX::Context context_{};
    std::vector<RENDER3D::RenderResourceView> views_{};
};

} // namespace HIKARI::POST
