#include "Render3D/Lighting/HIKARI_DynamicGiProbeDump.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <DirectXPackedVector.h>
#include <d3dx12.h>
#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"

namespace HIKARI::RENDER3D::DYNAMICGI {
namespace {
using Json = nlohmann::json;

// HIKARI_DynamicGiProbeAtlas.hlsli の tile と同じ並び。
constexpr uint32_t kTileWidth = 28, kTileHeight = 18, kOctahedralSide = 8;
constexpr uint32_t kStateX = 0, kStateY = 10, kStatisticsX = 0, kStatisticsY = 11, kConnectivityX = 1,
                   kConnectivityY = 10;
constexpr float kIrradianceGamma = 5.0f;

// HikariEncodeOctahedralDirection と同じ。
std::array<float, 2> EncodeOctahedral(float x, float y, float z) {
    const float sum = (std::max)(std::abs(x) + std::abs(y) + std::abs(z), 1e-6f);
    x /= sum, y /= sum, z /= sum;
    float u = x, v = y;
    if (z < 0.0f) {
        u = (1.0f - std::abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        v = (1.0f - std::abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    return {u * 0.5f + 0.5f, v * 0.5f + 0.5f};
}

struct Texels {
    const uint8_t* bytes;
    uint32_t rowPitch;
    std::array<float, 4> Read(uint32_t x, uint32_t y) const {
        const auto* half = reinterpret_cast<const DirectX::PackedVector::HALF*>(bytes + size_t(y) * rowPitch) + x * 4u;
        return {
            DirectX::PackedVector::XMConvertHalfToFloat(half[0]), DirectX::PackedVector::XMConvertHalfToFloat(half[1]),
            DirectX::PackedVector::XMConvertHalfToFloat(half[2]), DirectX::PackedVector::XMConvertHalfToFloat(half[3])};
    }
    // HikariSampleDynamicGiIrradiance と同じ：境界込み tile の (1 + uv * side) を保存値のまま双線形に読む。
    std::array<float, 4> SampleOctahedral(uint32_t tileX, uint32_t tileY, std::array<float, 2> uv) const {
        const float px = 0.5f + uv[0] * kOctahedralSide, py = 0.5f + uv[1] * kOctahedralSide;
        const auto x0 = uint32_t(px), y0 = uint32_t(py);
        const float fx = px - float(x0), fy = py - float(y0);
        std::array<float, 4> result{};
        for (uint32_t corner = 0; corner < 4; ++corner) {
            const uint32_t dx = corner & 1u, dy = corner >> 1u;
            const float weight = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
            const auto value = Read(tileX + x0 + dx, tileY + y0 + dy);
            for (size_t channel = 0; channel < 4; ++channel)
                result[channel] += weight * value[channel];
        }
        return result;
    }
};

double Round(float value) { return std::round(double(value) * 1.0e5) / 1.0e5; }

Json DescribeField(const LIGHTPROBE::LightProbeVolumeRuntimeData& data, size_t field, const Texels& texels) {
    static constexpr std::array<std::pair<const char*, std::array<float, 3>>, 6> kAxes{{{"+x", {1, 0, 0}},
                                                                                        {"-x", {-1, 0, 0}},
                                                                                        {"+y", {0, 1, 0}},
                                                                                        {"-y", {0, -1, 0}},
                                                                                        {"+z", {0, 0, 1}},
                                                                                        {"-z", {0, 0, -1}}}};
    Json probes = Json::array();
    for (uint32_t z = 0; z < data.countZ; ++z) {
        for (uint32_t y = 0; y < data.countY; ++y) {
            for (uint32_t x = 0; x < data.countX; ++x) {
                const uint32_t tileX = (x + y * data.countX) * kTileWidth, tileY = z * kTileHeight;
                const auto state = texels.Read(tileX + kStateX, tileY + kStateY);
                const auto statistics = texels.Read(tileX + kStatisticsX, tileY + kStatisticsY);
                // 隣 26 probe との連通（HikariDynamicGiNeighborDelta の並び、bit が立てば間を形状が遮らない）。
                const auto connectivity = texels.Read(tileX + kConnectivityX, tileY + kConnectivityY);
                const Json connected = connectivity[3] < 0.5f ? Json(nullptr)
                                                              : Json(uint32_t(connectivity[0] + 0.5f) |
                                                                     (uint32_t(connectivity[1] + 0.5f) << 11u) |
                                                                     (uint32_t(connectivity[2] + 0.5f) << 22u));
                Json irradiance = Json::object(), sky = Json::object();
                for (const auto& [name, axis] : kAxes) {
                    const auto value =
                        texels.SampleOctahedral(tileX, tileY, EncodeOctahedral(axis[0], axis[1], axis[2]));
                    irradiance[name] = {Round(std::pow((std::max)(value[0], 0.0f), kIrradianceGamma)),
                                        Round(std::pow((std::max)(value[1], 0.0f), kIrradianceGamma)),
                                        Round(std::pow((std::max)(value[2], 0.0f), kIrradianceGamma))};
                    sky[name] = Round(value[3]);
                }
                probes.push_back({{"cell", {x, y, z}},
                                  {"position",
                                   {Round(data.origin.x + x * data.spacing.x + state[0]),
                                    Round(data.origin.y + y * data.spacing.y + state[1]),
                                    Round(data.origin.z + z * data.spacing.z + state[2])}},
                                  {"active", state[3] > 0.0f},
                                  {"updates", Round(statistics[0])},
                                  {"variation", Round(statistics[1])},
                                  {"backRatio", Round(statistics[2])},
                                  {"fixedLevel", Round(statistics[3])},
                                  {"connectivity", connected},
                                  {"irradiance", std::move(irradiance)},
                                  {"skyVisibility", std::move(sky)}});
            }
        }
    }
    static constexpr const char* kNames[] = {"region", "near", "far"};
    return {{"field", field < std::size(kNames) ? kNames[field] : "unknown"},
            {"origin", {data.origin.x, data.origin.y, data.origin.z}},
            {"spacing", {data.spacing.x, data.spacing.y, data.spacing.z}},
            {"counts", {data.countX, data.countY, data.countZ}},
            {"probes", std::move(probes)}};
}

} // namespace

void ProbeDump::Request(std::filesystem::path path) { requested_ = std::move(path); }

void ProbeDump::Record(const GFX::Context& graphics, ID3D12GraphicsCommandList* commands, size_t field,
                       ID3D12Resource* atlas, const LIGHTPROBE::LightProbeVolumeRuntimeData& data) {
    if (atlas == nullptr || data.countX == 0)
        return;
    const D3D12_RESOURCE_DESC desc = atlas->GetDesc();
    Capture capture{};
    UINT64 bytes = 0;
    graphics.device->GetCopyableFootprints(&desc, 0, 1, 0, &capture.footprint, nullptr, nullptr, &bytes);
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);
    const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    if (FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(
            graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            GFX::VIDEO_MEMORY::Owner::Diagnostics, IID_PPV_ARGS(&capture.buffer))))
        return;
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        atlas, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->ResourceBarrier(1, &barrier);
    const CD3DX12_TEXTURE_COPY_LOCATION destination(capture.buffer.Get(), capture.footprint);
    const CD3DX12_TEXTURE_COPY_LOCATION source(atlas, 0);
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands->ResourceBarrier(1, &barrier);
    capture.field = field;
    capture.data = data;
    captures_.push_back(std::move(capture));
    fence_ = graphics.currentFrameRetireFenceValue;
}

void ProbeDump::Poll(const GFX::Context& graphics) {
    if (captures_.empty() || !GFX::IsCurrentFenceComplete(fence_))
        return;
    Json fields = Json::array();
    for (Capture& capture : captures_) {
        void* mapped = nullptr;
        const D3D12_RANGE range{0, size_t(capture.footprint.Footprint.RowPitch) * capture.footprint.Footprint.Height};
        if (SUCCEEDED(capture.buffer->Map(0, &range, &mapped)) && mapped != nullptr) {
            const Texels texels{static_cast<const uint8_t*>(mapped) + capture.footprint.Offset,
                                capture.footprint.Footprint.RowPitch};
            fields.push_back(DescribeField(capture.data, capture.field, texels));
            const D3D12_RANGE written{0, 0};
            capture.buffer->Unmap(0, &written);
        }
        GFX::RetireD3D12ObjectForFrame(capture.buffer, graphics, "DynamicGI.ProbeDump");
    }
    captures_.clear();
    std::string message;
    if (SERIALIZATION::JSON::WriteJsonFile(requested_, Json{{"fields", std::move(fields)}}, &message))
        HIKARI_LOG_INFO("[DynamicGi] probe dump written: " + TEXT::PathToUtf8(requested_));
    else
        HIKARI_LOG_ERROR("[DynamicGi] probe dump failed: " + message);
    requested_.clear();
}

void ProbeDump::Reset(const GFX::Context& graphics) {
    for (Capture& capture : captures_)
        GFX::RetireD3D12ObjectForFrame(capture.buffer, graphics, "DynamicGI.ProbeDump");
    captures_.clear();
    requested_.clear();
}

} // namespace HIKARI::RENDER3D::DYNAMICGI
