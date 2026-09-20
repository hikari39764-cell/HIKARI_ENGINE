#pragma once
#include "HIKARI_EditorInteractiveViewRenderer.h"
#include <functional>
#include <optional>
#if defined(HIKARI_WITH_EDITOR)
namespace HIKARI::RENDER3D::EDITORVIEW {
// Presentation-owner entry points; public ticket APIs remain thread-safe.
void ScheduleViewCaptures(const std::function<std::optional<uint64_t>(RenderViewId)>& schedule);
void RecordViewCapture(RenderViewId viewId,bool ready,uint64_t outputRevision,
    ID3D12Resource* resource,D3D12_RESOURCE_STATES resourceState,uint32_t width,uint32_t height);
void ShutdownViewCaptures();
}
#endif
