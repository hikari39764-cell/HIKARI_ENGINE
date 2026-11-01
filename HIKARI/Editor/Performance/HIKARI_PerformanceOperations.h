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
//   perf.findings [capture]                  捕獲か性能の分解への規則化された発見（既定は最新）
//   perf.breakdown [preset] [warmup] [samples] [rounds] [cancel]
//                                            画質の項目を 1 つずつ切り替えて、重い部分を測る（perf.breakdown.status で結果を読む）
//   perf.breakdown.status                    進み具合と、終わった分解の表
// 結果はプロジェクトの Library/Performance に保存される。捕獲の実体は Runtime の PERFORMANCE_CAPTURE
// （コマンドラインの --benchmark-output と同じ実装）、分解は PERFORMANCE_BREAKDOWN（--benchmark-breakdown と同じ実装）、
// 比較と発見は Diagnostics の PerformanceAnalysis と PerformanceBreakdownAnalysis。
void RegisterPerformanceOperations(EditorOperationRegistry& registry,
                                   std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
