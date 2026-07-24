#pragma once

#include <unordered_map>

#include <d3d12.h>
#include <d3dx12.h>

namespace HIKARI::GFX {

    class ResourceStateTracker {
    public:
        void Reset();

        void Track(
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES initialState);

        void Forget(ID3D12Resource* resource);

        bool Has(ID3D12Resource* resource) const;

        D3D12_RESOURCE_STATES GetState(
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES fallback) const;

        void Transition(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES after);

        void Transition(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after);

    private:
        std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> knownStates_;
    };

} // namespace HIKARI::GFX
