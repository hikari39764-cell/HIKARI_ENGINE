#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

// Render3D_ParticleSimulation.hlsl の CollisionViewConstants（b2）：碰撞深度を撮った視图。
struct GpuParticleCollisionView {
    MATH::Mat4 viewProj{};
    MATH::Mat4 invViewProj{};
    MATH::Vec4 size{};   // xy 碰撞深度の texel 数、zw その逆数
    MATH::Vec4 camera{}; // xyz 視点、w 有効（1）
};

class GpuParticleStorage final {
public:
    bool Initialize(const GFX::Context& context, uint32_t particleCapacity, uint32_t emitterPageCapacity,
                    uint32_t drawCapacity = kMaximumParticleEmitterSubmissionsPerFrame);
    void Shutdown(const GFX::Context& context) noexcept;

    bool IsReady() const noexcept;
    void Transition(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    void InsertUavBarrier(const GFX::Context& context) const;
    void InsertDrawResetUavBarrier(const GFX::Context& context) const;
    void InsertDrawCounterUavBarrier(const GFX::Context& context) const;
    void TransitionDrawCounters(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    void TransitionDrawArguments(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    void TransitionAliveIndices(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);

    bool WriteEmitterParameters(ID3D12Device* device, uint32_t frameSlot, GpuParticleEmitterUploadAddress address,
                                const GpuParticleEmitterParameters& parameters);
    D3D12_GPU_VIRTUAL_ADDRESS GetEmitterUploadGpuAddress(uint32_t frameSlot, uint32_t pageIndex) const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleUavGpuHandle() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetParticleGpuAddress() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetAliveIndexGpuAddress() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetDrawCounterGpuAddress() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetDrawArgumentGpuAddress() const noexcept;
    ID3D12Resource* GetDrawArgumentResource() const noexcept;
    // 形状パラメータは per-draw バッファで渡す。drawIndex で引く。
    D3D12_GPU_VIRTUAL_ADDRESS GetShapeStateGpuAddress(uint32_t frameSlot) const noexcept;
    GpuParticleShapeState* MappedShapeStates(uint32_t frameSlot) noexcept;

    // 場景深度碰撞（t5・u1・b2）：主視图の深度を半解像度にした R32F。大きさが変われば作り直す。撮る前と作れない時は
    // 空の記述子（読むと 0）を返し、視图の w = 0 で shader は碰撞しない。
    bool EnsureCollisionDepth(const GFX::Context& context, uint32_t width, uint32_t height);
    void TransitionCollisionDepth(const GFX::Context& context, D3D12_RESOURCE_STATES nextState);
    D3D12_GPU_DESCRIPTOR_HANDLE GetCollisionDepthSrv() const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE GetCollisionDepthUav() const noexcept;
    // 書かない派発（UpdateCS など）の u1 に置く空の記述子。
    D3D12_GPU_DESCRIPTOR_HANDLE GetNullCollisionDepthUav() const noexcept { return nullCollisionDepthUav_.gpu; }
    void WriteCollisionView(uint32_t frameSlot, const GpuParticleCollisionView& view);
    D3D12_GPU_VIRTUAL_ADDRESS GetCollisionViewGpuAddress(uint32_t frameSlot) const noexcept;

private:
    bool EnsureEmitterUploadPage(ID3D12Device* device, uint32_t frameSlot, uint32_t pageIndex);

    Microsoft::WRL::ComPtr<ID3D12Resource> particles_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveIndices_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> drawCounters_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> drawArguments_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, GFX::kFrameResourceCount> shapeStates_{};
    std::array<GpuParticleShapeState*, GFX::kFrameResourceCount> mappedShapeStates_{};
    RenderResourceView particleSrv_{};
    RenderResourceView particleUav_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> collisionDepth_{};
    RenderResourceView collisionDepthSrv_{};
    RenderResourceView collisionDepthUav_{};
    RenderResourceView nullCollisionDepthSrv_{};
    RenderResourceView nullCollisionDepthUav_{};
    uint32_t collisionDepthWidth_ = 0u;
    uint32_t collisionDepthHeight_ = 0u;
    D3D12_RESOURCE_STATES collisionDepthState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, GFX::kFrameResourceCount> collisionViews_{};
    std::array<GpuParticleCollisionView*, GFX::kFrameResourceCount> mappedCollisionViews_{};
    std::array<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>, GFX::kFrameResourceCount> emitterUploadPages_{};
    std::array<std::vector<GpuParticleEmitterParameters*>, GFX::kFrameResourceCount> mappedEmitterPages_{};
    uint32_t emitterPageCapacity_ = 0u;
    D3D12_RESOURCE_STATES particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES drawCounterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES drawArgumentState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES aliveIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
