#pragma once

#include <string>

namespace HIKARI::INPUT::ActionIds {

inline const std::string ToggleEditorUI = "Global.ToggleEditorUI";
inline const std::string StopPlay = "Global.StopPlay";
inline const std::string CloseProgram = "Global.CloseProgram";
inline const std::string GameplayMove = "Gameplay.Move";
inline const std::string GameplayLook = "Gameplay.Look";
inline const std::string GameplayCameraZoom = "Gameplay.CameraZoom";
inline const std::string GameplayCameraRecenter = "Gameplay.CameraRecenter";
inline const std::string GameplayJump = "Gameplay.Jump";
inline const std::string GameplaySprint = "Gameplay.Sprint";
inline const std::string GameplayInteract = "Gameplay.Interact";
inline const std::string EditorCameraMove = "Editor.CameraMove";
inline const std::string EditorCameraMoveVertical = "Editor.CameraMoveVertical";
inline const std::string EditorCameraLook = "Editor.CameraLook";
inline const std::string EditorCameraLookHeld = "Editor.CameraLookHeld";
inline const std::string EditorCameraPan = "Editor.CameraPan";
inline const std::string EditorCameraPanHeld = "Editor.CameraPanHeld";
inline const std::string EditorCameraZoom = "Editor.CameraZoom";
inline const std::string EditorCameraBoost = "Editor.CameraBoost";
inline const std::string EditorCameraReset = "Editor.CameraReset";

} // namespace HIKARI::INPUT::ActionIds
