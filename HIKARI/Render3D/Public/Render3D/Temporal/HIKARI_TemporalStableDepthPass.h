#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D::TEMPORAL {

// 後処理の線（漫画の輪郭・折れ目など）が読む深度を、出力の解像度で時間方向に均して作る。ずらした描画の深度を
// そのまま読むと、輪郭の位置が毎フレーム標本と一緒に跳ねて線が震える。戻り値は書いた深度（R32_FLOAT、
// 0 = 近い面、1 = 遠い面と空。後処理が読める状態）。作れない時は nullptr で、後処理は元の深度を読む。
// outSrv には共有の記述子ヒープ上の SRV を返す（RenderTarget2D 自身の SRV は別のヒープにある）。
RenderTarget2D* ExecuteTemporalStableDepthPass(const GFX::Context& graphics, const TemporalInputs& inputs,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* outSrv = nullptr);
void ShutdownTemporalStableDepthPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
