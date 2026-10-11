#include "Editor/Diagnostics/HIKARI_EditorCameraPath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"

namespace HIKARI::EDITOR {
namespace {

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

std::optional<EditorCameraPath> EditorCameraPath::FromCommandLine() {
    const auto path = ReadRuntimeCommandLinePath(L"--editor-camera-path");
    if (!path)
        return std::nullopt;
    const auto freeze = ReadRuntimeCommandLineUint32(L"--editor-camera-freeze");
    return Parse(TEXT::PathToUtf8(*path), freeze ? *freeze : UINT64_MAX);
}

std::optional<EditorCameraPath> EditorCameraPath::Parse(std::string_view text, uint64_t freezeFrame) {
    EditorCameraPath path;
    path.freezeFrame_ = freezeFrame;
    const std::string source(text);
    const char* cursor = source.c_str();
    while (*cursor != '\0') {
        char* end = nullptr;
        Key key{};
        key.frame = std::strtof(cursor, &end);
        if (end == cursor || *end != ':')
            return std::nullopt;
        cursor = end + 1;
        for (float& value : key.values) {
            value = std::strtof(cursor, &end);
            if (end == cursor)
                return std::nullopt;
            cursor = *end == ',' ? end + 1 : end;
        }
        path.keys_.push_back(key);
        cursor = *cursor == ';' ? cursor + 1 : cursor;
    }
    if (path.keys_.empty())
        return std::nullopt;
    std::sort(path.keys_.begin(), path.keys_.end(), [](const Key& a, const Key& b) { return a.frame < b.frame; });
    return path;
}

void EditorCameraPath::Apply(Camera3D& camera, uint64_t frame) const {
    const float f = static_cast<float>((std::min)(frame, freezeFrame_));
    size_t next = 0;
    while (next < keys_.size() && keys_[next].frame <= f)
        ++next;
    const Key& a = keys_[next == 0 ? 0 : next - 1];
    const Key& b = keys_[next < keys_.size() ? next : keys_.size() - 1];
    const float t = b.frame > a.frame ? Smooth((f - a.frame) / (b.frame - a.frame)) : 0.0f;
    float v[5];
    for (int i = 0; i < 5; ++i)
        v[i] = a.values[i] + (b.values[i] - a.values[i]) * t;
    // rotationEulerDeg と同じ回転（Ry * Rx）で +Z を向けた視線。
    constexpr float kDegrees = 0.01745329252f;
    const float pitch = v[3] * kDegrees;
    const float yaw = v[4] * kDegrees;
    const MATH::Vec3 eye{v[0], v[1], v[2]};
    const MATH::Vec3 forward{std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    camera.SetLookAt(eye, {eye.x + forward.x, eye.y + forward.y, eye.z + forward.z});
}

} // namespace HIKARI::EDITOR
