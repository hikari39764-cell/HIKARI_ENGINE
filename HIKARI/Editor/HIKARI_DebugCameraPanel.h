#pragma once

namespace HIKARI {

    class DebugCameraController3D;

    class DebugCameraPanel {
    public:
        void Draw(DebugCameraController3D& debugCamera) const;
        void DrawContents(DebugCameraController3D& debugCamera) const;
    };

} // namespace HIKARI
