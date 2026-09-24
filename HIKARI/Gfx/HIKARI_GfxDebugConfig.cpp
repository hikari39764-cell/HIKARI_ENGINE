#include "HIKARI_GfxDebugConfig.h"

namespace HIKARI::GFX {
namespace {
// グラフィックス診断設定をプロセス内で保持する。
GfxDebugConfig gConfig{};
} // namespace

const GfxDebugConfig& GetGfxDebugConfig() { return gConfig; }

void SetGfxDebugConfig(const GfxDebugConfig& config) { gConfig = config; }
} // namespace HIKARI::GFX
