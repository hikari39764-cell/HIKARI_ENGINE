#pragma once

#include <algorithm>
#include <cmath>
#include "HIKARI/Project/ActionTypes.h"

namespace HIKARI {
    // ScreenUp: 2D の画面上下。ViewDirection: 地面上の前進（見上げても後退しない）。
    enum class GameProjectCameraPlanarForward { ScreenUp, ViewDirection };
    namespace CAMERA_SPACE_DETAIL {
        inline float Dot(GameProjectFloat3 a, GameProjectFloat3 b) noexcept {
            return a.x*b.x + a.y*b.y + a.z*b.z;
        }
        inline GameProjectFloat3 Cross(GameProjectFloat3 a, GameProjectFloat3 b) noexcept {
            return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
        }
        inline bool Normalize(GameProjectFloat3& v) noexcept {
            const float squared = Dot(v,v);
            if (!std::isfinite(squared) || squared < 1e-10f) return false;
            const float inverse = 1.0f/std::sqrt(squared);
            v = {v.x*inverse,v.y*inverse,v.z*inverse};
            return true;
        }
        inline GameProjectFloat3 Reject(GameProjectFloat3 v, GameProjectFloat3 normal) noexcept {
            const float along = Dot(v,normal);
            return {v.x-normal.x*along,v.y-normal.y*along,v.z-normal.z*along};
        }
    }

    // 入力の右/上をカメラ基準の移動平面へ写す。XZ は normal=(0,1,0)、XY は (0,0,1)。
    // 対角入力は長さ 1 以下、アナログ入力の強さは維持する。解けない視点は false/zero。
    inline bool ResolveCameraPlanarMovement(const GameProjectCameraPose& camera,
        float horizontal, float vertical, GameProjectFloat3 planeNormal,
        GameProjectFloat3& movement,
        GameProjectCameraPlanarForward forwardMode = GameProjectCameraPlanarForward::ScreenUp) noexcept {
        using namespace CAMERA_SPACE_DETAIL;
        movement = {};
        if (!std::isfinite(horizontal) || !std::isfinite(vertical) || !Normalize(planeNormal)) return false;
        GameProjectFloat3 forward{camera.target.x-camera.eye.x,
            camera.target.y-camera.eye.y,camera.target.z-camera.eye.z};
        if (!Normalize(forward)) return false;
        // Camera3D は右手系。up x forward は画面の左になってしまう。
        auto right = Reject(Cross(forward,camera.up),planeNormal);
        if (!Normalize(right)) return false;
        const bool alongView = forwardMode == GameProjectCameraPlanarForward::ViewDirection;
        auto up = Reject(Reject(alongView ? forward : camera.up,planeNormal),right);
        if (!Normalize(up)) {
            up = Reject(Reject(alongView ? camera.up : forward,planeNormal),right);
            if (!Normalize(up)) return false;
        }
        const float length = std::max(1.0f,std::hypot(horizontal,vertical));
        horizontal /= length;
        vertical /= length;
        movement = {right.x*horizontal+up.x*vertical,
            right.y*horizontal+up.y*vertical,right.z*horizontal+up.z*vertical};
        return true;
    }
}
