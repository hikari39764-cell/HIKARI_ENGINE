#include "Tools/Geometry/HIKARI_MeshLodGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

#include "../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"

namespace HIKARI::TOOLS::GEOMETRY {

    namespace {
        using RENDER3D::CLUSTER::ClusterVertex;

        constexpr size_t kLodAttributeCount = 15u;

        struct PositionKey {
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t z = 0;

            bool operator==(const PositionKey& rhs) const {
                return x == rhs.x && y == rhs.y && z == rhs.z;
            }
        };

        struct PositionKeyHash {
            size_t operator()(const PositionKey& key) const {
                size_t h = static_cast<size_t>(key.x) * 73856093u;
                h ^= static_cast<size_t>(key.y) * 19349663u;
                h ^= static_cast<size_t>(key.z) * 83492791u;
                return h;
            }
        };

        struct EdgeKey {
            uint32_t a = 0;
            uint32_t b = 0;

            bool operator==(const EdgeKey& rhs) const {
                return a == rhs.a && b == rhs.b;
            }
        };

        struct EdgeKeyHash {
            size_t operator()(const EdgeKey& key) const {
                return (static_cast<size_t>(key.a) * 16777619u) ^
                    static_cast<size_t>(key.b);
            }
        };

        size_t AlignIndexCountToTriangles(size_t indexCount) {
            return indexCount - (indexCount % 3u);
        }

        float ClampRatio(float value) {
            if (!std::isfinite(value)) {
                return 0.75f;
            }
            return (std::max)(0.05f, (std::min)(value, 0.98f));
        }

        float ClampTargetError(float value) {
            if (!std::isfinite(value)) {
                return 0.006f;
            }
            return (std::max)(0.0001f, (std::min)(value, 0.08f));
        }

        uint32_t FloatBits(float value) {
            if (value == 0.0f) {
                return 0u;
            }

            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        PositionKey MakePositionKey(const ClusterVertex& vertex) {
            return {
                FloatBits(vertex.position.x),
                FloatBits(vertex.position.y),
                FloatBits(vertex.position.z)
            };
        }

        EdgeKey MakeEdgeKey(uint32_t a, uint32_t b) {
            if (a <= b) {
                return { a, b };
            }
            return { b, a };
        }

        void BuildSimplifierStreams(
            const std::vector<ClusterVertex>& vertices,
            std::vector<float>& outPositions,
            std::vector<float>& outAttributes) {

            outPositions.clear();
            outAttributes.clear();
            outPositions.reserve(vertices.size() * 3u);
            outAttributes.reserve(vertices.size() * kLodAttributeCount);

            for (const ClusterVertex& vertex : vertices) {
                outPositions.push_back(vertex.position.x);
                outPositions.push_back(vertex.position.y);
                outPositions.push_back(vertex.position.z);

                // LOD は見た目の破綻を避けるため、形状だけでなく主要属性も誤差評価に入れる。
                outAttributes.push_back(vertex.normal.x);
                outAttributes.push_back(vertex.normal.y);
                outAttributes.push_back(vertex.normal.z);
                outAttributes.push_back(vertex.tangent.x);
                outAttributes.push_back(vertex.tangent.y);
                outAttributes.push_back(vertex.tangent.z);
                outAttributes.push_back(vertex.tangent.w);
                outAttributes.push_back(vertex.uv0.x);
                outAttributes.push_back(vertex.uv0.y);
                outAttributes.push_back(vertex.uv1.x);
                outAttributes.push_back(vertex.uv1.y);
                outAttributes.push_back(vertex.color.x);
                outAttributes.push_back(vertex.color.y);
                outAttributes.push_back(vertex.color.z);
                outAttributes.push_back(vertex.color.w);
            }
        }

        std::vector<uint32_t> BuildPositionIds(const std::vector<ClusterVertex>& vertices) {
            std::unordered_map<PositionKey, uint32_t, PositionKeyHash> positionRemap{};
            positionRemap.reserve(vertices.size());

            std::vector<uint32_t> positionIds(vertices.size(), 0u);
            for (size_t i = 0; i < vertices.size(); ++i) {
                const PositionKey key = MakePositionKey(vertices[i]);
                const auto found = positionRemap.find(key);
                if (found != positionRemap.end()) {
                    positionIds[i] = found->second;
                    continue;
                }

                const uint32_t id = static_cast<uint32_t>(positionRemap.size());
                positionRemap.emplace(key, id);
                positionIds[i] = id;
            }
            return positionIds;
        }

        std::vector<unsigned char> BuildGeometricBorderLocks(
            const std::vector<ClusterVertex>& vertices,
            const std::vector<unsigned int>& indices) {

            std::vector<unsigned char> locks(vertices.size(), 0u);
            if (vertices.empty() || indices.size() < 3u) {
                return {};
            }

            const std::vector<uint32_t> positionIds = BuildPositionIds(vertices);
            uint32_t positionIdCount = 0u;
            for (uint32_t id : positionIds) {
                positionIdCount = (std::max)(positionIdCount, id + 1u);
            }

            std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> edgeUseCount{};
            edgeUseCount.reserve(indices.size());

            auto addEdge = [&](uint32_t ia, uint32_t ib) {
                if (ia >= positionIds.size() || ib >= positionIds.size()) {
                    return;
                }

                const uint32_t pa = positionIds[ia];
                const uint32_t pb = positionIds[ib];
                if (pa == pb) {
                    return;
                }

                uint32_t& count = edgeUseCount[MakeEdgeKey(pa, pb)];
                if (count < (std::numeric_limits<uint32_t>::max)()) {
                    ++count;
                }
            };

            for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
                const uint32_t i0 = indices[i + 0u];
                const uint32_t i1 = indices[i + 1u];
                const uint32_t i2 = indices[i + 2u];
                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                    continue;
                }
                addEdge(i0, i1);
                addEdge(i1, i2);
                addEdge(i2, i0);
            }

