#include "HIKARI_EditorViewCapture.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Gfx/HIKARI_GfxContext.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include <d3dx12.h>
#include <DirectXTex.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>
#pragma comment(lib, "windowscodecs.lib")
namespace HIKARI::RENDER3D::EDITORVIEW {
namespace {
struct PreviewCaptureRequest {
    bool pending = false;
    bool done = false;
    bool scheduled = false;
    PreviewCaptureId id = 0;
    uint64_t minimumOutputRevision = 0;
    uint32_t width = 0, height = 0;
    uint64_t outputRevision = 0;
    RenderViewId viewId{};
    uint64_t frameIndex = 0, sceneRevision = 0;
    ResolvedCameraFrame cameraFrame{};
    std::string path;
    std::string error;
    std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
};
struct CaptureReadback {
    PreviewCaptureId id = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> staging, source;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    int width = 0, height = 0;
    UINT pitch = 0;
};
std::vector<CaptureReadback> g_captureReadbacks;

std::mutex g_captureMutex;
std::map<PreviewCaptureId, PreviewCaptureRequest> g_captureRequests{};
PreviewCaptureId g_captureSerial = 0;
ResolvedCameraFrame g_primaryCaptureCamera{};
uint64_t g_primaryCaptureSceneRevision = 0;
uint64_t g_primaryCaptureFrame = UINT64_MAX;

uint64_t NextNonZero(uint64_t value) { return ++value ? value : 1u; }
bool WriteBmp(const std::string& path, int width, int height, UINT sourcePitch, const uint8_t* source) {
    const int destinationRow = ((width * 3 + 3) / 4) * 4;
    const uint32_t imageSize = static_cast<uint32_t>(destinationRow) * static_cast<uint32_t>(height);
    const uint32_t fileSize = 54u + imageSize;
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    header[2] = static_cast<uint8_t>(fileSize & 0xffu);
    header[3] = static_cast<uint8_t>((fileSize >> 8u) & 0xffu);
    header[4] = static_cast<uint8_t>((fileSize >> 16u) & 0xffu);
    header[5] = static_cast<uint8_t>((fileSize >> 24u) & 0xffu);
    header[10] = 54u;
    header[14] = 40u;
    const auto writeInt = [&header](size_t offset, int32_t value) {
        const uint32_t bits = static_cast<uint32_t>(value);
        for (size_t index = 0u; index < 4u; ++index) {
            header[offset + index] = static_cast<uint8_t>((bits >> (index * 8u)) & 0xffu);
        }
    };
    writeInt(18, width);
    writeInt(22, height);
    header[26] = 1u;
    header[28] = 24u;
    writeInt(34, static_cast<int32_t>(imageSize));
    output.write(reinterpret_cast<const char*>(header), sizeof(header));
    std::vector<uint8_t> row(static_cast<size_t>(destinationRow));
    for (int y = height - 1; y >= 0; --y) {
        const uint8_t* sourceRow = source + static_cast<size_t>(y) * sourcePitch;
        for (int x = 0; x < width; ++x) {
            row[static_cast<size_t>(x) * 3u + 0u] = sourceRow[static_cast<size_t>(x) * 4u + 2u];
            row[static_cast<size_t>(x) * 3u + 1u] = sourceRow[static_cast<size_t>(x) * 4u + 1u];
            row[static_cast<size_t>(x) * 3u + 2u] = sourceRow[static_cast<size_t>(x) * 4u + 0u];
        }
        output.write(reinterpret_cast<const char*>(row.data()), row.size());
    }
    return output.good();
}

#ifdef _WIN32
bool WritePng(const std::string& path, int width, int height, UINT sourcePitch, const uint8_t* source) {
    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coResult) && coResult != RPC_E_CHANGED_MODE) {
        return false;
    }
    // 主画面とpreviewは表示用RGBA8。同じPNG encoderで色空間だけ明示し、再gamma変換しない。
    const DirectX::Image image{static_cast<size_t>(width),
                               static_cast<size_t>(height),
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               sourcePitch,
                               static_cast<size_t>(sourcePitch) * static_cast<size_t>(height),
                               const_cast<uint8_t*>(source)};
    const bool succeeded = SUCCEEDED(DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_FORCE_SRGB,
                                                            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),
                                                            TEXT::PathFromUtf8(path).c_str()));
    if (SUCCEEDED(coResult)) {
        CoUninitialize();
    }
    return succeeded;
}
#endif

