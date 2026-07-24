#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <d3d12.h>

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"

namespace HIKARI::RENDER3D::TEMPORAL::INTERNAL {

    template <typename Constants, typename Slot, size_t SlotCount>
    bool EnsureMappedConstantBufferSlots(
        ID3D12Device* device,
        std::array<Slot, SlotCount>& slots,
        const wchar_t* resourceNamePrefix) {

        for (size_t index = 0; index < SlotCount; ++index) {
            Slot& slot = slots[index];
            if (slot.buffer != nullptr && slot.mapped != nullptr) {
                continue;
            }
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    GFX::AlignD3D12ConstantBufferByteSize(sizeof(Constants)),
                    slot.buffer,
                    reinterpret_cast<void**>(&slot.mapped))) {
                return false;
            }
            const std::wstring name =
                resourceNamePrefix + std::to_wstring(index);
            slot.buffer->SetName(name.c_str());
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::TEMPORAL::INTERNAL
