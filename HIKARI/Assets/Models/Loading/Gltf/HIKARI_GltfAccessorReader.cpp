#include "Assets/Models/Loading/Gltf/HIKARI_GltfAccessorReader.h"

#include <cstring>
#include <string>

namespace HIKARI::ASSETS::MODELS::GLTF {

    namespace {
        using nlohmann::json;

        int GetAccessorComponentCount(const std::string& type) {
            if (type == "SCALAR") return 1;
            if (type == "VEC2") return 2;
            if (type == "VEC3") return 3;
            if (type == "VEC4") return 4;
            return 0;
        }
    } // namespace

        bool ReadFloatAccessor(
            const Json& accessors,
            const Json& bufferViews,
            const BufferStorage& loadedBuffers,
            int accessorIndex,
            int expectedComponents,
            std::vector<float>& out,
            int* outCount) {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            if (componentType != 5126) return false; // FLOAT

            const int components = GetAccessorComponentCount(accessor.value("type", ""));
            if (components != expectedComponents) return false;

            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            if (outCount) *outCount = count;

            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", components * 4));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];

            out.resize(static_cast<size_t>(count) * static_cast<size_t>(components));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + static_cast<size_t>(components * 4) > bufferData.size()) {
                    return false;
                }
                std::memcpy(out.data() + static_cast<size_t>(i * components), bufferData.data() + srcOffset, static_cast<size_t>(components * 4));
            }
            return true;
    }

        bool ReadNormalizedFloatAccessor(
            const Json& accessors,
            const Json& bufferViews,
            const BufferStorage& loadedBuffers,
            int accessorIndex,
            int expectedComponents,
            std::vector<float>& out,
            int* outCount) {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            const int components = GetAccessorComponentCount(accessor.value("type", ""));
            if (components != expectedComponents) return false;

            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            if (outCount) *outCount = count;

            const size_t componentSize =
                (componentType == 5126) ? 4u :
                ((componentType == 5121) ? 1u :
                ((componentType == 5123) ? 2u : 0u));
            if (componentSize == 0u) return false;

            const bool normalized = accessor.value("normalized", componentType != 5126);
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t elementSize = componentSize * static_cast<size_t>(components);
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(elementSize)));
            if (stride < elementSize) return false;

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count) * static_cast<size_t>(components));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + elementSize > bufferData.size()) {
                    return false;
                }
                for (int c = 0; c < components; ++c) {
                    const size_t componentOffset = srcOffset + componentSize * static_cast<size_t>(c);
                    float value = 0.0f;
                    if (componentType == 5126) {
                        std::memcpy(&value, bufferData.data() + componentOffset, sizeof(float));
                    } else if (componentType == 5121) {
                        const uint8_t raw = bufferData[componentOffset];
                        value = normalized ? static_cast<float>(raw) / 255.0f : static_cast<float>(raw);
                    } else {
                        uint16_t raw = 0;
                        std::memcpy(&raw, bufferData.data() + componentOffset, sizeof(uint16_t));
                        value = normalized ? static_cast<float>(raw) / 65535.0f : static_cast<float>(raw);
                    }
                    out[static_cast<size_t>(i * components + c)] = value;
                }
            }
            return true;
    }

        bool ReadScalarAccessor(
            const Json& accessors,
            const Json& bufferViews,
            const BufferStorage& loadedBuffers,
            int accessorIndex,
            std::vector<float>& out) {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            const int count = accessor.value("count", 0);
            if (count <= 0) return false;

            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count));

            if (componentType == 5126) {
                const size_t stride = static_cast<size_t>(view.value("byteStride", 4));
                for (int i = 0; i < count; ++i) {
                    const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                    if (srcOffset + 4 > bufferData.size()) return false;
                    std::memcpy(&out[static_cast<size_t>(i)], bufferData.data() + srcOffset, 4);
                }
                return true;
            }
            return false;
    }

        bool ReadIndexAccessor(
            const Json& accessors,
            const Json& bufferViews,
            const BufferStorage& loadedBuffers,
            int accessorIndex,
            std::vector<uint32_t>& out) {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) return false;
            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) return false;

            const int componentType = accessor.value("componentType", 0);
            const int count = accessor.value("count", 0);
            if (count <= 0) return false;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t strideDefault =
                (componentType == 5121) ? 1u :
                ((componentType == 5123) ? 2u :
                ((componentType == 5125) ? 4u : 0u));
            if (strideDefault == 0u) return false;
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(strideDefault)));
            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];

            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + strideDefault > bufferData.size()) return false;
                if (componentType == 5121) {
                    out[static_cast<size_t>(i)] = static_cast<uint32_t>(bufferData[srcOffset]);
                } else if (componentType == 5123) {
                    uint16_t v = 0;
                    std::memcpy(&v, bufferData.data() + srcOffset, sizeof(uint16_t));
                    out[static_cast<size_t>(i)] = static_cast<uint32_t>(v);
                } else {
                    uint32_t v = 0;
                    std::memcpy(&v, bufferData.data() + srcOffset, sizeof(uint32_t));
                    out[static_cast<size_t>(i)] = v;
                }
            }
            return true;
    }

        bool ReadMatrixAccessor(
            int accessorIndex,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            std::vector<MATH::Mat4>& out) {
            out.clear();
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
                return false;
            }

            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            if (accessor.value("componentType", 0) != 5126 || accessor.value("type", "") != "MAT4") {
                return false;
            }

            const int count = accessor.value("count", 0);
            if (count <= 0) {
                return false;
            }

            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
                return false;
            }

            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) {
                return false;
            }

            constexpr size_t kMat4ByteSize = sizeof(float) * 16u;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(kMat4ByteSize)));
            if (stride < kMat4ByteSize) {
                return false;
            }

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count), MATH::Mat4::Identity());
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + kMat4ByteSize > bufferData.size()) {
                    out.clear();
                    return false;
                }

                float values[16]{};
                std::memcpy(values, bufferData.data() + srcOffset, kMat4ByteSize);
                MATH::Mat4 mat = MATH::Mat4::Identity();
                for (int col = 0; col < 4; ++col) {
                    for (int row = 0; row < 4; ++row) {
                        mat.m[col][row] = values[col * 4 + row];
                    }
                }
                out[static_cast<size_t>(i)] = mat;
            }
            return true;
        }

        bool ReadJointAccessor(
            int accessorIndex,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            std::vector<std::array<uint16_t, 4>>& out) {
            out.clear();
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
                return false;
            }

            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            if (accessor.value("type", "") != "VEC4") {
                return false;
            }

            const int componentType = accessor.value("componentType", 0);
            const size_t componentSize = (componentType == 5121) ? 1u : ((componentType == 5123) ? 2u : 0u);
            if (componentSize == 0u) {
                return false;
            }

            const int count = accessor.value("count", 0);
            if (count <= 0) {
                return false;
            }

            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
                return false;
            }

            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) {
                return false;
            }

            const size_t elementSize = componentSize * 4u;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(elementSize)));
            if (stride < elementSize) {
                return false;
            }

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + elementSize > bufferData.size()) {
                    out.clear();
                    return false;
                }

                std::array<uint16_t, 4> joints{};
                for (size_t c = 0; c < joints.size(); ++c) {
                    const size_t componentOffset = srcOffset + componentSize * c;
                    if (componentType == 5121) {
                        joints[c] = static_cast<uint16_t>(bufferData[componentOffset]);
                    } else {
                        uint16_t value = 0;
                        std::memcpy(&value, bufferData.data() + componentOffset, sizeof(uint16_t));
                        joints[c] = value;
                    }
                }
                out[static_cast<size_t>(i)] = joints;
            }
            return true;
        }

        bool ReadWeightAccessor(
            int accessorIndex,
            const json& accessors,
            const json& bufferViews,
            const std::vector<std::vector<uint8_t>>& loadedBuffers,
            std::vector<std::array<float, 4>>& out) {
            out.clear();
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) {
                return false;
            }

            const json& accessor = accessors[static_cast<size_t>(accessorIndex)];
            if (accessor.value("type", "") != "VEC4") {
                return false;
            }

            const int componentType = accessor.value("componentType", 0);
            const size_t componentSize = (componentType == 5126) ? 4u : ((componentType == 5121) ? 1u : ((componentType == 5123) ? 2u : 0u));
            if (componentSize == 0u) {
                return false;
            }

            const int count = accessor.value("count", 0);
            if (count <= 0) {
                return false;
            }

            const int bufferViewIndex = accessor.value("bufferView", -1);
            if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size())) {
                return false;
            }

            const json& view = bufferViews[static_cast<size_t>(bufferViewIndex)];
            const int bufferIndex = view.value("buffer", -1);
            if (bufferIndex < 0 || bufferIndex >= static_cast<int>(loadedBuffers.size())) {
                return false;
            }

            const size_t elementSize = componentSize * 4u;
            const size_t accessorOffset = static_cast<size_t>(accessor.value("byteOffset", 0));
            const size_t viewOffset = static_cast<size_t>(view.value("byteOffset", 0));
            const size_t stride = static_cast<size_t>(view.value("byteStride", static_cast<int>(elementSize)));
            if (stride < elementSize) {
                return false;
            }

            const std::vector<uint8_t>& bufferData = loadedBuffers[static_cast<size_t>(bufferIndex)];
            out.resize(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const size_t srcOffset = viewOffset + accessorOffset + stride * static_cast<size_t>(i);
                if (srcOffset + elementSize > bufferData.size()) {
                    out.clear();
                    return false;
                }

                std::array<float, 4> weights{};
                for (size_t c = 0; c < weights.size(); ++c) {
                    const size_t componentOffset = srcOffset + componentSize * c;
                    if (componentType == 5126) {
                        std::memcpy(&weights[c], bufferData.data() + componentOffset, sizeof(float));
                    } else if (componentType == 5121) {
                        weights[c] = static_cast<float>(bufferData[componentOffset]) / 255.0f;
                    } else {
                        uint16_t value = 0;
                        std::memcpy(&value, bufferData.data() + componentOffset, sizeof(uint16_t));
                        weights[c] = static_cast<float>(value) / 65535.0f;
                    }
                }

                const float sum = weights[0] + weights[1] + weights[2] + weights[3];
                if (sum > 0.00001f) {
                    for (float& weight : weights) {
                        weight /= sum;
                    }
                } else {
                    weights = { 1.0f, 0.0f, 0.0f, 0.0f };
                }
                out[static_cast<size_t>(i)] = weights;
            }
            return true;
        }

} // namespace HIKARI::ASSETS::MODELS::GLTF
