#pragma once

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}
namespace HIKARI::POST { struct SunGlareFrame; }

namespace HIKARI::RENDER3D::PIPELINE {

    // 太陽の意味情報を view 固有の Post 入力へ変換する。GPU 状態は変更しない。
    POST::SunGlareFrame BuildSunGlareFrame(const Camera3D& camera, const SceneEnvironment& environment);

} // namespace HIKARI::RENDER3D::PIPELINE
