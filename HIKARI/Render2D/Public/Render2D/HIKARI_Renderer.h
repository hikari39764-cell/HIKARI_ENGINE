#pragma once
#include "Core/Math/HIKARI_Vector2.h"

namespace HIKARI::RENDERER {

// 2D コマンドのレイヤー。レイヤー順→Y ソート→投入順で描画する。
enum class RenderLayer { Background = 0, Shadow = 1, Entity = 2, VFX = 3, UI = 4, Debug = 5 };

// この描画呼び出しが 2D カメラの影響を受けるかどうか
enum class CameraMode { Inherit, Ignore };

// Spine などのメッシュ出力をワールド座標の四角形（三角形は rb=lb）として投入する。
void DrawMeshQuadHandleUV_Vertices(int textureHandle, const Vector2& lt, const Vector2& rt, const Vector2& lb,
                                   const Vector2& rb, float u_lt, float v_lt, float u_rt, float v_rt, float u_lb,
                                   float v_lb, float u_rb, float v_rb, CameraMode cam = CameraMode::Inherit,
                                   unsigned int rgba = 0xFFFFFFFF);

void SetCurrentLayer(RenderLayer layer);
RenderLayer GetCurrentLayer();
void BeginFrame();
void RenderLayerRange(RenderLayer minLayer, RenderLayer maxLayer, bool clearAfter = false);
void ClearSubmittedCommands();

} // namespace HIKARI::RENDERER
