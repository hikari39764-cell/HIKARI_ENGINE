#pragma once
#include "Core/Math/HIKARI_Vector2.h"
#include "Render2D/HIKARI_Matrix3x3.h"

namespace HIKARI {
namespace INPUT {
class InputSnapshot;
}

// 2D コマンド（Spine など）用のカメラ。パン／ズーム／回転／俯仰からビュー行列を作る。
namespace CAMERA {

void SetScreenCenter(const Vector2& screenCenter);
// 画面サイズを設定し、画面中心と回転基準点を更新する
void SetScreenSize(int width, int height);
int GetScreenWidth();
int GetScreenHeight();

// デバッグ入力（エディターカメラ操作）によるパン／ズーム／回転を許可する
void EnableDebugControl(bool enable);

void Update(float deltaTime, const INPUT::InputSnapshot* input = nullptr);
const Matrix3x3& GetViewMatrix();

} // namespace CAMERA
} // namespace HIKARI
