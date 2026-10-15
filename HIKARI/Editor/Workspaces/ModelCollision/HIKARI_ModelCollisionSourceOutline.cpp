#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionSourceOutline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

constexpr size_t kMaximumTopologyEdges = 250000u;
constexpr float kNormalEpsilon = 1.0e-10f;

struct QuantizedPosition {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    bool operator==(const QuantizedPosition& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

bool PositionLess(const QuantizedPosition& left, const QuantizedPosition& right) noexcept {
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.z < right.z;
}

struct WeldedEdgeKey {
    QuantizedPosition first{};
    QuantizedPosition second{};

    bool operator==(const WeldedEdgeKey& other) const noexcept {
        return first == other.first && second == other.second;
    }
};

size_t HashCoordinate(int64_t value) noexcept {
    uint64_t hash = static_cast<uint64_t>(value);
    hash ^= hash >> 30u;
    hash *= 0xbf58476d1ce4e5b9ull;
    hash ^= hash >> 27u;
    hash *= 0x94d049bb133111ebull;
    hash ^= hash >> 31u;
    return static_cast<size_t>(hash);
}

struct WeldedEdgeKeyHash {
    size_t operator()(const WeldedEdgeKey& key) const noexcept {
        size_t hash = HashCoordinate(key.first.x);
        const auto combine = [&hash](int64_t value) {
            hash ^= HashCoordinate(value) + static_cast<size_t>(0x9e3779b9u) + (hash << 6u) + (hash >> 2u);
        };
        combine(key.first.y);
        combine(key.first.z);
        combine(key.second.x);
        combine(key.second.y);
        combine(key.second.z);
        return hash;
    }
};

QuantizedPosition QuantizePosition(const MATH::Vec3& position, double inverseStep) noexcept {
    constexpr double kCoordinateLimit = static_cast<double>((std::numeric_limits<int64_t>::max)() / 2);
    const auto quantize = [inverseStep](float coordinate) {
        const double scaled =
            std::clamp(static_cast<double>(coordinate) * inverseStep, -kCoordinateLimit, kCoordinateLimit);
        return static_cast<int64_t>(std::llround(scaled));
    };
    return {quantize(position.x), quantize(position.y), quantize(position.z)};
}

MATH::Vec3 TransformPoint(const MATH::Mat4& matrix, const MATH::Vec3& point) noexcept {
    const MATH::Vec4 transformed = matrix.TransformPoint({point.x, point.y, point.z, 1.0f});
    return {transformed.x, transformed.y, transformed.z};
}

MATH::Vec3 FaceNormal(const MATH::Vec3& first, const MATH::Vec3& second, const MATH::Vec3& third) noexcept {
    const MATH::Vec3 normal = MATH::Cross(second - first, third - first);
    const float length = MATH::Length(normal);
    return length > kNormalEpsilon ? normal * (1.0f / length) : MATH::Vec3{};
}

bool ProjectPoint(const Camera3D& camera, const MATH::Vec3& point, const ImVec2& origin, const ImVec2& size,
                  ImVec2& outPoint) noexcept {
    const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({point.x, point.y, point.z, 1.0f});
    if (clip.w <= 1.0e-5f) {
        return false;
    }
    const float inverseW = 1.0f / clip.w;
    const float x = clip.x * inverseW;
    const float y = clip.y * inverseW;
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    outPoint.x = origin.x + (x * 0.5f + 0.5f) * size.x;
    outPoint.y = origin.y + (0.5f - y * 0.5f) * size.y;
    return true;
}

} // namespace

void ModelCollisionSourceOutlineCache::Reset() noexcept {
    modelRevision_ = 0u;
    nodes_.clear();
}

const ModelCollisionSourceOutlineCache::NodeOutline* ModelCollisionSourceOutlineCache::GetOrBuild(
    const ModelAsset& model, const ModelPreviewNode& node, uint64_t modelRevision) {
    if (modelRevision_ != modelRevision) {
        nodes_.clear();
        modelRevision_ = modelRevision;
    }
    const auto found = nodes_.find(node.nodeIndex);
    if (found != nodes_.end()) {
        return &found->second;
    }
    auto [inserted, _] = nodes_.emplace(node.nodeIndex, Build(model, node));
    return &inserted->second;
}

ModelCollisionSourceOutlineCache::NodeOutline ModelCollisionSourceOutlineCache::Build(
    const ModelAsset& model, const ModelPreviewNode& node) const {
    NodeOutline outline{};
    if (node.meshIndex < 0 || static_cast<size_t>(node.meshIndex) >= model.meshes.size()) {
        return outline;
    }

    const MeshAsset& mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
    struct EdgeAccumulator {
        MATH::Vec3 first{};
        MATH::Vec3 second{};
        MATH::Vec3 normal1{};
        MATH::Vec3 normal2{};
        uint8_t faceCount = 0;
    };

    const MATH::Vec3 boundsExtent = node.bounds.max - node.bounds.min;
    const float boundsDiagonal = MATH::Length(boundsExtent);
    const double weldStep = static_cast<double>((std::max)(1.0e-5f, boundsDiagonal * 1.0e-6f));
    const double inverseWeldStep = 1.0 / weldStep;
    std::unordered_map<WeldedEdgeKey, EdgeAccumulator, WeldedEdgeKeyHash> topology{};
    topology.reserve(4096u);

    const auto addEdge = [&](const MATH::Vec3& first, const MATH::Vec3& second, const MATH::Vec3& normal) {
        QuantizedPosition firstKey = QuantizePosition(first, inverseWeldStep);
        QuantizedPosition secondKey = QuantizePosition(second, inverseWeldStep);
        if (firstKey == secondKey) {
            return;
        }
        if (PositionLess(secondKey, firstKey)) {
            std::swap(firstKey, secondKey);
        }

        const WeldedEdgeKey key{firstKey, secondKey};
        auto [found, inserted] = topology.try_emplace(key);
        EdgeAccumulator& edge = found->second;
        if (inserted) {
            edge.first = first;
            edge.second = second;
            edge.normal1 = normal;
            edge.faceCount = 1u;
        } else {
            if (edge.faceCount == 1u) {
                edge.normal2 = normal;
            }
            edge.faceCount = static_cast<uint8_t>((std::min)(255, static_cast<int>(edge.faceCount) + 1));
        }
    };

    for (const MeshPrimitive& primitive : mesh.primitives) {
        const size_t vertexCount = primitive.layout == VertexLayoutKind::SkinnedPNTTJW
                                       ? primitive.skinnedVertices.size()
                                       : primitive.staticVertices.size();
        if (vertexCount == 0u || primitive.indices.size() < 3u) {
            continue;
        }

        std::vector<MATH::Vec3> positions(vertexCount);
        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
            const MATH::Vec3 localPosition = primitive.layout == VertexLayoutKind::SkinnedPNTTJW
                                                 ? primitive.skinnedVertices[vertexIndex].position
                                                 : primitive.staticVertices[vertexIndex].position;
            positions[vertexIndex] = TransformPoint(node.globalTransform, localPosition);
        }

        for (size_t index = 0; index + 2u < primitive.indices.size(); index += 3u) {
            const uint32_t index0 = primitive.indices[index];
            const uint32_t index1 = primitive.indices[index + 1u];
            const uint32_t index2 = primitive.indices[index + 2u];
            if (index0 >= vertexCount || index1 >= vertexCount || index2 >= vertexCount || index0 == index1 ||
                index1 == index2 || index2 == index0) {
                continue;
            }
            const MATH::Vec3 normal = FaceNormal(positions[index0], positions[index1], positions[index2]);
            if (MATH::Dot(normal, normal) <= kNormalEpsilon) {
                continue;
            }
            addEdge(positions[index0], positions[index1], normal);
            addEdge(positions[index1], positions[index2], normal);
            addEdge(positions[index2], positions[index0], normal);
            if (topology.size() >= kMaximumTopologyEdges) {
                outline.truncated = true;
                break;
            }
        }

        if (outline.truncated) {
            break;
        }
    }