bool WriteCaptureImage(const std::string& path, int width, int height, UINT sourcePitch, const uint8_t* source) {
#ifdef _WIN32
    const std::string lower = path;
    const auto endsWithPng = [&lower]() {
        if (lower.size() < 4u) {
            return false;
        }
        const auto asciiLower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return asciiLower(lower[lower.size() - 4u]) == '.' && asciiLower(lower[lower.size() - 3u]) == 'p' &&
               asciiLower(lower[lower.size() - 2u]) == 'n' && asciiLower(lower[lower.size() - 1u]) == 'g';
    };
    if (endsWithPng()) {
        return WritePng(path, width, height, sourcePitch, source);
    }
#else
    (void)path;
#endif
    return WriteBmp(path, width, height, sourcePitch, source);
}

void CollectCaptureReadbacks() {
    for (auto it = g_captureReadbacks.begin(); it != g_captureReadbacks.end();) {
        const uint64_t completed = it->fence->GetCompletedValue();
        if (completed < it->fenceValue) {
            ++it;
            continue;
        }
        std::lock_guard<std::mutex> lock(g_captureMutex);
        const auto found = g_captureRequests.find(it->id);
        if (found != g_captureRequests.end() && !found->second.done) {
            auto& request = found->second;
            void* mapped = nullptr;
            if (completed == UINT64_MAX)
                request.error = "截图失败：GPU 设备已移除";
            else if (FAILED(it->staging->Map(0, nullptr, &mapped)) || !mapped)
                request.error = "截图读回映射失败";
            else {
                if (!WriteCaptureImage(request.path, it->width, it->height, it->pitch,
                                       static_cast<const uint8_t*>(mapped)))
                    request.error = "截图图片写入失败";
                const D3D12_RANGE noWrites{0, 0};
                it->staging->Unmap(0, &noWrites);
            }
            request.done = true;
        }
        it = g_captureReadbacks.erase(it);
    }
}

} // namespace
void SetPrimaryViewCaptureFrame(const ResolvedCameraFrame& camera, uint64_t sceneRevision) {
    g_primaryCaptureCamera = camera;
    g_primaryCaptureSceneRevision = sceneRevision;
    g_primaryCaptureFrame = TIME::GetFrameContext().frameIndex;
}
void RecordViewCapture(RenderViewId viewId, bool ready, uint64_t outputRevision, ID3D12Resource* resource,
                       D3D12_RESOURCE_STATES resourceState, uint32_t width, uint32_t height,
                       const ResolvedCameraFrame* camera, uint64_t sceneRevision) {
    if (viewId == kPrimaryRenderViewId) {
        if (g_primaryCaptureFrame != TIME::GetFrameContext().frameIndex)
            return;
        camera = &g_primaryCaptureCamera;
        sceneRevision = g_primaryCaptureSceneRevision;
    }
    PreviewCaptureRequest request{};
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        const auto found = std::find_if(g_captureRequests.begin(), g_captureRequests.end(), [&](const auto& entry) {
            const auto& capture = entry.second;
            return capture.pending && capture.scheduled && capture.viewId == viewId && ready &&
                   outputRevision >= capture.minimumOutputRevision;
        });
        if (found == g_captureRequests.end())
            return;
        request = found->second;
        found->second.pending = false;
        found->second.width = width;
        found->second.height = height;
        found->second.outputRevision = outputRevision;
        found->second.frameIndex = TIME::GetFrameContext().frameIndex;
        found->second.sceneRevision = sceneRevision;
        if (camera)
            found->second.cameraFrame = *camera;
    }
    const auto finish = [id = request.id](std::string error) {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        const auto found = g_captureRequests.find(id);
        if (found == g_captureRequests.end())
            return;
        found->second.done = true;
        found->second.error = std::move(error);
    };

    if (!resource || !width || !height || resource->GetDesc().Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        finish("预览截图需要有效的 RGBA8 显示目标");
        return;
    }
    const auto& context = GFX::GetCurrentContext();
    auto* device = context.device;
    auto* list = context.cmdList;
    if (!device || !list || !context.frameFence || !context.currentFrameRetireFenceValue) {
        finish("截图需要有效的帧命令与完成栅栏");
        return;
    }
    const UINT sourcePitch =
        static_cast<UINT>((static_cast<UINT64>(width) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
                          ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u));
    const UINT64 totalBytes = static_cast<UINT64>(sourcePitch) * static_cast<UINT64>(height);

    CaptureReadback readback{};
    readback.id = request.id;
    readback.source = resource;
    readback.fence = context.frameFence;
    readback.fenceValue = context.currentFrameRetireFenceValue;
    readback.width = width;
    readback.height = height;
    readback.pitch = sourcePitch;
    const auto stagingDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
    if (FAILED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback.staging)))) {
        finish("创建截图读回资源失败");
        return;
    }
    const D3D12_RESOURCE_STATES previousState = resourceState;
    D3D12_RESOURCE_BARRIER toCopy =
        CD3DX12_RESOURCE_BARRIER::Transition(resource, previousState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1, &toCopy);
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = resource;
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = readback.staging.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destinationLocation.PlacedFootprint.Offset = 0u;
    destinationLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    destinationLocation.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
    destinationLocation.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
    destinationLocation.PlacedFootprint.Footprint.Depth = 1u;
    destinationLocation.PlacedFootprint.Footprint.RowPitch = sourcePitch;
    D3D12_BOX sourceBox{0u, 0u, 0u, static_cast<UINT>(width), static_cast<UINT>(height), 1u};
    list->CopyTextureRegion(&destinationLocation, 0u, 0u, 0u, &sourceLocation, &sourceBox);
    D3D12_RESOURCE_BARRIER toPrevious =
        CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, previousState);
    list->ResourceBarrier(1, &toPrevious);
    // 現在のフレームと同じリストへ記録し、完了後のフレームで回収する。
    // UI スレッドは GPU を待たず、取消時もリソースは fence まで保持する。
    g_captureReadbacks.push_back(std::move(readback));
}

