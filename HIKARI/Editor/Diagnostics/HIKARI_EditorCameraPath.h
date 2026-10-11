#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

// 診断用：编辑相机を帧号で決まる経路に沿って動かす（移動中の阴影・灯光の跳変を無頭で再現する）。
// 起動参数 --editor-camera-path "帧:x,y,z,pitch,yaw;帧:x,y,z,pitch,yaw;..."（世界の位置 [m]、向き [度]、
// 場景の rotationEulerDeg と同じ pitch/yaw：yaw 0 が +Z、pitch 正が下向き）。鍵の間は smoothstep で補間し、
// 最初の鍵より前と最後の鍵より後はその姿勢で止まる。--editor-camera-freeze F は帧 F の姿勢で止める
// （同じ帧号の「静止して収束した画」と比べるため）。Play 中は使わない。経路がある間は灯・相机などの辅助表示を描かない。
class EditorCameraPath {
public:
    // 起動参数から読む。無ければ空。
    static std::optional<EditorCameraPath> FromCommandLine();

    void Apply(Camera3D& camera, uint64_t frame) const;

private:
    static std::optional<EditorCameraPath> Parse(std::string_view text, uint64_t freezeFrame);

    struct Key {
        float frame = 0.0f;
        float values[5]{}; // x, y, z, pitch, yaw
    };
    std::vector<Key> keys_;
    uint64_t freezeFrame_ = UINT64_MAX;
};

} // namespace HIKARI::EDITOR
