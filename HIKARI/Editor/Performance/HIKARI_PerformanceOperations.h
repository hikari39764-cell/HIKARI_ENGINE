#pragma once

#include <functional>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 性能の操作を登録する：
//   perf.capture [warmup] [samples] [trace]  現在のエディタの世界で捕獲を始める（perf.capture.status で結果を読む）
//   perf.capture.status                      進み具合と、終わった捕獲の要点
//   perf.compare <a> <b>                     2 つの捕獲を指標ごとに比べる
//   perf.findings [capture]                  捕獲への規則化された発見（既定は最新）
// 捕獲はプロジェクトの Library/Performance に保存される。捕獲の実体は Runtime の PERFORMANCE_CAPTURE
// （コマンドラインの --benchmark-output と同じ実装）、比較と発見は Diagnostics の PerformanceAnalysis。
void RegisterPerformanceOperations(EditorOperationRegistry& registry,
                                   std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
