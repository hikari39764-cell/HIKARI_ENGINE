#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include "Render2D/HIKARI_RenderTarget2D.h"

namespace HIKARI::TOOLS::BAKING {

inline constexpr float kProbeCaptureNearPlane = 0.05f;

class ProbeCubemapCaptureTarget {
public:
    // near/far が有効な場合、HDR capture の alpha に正規化した放射距離を保存する。
    bool Initialize(uint32_t resolution, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT, float nearPlane = 0.0f,
                    float farPlane = 0.0f);
    void Release();

    bool BeginFace(uint32_t faceIndex, float r, float g, float b, float a, float depth);
    void EndFace(uint32_t faceIndex);

    bool QueueReadbackFace(uint32_t faceIndex, std::string* outMessage = nullptr);
    bool SaveReadbackToCubemapDds(const std::filesystem::path& outputPath, std::string* outMessage = nullptr) const;

    uint32_t GetResolution() const { return resolution_; }
    DXGI_FORMAT GetFormat() const { return format_; }
    bool IsInitialized() const { return initialized_; }
    float GetVisibilityDistanceScale() const;

private:
    struct FaceReadback {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
    };

    bool CreateReadback(ID3D12Resource* resource, FaceReadback& readback, std::string* outMessage);
    bool StoreDepthInCaptureAlpha(uint32_t faceIndex, uint8_t* pixels, size_t rowPitch, std::string* outMessage) const;

private:
    std::array<RenderTarget2D, 6> faces_{};
    std::array<FaceReadback, 6> readbacks_{};
    std::array<FaceReadback, 6> depthReadbacks_{};
    uint32_t resolution_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    float nearPlane_ = 0.0f;
    float farPlane_ = 0.0f;
    bool initialized_ = false;
};

} // namespace HIKARI::TOOLS::BAKING
