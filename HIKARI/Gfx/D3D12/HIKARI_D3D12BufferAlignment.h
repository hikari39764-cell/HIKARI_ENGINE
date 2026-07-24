#pragma once

#include <cstddef>
#include <cstdint>

namespace HIKARI::GFX {

    constexpr uint32_t AlignD3D12ConstantBufferByteSize(size_t byteSize) {
        return static_cast<uint32_t>((byteSize + 255u) & ~size_t{ 255u });
    }

} // namespace HIKARI::GFX