            std::vector<unsigned char> borderPositions(positionIdCount, 0u);
            for (const auto& entry : edgeUseCount) {
                if (entry.second != 1u) {
                    continue;
                }
                borderPositions[entry.first.a] = 1u;
                borderPositions[entry.first.b] = 1u;
            }

            uint32_t lockedCount = 0u;
            for (size_t i = 0; i < positionIds.size(); ++i) {
                if (borderPositions[positionIds[i]] == 0u) {
                    continue;
                }
                // 位置で見た本当の外周だけを固定し、UV/法線 seam は簡略化対象に残す。
                locks[i] = static_cast<unsigned char>(meshopt_SimplifyVertex_Lock);
                ++lockedCount;
            }

            if (lockedCount == 0u) {
                return {};
            }
            return locks;
        }

        bool CompactLodVertices(
            const std::vector<ClusterVertex>& sourceVertices,
            const std::vector<unsigned int>& sourceIndices,
            MeshLodGeneratorResult& outResult) {

            outResult.vertices.clear();
            outResult.indices.clear();
            if (sourceVertices.empty() || sourceIndices.size() < 3u) {
                return false;
            }

            std::vector<uint32_t> remap(
                sourceVertices.size(),
                RENDER3D::CLUSTER::kInvalidClusterIndex);
            outResult.vertices.reserve(sourceVertices.size());
            outResult.indices.reserve(sourceIndices.size());

            for (unsigned int rawIndex : sourceIndices) {
                if (rawIndex >= sourceVertices.size()) {
                    return false;
                }

                uint32_t& mappedIndex = remap[rawIndex];
                if (mappedIndex == RENDER3D::CLUSTER::kInvalidClusterIndex) {
                    mappedIndex = static_cast<uint32_t>(outResult.vertices.size());
                    outResult.vertices.push_back(sourceVertices[rawIndex]);
                }
                outResult.indices.push_back(mappedIndex);
            }

            if (outResult.indices.size() < 3u) {
                return false;
            }

            std::vector<uint32_t> filteredIndices{};
            filteredIndices.reserve(outResult.indices.size());
            for (size_t i = 0; i + 2u < outResult.indices.size(); i += 3u) {
                const uint32_t i0 = outResult.indices[i + 0u];
                const uint32_t i1 = outResult.indices[i + 1u];
                const uint32_t i2 = outResult.indices[i + 2u];
                if (i0 == i1 || i1 == i2 || i2 == i0) {
                    continue;
                }
                filteredIndices.push_back(i0);
                filteredIndices.push_back(i1);
                filteredIndices.push_back(i2);
            }

            outResult.indices = std::move(filteredIndices);
            return outResult.indices.size() >= 3u && !outResult.vertices.empty();
        }