PreviewCaptureId RequestViewCapture(RenderViewId viewId, const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    // 完了した未回収のレガシー要求だけを整理する。実行中の要求は上書きしない。
    if (g_captureRequests.size() >= 64) {
        for (auto it = g_captureRequests.begin(); it != g_captureRequests.end();) {
            if (it->second.done)
                it = g_captureRequests.erase(it);
            else
                ++it;
        }
    }
    if (g_captureRequests.size() >= 64 || outputPath.empty())
        return 0;
    for (const auto& [id, request] : g_captureRequests) {
        if (!request.done && request.path == outputPath)
            return 0;
    }
    PreviewCaptureRequest request{};
    request.pending = true;
    // 主画面は再描画要求を作らず、次の実際の最終表示から読む。
    request.scheduled = viewId == kPrimaryRenderViewId;
    request.id = g_captureSerial = NextNonZero(g_captureSerial);
    request.viewId = viewId;
    request.path = outputPath;
    g_captureRequests.emplace(request.id, request);
    return request.id;
}

void CancelViewCapture(PreviewCaptureId id) {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    const auto found = g_captureRequests.find(id);
    if (found == g_captureRequests.end())
        return;
    found->second.pending = false;
    found->second.done = true;
    found->second.error = "截图已取消";
}

PreviewCaptureResult GetViewCaptureResult(PreviewCaptureId id) {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    const auto found = g_captureRequests.find(id);
    if (found == g_captureRequests.end())
        return {true, {}, "Capture expired or was not found."};
    const auto& request = found->second;
    return {request.done,   request.path,           request.error,      request.viewId,        request.width,
            request.height, request.outputRevision, request.frameIndex, request.sceneRevision, request.cameraFrame};
}

void ScheduleViewCaptures(const std::function<std::optional<uint64_t>(RenderViewId)>& schedule) {
    CollectCaptureReadbacks();
    std::lock_guard<std::mutex> lock(g_captureMutex);
    for (auto& [id, capture] : g_captureRequests) {
        if (!capture.done && std::chrono::steady_clock::now() - capture.created > std::chrono::seconds(30)) {
            capture.pending = false;
            capture.done = true;
            capture.error = "截图超时";
        }
        if (!capture.pending || capture.viewId == kPrimaryRenderViewId)
            continue;
        const auto revision = schedule(capture.viewId);
        if (!revision) {
            capture.pending = false;
            capture.done = true;
            capture.error = "目标预览不可用，请先准备对应文档的预览";
        } else if (!capture.scheduled) {
            capture.minimumOutputRevision = NextNonZero(*revision);
            capture.scheduled = true;
        }
    }
}
void ShutdownViewCaptures() {
    g_primaryCaptureFrame = UINT64_MAX;
    g_primaryCaptureCamera = {};
    g_primaryCaptureSceneRevision = 0;
    for (auto& readback : g_captureReadbacks) {
        GFX::RetireD3D12ObjectForCurrentFrame(readback.staging, "Editor capture readback");
        GFX::RetireD3D12ObjectForCurrentFrame(readback.source, "Editor capture source");
    }
    g_captureReadbacks.clear();
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        g_captureRequests.clear();
    }
}
} // namespace HIKARI::RENDER3D::EDITORVIEW
#endif
