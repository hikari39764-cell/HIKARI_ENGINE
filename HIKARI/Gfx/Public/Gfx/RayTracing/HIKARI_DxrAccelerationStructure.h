#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_FencedUploadBuffers.h"

namespace HIKARI::GFX {
struct Context;
}

namespace HIKARI::GFX::RAYTRACING {

// BLAS は共有ジオメトリ、TLAS は View 非依存のインスタンス集合を表す。
// 入力ジオメトリは build 完了まで、参照 BLAS は TLAS の最終使用まで
// 呼び出し側が保持する。ここでは Scene/材質/選択予算を所有しない。
class DxrAccelerationStructure final {
public:
    DxrAccelerationStructure() = default;
    ~DxrAccelerationStructure();
    DxrAccelerationStructure(const DxrAccelerationStructure&) = delete;
    DxrAccelerationStructure& operator=(const DxrAccelerationStructure&) = delete;

    // allowUpdate なら後で RefitInPlace できる（refit 用の scratch も確保する）。
    bool BuildBottomLevel(const Context& graphics, std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometry,
                          uint64_t maximumAllocationBytes, std::string* outMessage = nullptr, bool allowUpdate = false);
    bool BuildTopLevel(const Context& graphics, std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances,
                       uint64_t maximumAllocationBytes, bool allowUpdate = false,
                       const DxrAccelerationStructure* updateSource = nullptr, std::string* outMessage = nullptr,
                       uint32_t reservedInstances = 0u);
    // 同じ形（記述の数・旗）のまま、結果を作り直さずにその場で refit する。毎フレーム頂点が動く BLAS と、
    // それを指す TLAS に使う。allowUpdate で build したものだけ。geometry は BLAS の時だけ渡す（build と同じ数）。
    bool RefitInPlace(const Context& graphics, std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometry = {});
    // TLAS を組み直す。結果と scratch は作り直さずその場へ書く（配置や個体の数だけが変わったフレーム用）。
    // 数は build の時の reservedInstances まで増やせる。
    // refit と違い BVH の質は落ちないので、遠くへ動いた instance があっても光線の走査は重くならない。
    // instance の upload は GPU が読み終えたものを使い回す。収まらない・形が違う時は false（呼び出し側が build する）。
    bool RebuildTopLevelInPlace(const Context& graphics, std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances);
    // 呼び出し側が GPU 上に書いた instance 記述（count 個、NON_PIXEL_SHADER_RESOURCE）から同じようにその場で組み直す。
    // 記述は呼び出し側が持ち続け、以後の refit もそれを読む（CPU の記述を書き直すまで）。
    bool RebuildTopLevelInPlace(const Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, uint32_t count);
    void Reset(const Context* retireContext = nullptr);

    bool IsReady() const noexcept { return result_ != nullptr; }
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const noexcept;
    // 現在の result + scratch + instance upload（予備を含む）の committed allocation。
    // 遅延解放待ちの旧世代、共有入力ジオメトリは含まない。
    uint64_t GetAllocationBytes() const noexcept { return allocationBytes_ + instances_.AllocationBytes(); }

private:
    bool Build(const Context& graphics, D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs,
               std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances, uint64_t maximumAllocationBytes,
               const DxrAccelerationStructure* updateSource, std::string* outMessage,
               uint32_t reservedDescriptors = 0u);
    // 組み直しの共通部分。結果と scratch に収まらなければ false（instanceDescs は書く前に確かめてから渡す）。
    bool CanRebuildTopLevelInPlace(const Context& graphics, uint32_t count) const;
    bool RecordTopLevelRebuild(const Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, uint32_t count);

    Microsoft::WRL::ComPtr<ID3D12Resource> result_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> scratch_{};
    // TLAS の instance 記述。その場の組み直しは GPU が読み終えた予備へ書く。
    FencedUploadBuffers instances_{UPLOAD_TRAFFIC::Owner::RayTracingInstances};
    // 最後の組み立てが呼び出し側の GPU 記述を読んだ時はその番地（refit も読む）。0 なら instances_。
    D3D12_GPU_VIRTUAL_ADDRESS externalInstances_ = 0;
    ID3D12Device* device_ = nullptr;
    uint64_t deviceGeneration_ = 0;
    uint64_t allocationBytes_ = 0; // result + scratch
    uint64_t resultBytes_ = 0;
    uint64_t scratchBytes_ = 0;
    uint32_t descriptorCount_ = 0;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE type_{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags_{};
};

struct DxrPosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class DxrTriangleAccelerationStructure final {
public:
    ~DxrTriangleAccelerationStructure();
    // indices の先頭 opaqueIndexCount 個が不透明の geometry 0、残りが any-hit を通す geometry 1（alpha test 用）。
    bool Build(const Context& graphics, const std::vector<DxrPosition>& positions, const std::vector<uint32_t>& indices,
               size_t opaqueIndexCount, std::string* outMessage = nullptr);
    void Reset();

    bool IsReady() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS
    GetTopLevelGpuAddress() const noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_{};
    DxrAccelerationStructure bottomLevel_{};
    DxrAccelerationStructure topLevel_{};
};

} // namespace HIKARI::GFX::RAYTRACING
