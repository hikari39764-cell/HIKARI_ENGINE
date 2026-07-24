#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <json.hpp>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::ASSETS::MODELS::GLTF {

    using Json = nlohmann::json;
    using BufferStorage = std::vector<std::vector<uint8_t>>;

    bool ReadFloatAccessor(
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        int accessorIndex,
        int expectedComponents,
        std::vector<float>& out,
        int* outCount = nullptr);

    bool ReadNormalizedFloatAccessor(
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        int accessorIndex,
        int expectedComponents,
        std::vector<float>& out,
        int* outCount = nullptr);

    bool ReadScalarAccessor(
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        int accessorIndex,
        std::vector<float>& out);

    bool ReadIndexAccessor(
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        int accessorIndex,
        std::vector<uint32_t>& out);

    bool ReadMatrixAccessor(
        int accessorIndex,
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        std::vector<MATH::Mat4>& out);

    bool ReadJointAccessor(
        int accessorIndex,
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        std::vector<std::array<uint16_t, 4>>& out);

    bool ReadWeightAccessor(
        int accessorIndex,
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        std::vector<std::array<float, 4>>& out);

} // namespace HIKARI::ASSETS::MODELS::GLTF
