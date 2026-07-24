#pragma once
#include "Core/Math/HIKARI_Vector2.h"
#include "Matrix3x3.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

	struct Transform2D {
		Vector2 position{ 0.0f, 0.0f };
		Vector2 scale{ 1.0f, 1.0f };
		float rotation{ 0.0f }; // ラジアン角度
		Vector2 pivotPx{ 0.0f, 0.0f }; // オブジェクト内のピクセル基準点

		// ローカル座標 → ワールド座標（カメラ無し）
		Matrix3x3 ToWorld(float width, float height) const;

		// 2D semantics with explicit z-sort output as Mat4 (for unified 2D/3D path)
		MATH::Mat4 GetWorldMatrix(float zForSort = 0.0f) const;
	};

} // namespace HIKARI
