#pragma once
#include <cstdint>
namespace HIKARI::INPUT {
class InputService;
struct InputPointerState;
} // namespace HIKARI::INPUT

namespace HIKARI::SERVICES {
bool IsInProcessGamePresentationActive();
bool IsGamePresentationActive();

// ゲーム画像の座標系を SDK とネイティブ UI で共有する。
bool TryGetGamePointer(const INPUT::InputService& input, INPUT::InputPointerState& pointer, uint32_t& width,
                       uint32_t& height);

bool BeginInProcessGamePresentation();
bool BeginInProcessGamePresentationStop();
bool DrainInProcessGamePresentation();
bool RestoreEditorPresentation();
bool EndInProcessGamePresentation();
bool ConsumeInProcessGameCloseRequest();
void UpdateGamePresentationPerformanceTitle();
} // namespace HIKARI::SERVICES
