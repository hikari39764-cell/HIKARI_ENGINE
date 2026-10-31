#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"

// Gfx 診断設定の項目表。コンソール変数（gfx.）が使う。
// GfxDebugConfig を読むだけの所に JSON の取り込みを持ち込まないよう、構造体の宣言とは別のヘッダに置く。
namespace HIKARI::GFX {

// ReadOnly はデバイスを作る時にだけ効く項目（Dx12Core::Initialize と ConfigureD3D12InfoQueue が読む）。実行中に変えても
// 効かない。
inline constexpr auto kGfxDebugConfigFields = [](auto&& field) {
    using S = GfxDebugConfig;
    using JSONFIELDS::ReadOnly;
    field("enableDebugLayer", &S::enableDebugLayer, ReadOnly{});
    field("enableGpuBasedValidation", &S::enableGpuBasedValidation, ReadOnly{});
    field("enableGpuFrameProfiler", &S::enableGpuFrameProfiler);
    field("enableGpuFrameProfilerWithDebugLayer", &S::enableGpuFrameProfilerWithDebugLayer);
    field("enableGpuPipelineStats", &S::enableGpuPipelineStats);
    field("enableClusterGpuCullCounterReadback", &S::enableClusterGpuCullCounterReadback);
    field("enableClusterGpuCullDebugCounters", &S::enableClusterGpuCullDebugCounters);
    field("enableInfoQueueBreakOnError", &S::enableInfoQueueBreakOnError, ReadOnly{});
    field("enableInfoQueueBreakOnWarning", &S::enableInfoQueueBreakOnWarning, ReadOnly{});
    field("dumpInfoQueueOnFrameEnd", &S::dumpInfoQueueOnFrameEnd);
    field("verbosePostLog", &S::verbosePostLog);
};

} // namespace HIKARI::GFX