    outline.edges.reserve(topology.size());
    for (const auto& [_, source] : topology) {
        Edge edge{};
        edge.first = source.first;
        edge.second = source.second;
        edge.firstFaceNormal = source.normal1;
        edge.secondFaceNormal = source.normal2;
        edge.faceCount = source.faceCount;
        outline.edges.push_back(edge);
    }
    return outline;
}

bool ModelCollisionSourceOutlineCache::Draw(ImDrawList* drawList, const Camera3D& camera, float viewportX,
                                            float viewportY, float viewportWidth, float viewportHeight,
                                            const ModelAsset& model, const ModelPreviewNode& node,
                                            uint64_t modelRevision, uint32_t color, float thickness) {
    if (drawList == nullptr || viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        return false;
    }
    const NodeOutline* outline = GetOrBuild(model, node, modelRevision);
    if (outline == nullptr || outline->edges.empty()) {
        return false;
    }

    const ImVec2 origin{viewportX, viewportY};
    const ImVec2 size{viewportWidth, viewportHeight};
    const MATH::Vec3 cameraPosition = camera.GetPosition();
    size_t drawnEdgeCount = 0u;
    for (const Edge& edge : outline->edges) {
        const MATH::Vec3 midpoint = (edge.first + edge.second) * 0.5f;
        const MATH::Vec3 toCamera = cameraPosition - midpoint;
        const float firstFacing = MATH::Dot(edge.firstFaceNormal, toCamera);
        const float secondFacing = MATH::Dot(edge.secondFaceNormal, toCamera);
        const bool boundary = edge.faceCount == 1u;
        const bool silhouette = edge.faceCount == 2u && ((firstFacing >= 0.0f) != (secondFacing >= 0.0f));
        if (!boundary && !silhouette) {
            continue;
        }

        ImVec2 first{};
        ImVec2 second{};
        if (!ProjectPoint(camera, edge.first, origin, size, first) ||
            !ProjectPoint(camera, edge.second, origin, size, second)) {
            continue;
        }
        drawList->AddLine(first, second, IM_COL32(12, 16, 20, 230), thickness + 2.6f);
        drawList->AddLine(first, second, color, thickness);
        ++drawnEdgeCount;
    }
    return drawnEdgeCount > 0u;
}

} // namespace HIKARI::EDITOR