        std::array<float, kLodAttributeCount> BuildAttributeWeights() {
            return {
                0.55f, 0.55f, 0.55f,
                0.25f, 0.25f, 0.25f, 0.10f,
                0.70f, 0.70f,
                0.20f, 0.20f,
                0.12f, 0.12f, 0.12f, 0.12f
            };
        }
    }

    bool GenerateClusterSurfaceLod(
        const std::vector<ClusterVertex>& sourceVertices,
        const std::vector<uint32_t>& sourceIndices,
        const MeshLodGeneratorSettings& settings,
        MeshLodGeneratorResult& outResult) {

        outResult = {};
        const size_t sourceIndexCount = AlignIndexCountToTriangles(sourceIndices.size());
        if (sourceVertices.empty() || sourceIndexCount < 96u) {
            return false;
        }

        const float targetRatio = ClampRatio(settings.targetTriangleRatio);
        size_t targetIndexCount = AlignIndexCountToTriangles(
            static_cast<size_t>(std::floor(static_cast<double>(sourceIndexCount) * targetRatio)));
        targetIndexCount = (std::max)(size_t{ 48u }, targetIndexCount);
        if (targetIndexCount >= sourceIndexCount) {
            return false;
        }

        std::vector<unsigned int> sourceIndexData{};
        sourceIndexData.reserve(sourceIndexCount);
        for (size_t i = 0; i < sourceIndexCount; ++i) {
            sourceIndexData.push_back(static_cast<unsigned int>(sourceIndices[i]));
        }

        std::vector<float> positions{};
        std::vector<float> attributes{};
        BuildSimplifierStreams(sourceVertices, positions, attributes);

        std::vector<unsigned int> simplifiedIndices(sourceIndexCount);
        const std::array<float, kLodAttributeCount> attributeWeights = BuildAttributeWeights();
        unsigned int options = meshopt_SimplifyRegularizeLight;
        if (settings.lockOpenBorders) {
            options |= meshopt_SimplifyLockBorder;
        }
        if (settings.allowAttributeSeamCollapse) {
            options |= meshopt_SimplifyPermissive;
        }
        if (settings.pruneIsolatedComponents) {
            options |= meshopt_SimplifyPrune;
        }

        std::vector<unsigned char> vertexLocks{};
        if (settings.protectGeometricBorders) {
            vertexLocks = BuildGeometricBorderLocks(sourceVertices, sourceIndexData);
        }

        float resultError = 0.0f;
        size_t simplifiedIndexCount = 0u;
        if (settings.preserveAttributes) {
            simplifiedIndexCount = meshopt_simplifyWithAttributes(
                simplifiedIndices.data(),
                sourceIndexData.data(),
                sourceIndexCount,
                positions.data(),
                sourceVertices.size(),
                sizeof(float) * 3u,
                attributes.data(),
                sizeof(float) * kLodAttributeCount,
                attributeWeights.data(),
                kLodAttributeCount,
                vertexLocks.empty() ? nullptr : vertexLocks.data(),
                targetIndexCount,
                ClampTargetError(settings.targetError),
                options,
                &resultError);
        } else {
            simplifiedIndexCount = meshopt_simplify(
                simplifiedIndices.data(),
                sourceIndexData.data(),
                sourceIndexCount,
                positions.data(),
                sourceVertices.size(),
                sizeof(float) * 3u,
                targetIndexCount,
                ClampTargetError(settings.targetError),
                options,
                &resultError);
        }

        simplifiedIndexCount = AlignIndexCountToTriangles(simplifiedIndexCount);
        if (simplifiedIndexCount < 3u || simplifiedIndexCount >= sourceIndexCount) {
            return false;
        }

        simplifiedIndices.resize(simplifiedIndexCount);
        if (settings.optimizeVertexCache) {
            meshopt_optimizeVertexCache(
                simplifiedIndices.data(),
                simplifiedIndices.data(),
                simplifiedIndices.size(),
                sourceVertices.size());
        }

        if (!CompactLodVertices(sourceVertices, simplifiedIndices, outResult)) {
            return false;
        }

        const float scale = meshopt_simplifyScale(
            positions.data(),
            sourceVertices.size(),
            sizeof(float) * 3u);
        outResult.geometricError = resultError * scale;
        outResult.simplified = outResult.indices.size() < sourceIndexCount;
        return outResult.simplified;
    }

} // namespace HIKARI::TOOLS::GEOMETRY
