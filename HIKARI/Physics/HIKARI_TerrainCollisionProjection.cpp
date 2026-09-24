#include "Physics/HIKARI_TerrainCollisionProjection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI::PHYSICS {
namespace {
constexpr uint32_t kInvalidVertexIndex = (std::numeric_limits<uint32_t>::max)();

struct AuthoredLatticeKey {
    int64_t x = 0;
    int64_t z = 0;

    friend bool operator==(const AuthoredLatticeKey&, const AuthoredLatticeKey&) = default;
};

bool LatticeKeyLess(const AuthoredLatticeKey& lhs, const AuthoredLatticeKey& rhs) noexcept {
    return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.z < rhs.z);
}

struct AuthoredLatticeKeyHash {
    std::size_t operator()(const AuthoredLatticeKey& value) const noexcept {
        const std::size_t x = std::hash<int64_t>{}(value.x);
        const std::size_t z = std::hash<int64_t>{}(value.z);
        return x ^ (z + 0x9e3779b9u + (x << 6u) + (x >> 2u));
    }
};

struct SourceEdgeKey {
    AuthoredLatticeKey first{};
    AuthoredLatticeKey second{};

    friend bool operator==(const SourceEdgeKey&, const SourceEdgeKey&) = default;
};

SourceEdgeKey MakeSourceEdgeKey(const AuthoredLatticeKey& first, const AuthoredLatticeKey& second) noexcept {
    return LatticeKeyLess(second, first) ? SourceEdgeKey{second, first} : SourceEdgeKey{first, second};
}

struct SourceEdgeKeyHash {
    std::size_t operator()(const SourceEdgeKey& value) const noexcept {
        const std::size_t first = AuthoredLatticeKeyHash{}(value.first);
        const std::size_t second = AuthoredLatticeKeyHash{}(value.second);
        return first ^ (second + 0x9e3779b9u + (first << 6u) + (first >> 2u));
    }
};

struct TriangleKey {
    std::array<uint32_t, 3u> indices{};

    friend bool operator==(const TriangleKey&, const TriangleKey&) = default;
};

struct TriangleKeyHash {
    std::size_t operator()(const TriangleKey& value) const noexcept {
        std::size_t result = 0u;
        for (const uint32_t index : value.indices) {
            const std::size_t part = std::hash<uint32_t>{}(index);
            result ^= part + 0x9e3779b9u + (result << 6u) + (result >> 2u);
        }
        return result;
    }
};

struct CoverageVertex {
    AuthoredLatticeKey key{};
    MATH::Vec3 position{};
    float coverage = 0.0f;
};

struct SourceVertexRecord {
    uint32_t index = kInvalidVertexIndex;
    float coverage = 0.0f;
};

using MATH::IsFinite;

bool SameVertexValue(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

std::string ChunkLabel(const TERRAIN::TerrainChunkKey& key) {
    return "chunk(" + std::to_string(key.x) + "," + std::to_string(key.z) + ")";
}

TerrainCollisionProjection MakeProjectionBase(RuntimeObjectHandle object,
                                              const TerrainSurfaceComponent& terrain) noexcept {
    TerrainCollisionProjection result{};
    result.object = object;
    result.sourceRevision = terrain.GetCollisionRevision();
    result.authoredRevision = terrain.GetGeometryRevision();
    // 0 は source revision 無しに予約する。wrap 時も 0 を出さない。
    result.contentRevision = result.authoredRevision == (std::numeric_limits<uint64_t>::max)()
                                 ? (std::numeric_limits<uint64_t>::max)()
                                 : result.authoredRevision + 1u;
    return result;
}

bool ValidateTerrainChunkInputOnly(const TerrainSurfaceComponent& terrain, const TERRAIN::TerrainChunkKey& key,
                                   const TERRAIN::TerrainChunk& chunk, std::string& diagnostic) {
    const TERRAIN::TerrainSurface& surface = terrain.GetSurface();
    if (!std::isfinite(surface.chunkCellSize)) {
        diagnostic = "chunkCellSize is not finite";
        return false;
    }
    if (!std::isfinite(surface.shoreline.edgeFalloff) || !std::isfinite(surface.shoreline.edgeDepth) ||
        !std::isfinite(surface.deform.rimStrength)) {
        diagnostic = "shoreline/deform collision settings are not finite";
        return false;
    }

    const uint32_t expectedCells = terrain.GetChunkCells();
    const float cellSize = (std::max)(surface.chunkCellSize, 0.01f);
    const float chunkSize = static_cast<float>(expectedCells) * cellSize;
    const std::string label = ChunkLabel(key);
    if (expectedCells == 0u || expectedCells > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)()) ||
        !std::isfinite(cellSize) || !std::isfinite(chunkSize) || chunkSize <= 0.0f) {
        diagnostic = "derived terrain scale is invalid";
        return false;
    }
    if (chunk.cells != expectedCells || !TERRAIN::TerrainChunkValid(chunk)) {
        diagnostic = label + " metadata or buffers differ from the surface lattice";
        return false;
    }

    const std::size_t vertexCount = TERRAIN::TerrainChunkVertexCount(chunk.cells);
    for (std::size_t index = 0u; index < vertexCount; ++index) {
        if (!std::isfinite(chunk.heights[index]) || !std::isfinite(chunk.coverage[index])) {
            diagnostic = label + " contains non-finite height or coverage data";
            return false;
        }
    }

    const int64_t minX = static_cast<int64_t>(key.x) * expectedCells;
    const int64_t minZ = static_cast<int64_t>(key.z) * expectedCells;
    const int64_t maxX = minX + expectedCells;
    const int64_t maxZ = minZ + expectedCells;
    constexpr int64_t latticeMin = static_cast<int64_t>((std::numeric_limits<int32_t>::min)()) + 1;
    constexpr int64_t latticeMax = static_cast<int64_t>((std::numeric_limits<int32_t>::max)()) - 1;
    if (minX < latticeMin || maxX > latticeMax || minZ < latticeMin || maxZ > latticeMax) {
        diagnostic = label + " exceeds the supported global lattice range";
        return false;
    }

    const double originX = static_cast<double>(minX) * cellSize;
    const double originZ = static_cast<double>(minZ) * cellSize;
    const double endX = static_cast<double>(maxX) * cellSize;
    const double endZ = static_cast<double>(maxZ) * cellSize;
    if (!std::isfinite(originX) || !std::isfinite(originZ) || !std::isfinite(endX) || !std::isfinite(endZ) ||
        std::fabs(originX) > (std::numeric_limits<float>::max)() ||
        std::fabs(originZ) > (std::numeric_limits<float>::max)() ||
        std::fabs(endX) > (std::numeric_limits<float>::max)() ||
        std::fabs(endZ) > (std::numeric_limits<float>::max)()) {
        diagnostic = label + " produces non-finite coordinates";
        return false;
    }
    const float firstX = static_cast<float>(originX);
    const float nextX = static_cast<float>(static_cast<double>(minX + 1) * cellSize);
    const float previousX = static_cast<float>(static_cast<double>(maxX - 1) * cellSize);
    const float lastX = static_cast<float>(endX);
    const float firstZ = static_cast<float>(originZ);
    const float nextZ = static_cast<float>(static_cast<double>(minZ + 1) * cellSize);
    const float previousZ = static_cast<float>(static_cast<double>(maxZ - 1) * cellSize);
    const float lastZ = static_cast<float>(endZ);
    if (!(firstX < nextX) || !(previousX < lastX) || !(firstZ < nextZ) || !(previousZ < lastZ)) {
        diagnostic = label + " lattice spacing is not representable in float coordinates";
        return false;
    }
    return true;
}

class TerrainGeometryBuilder final {
public:
    void AppendCoverageClippedTriangle(const CoverageVertex& first, const CoverageVertex& second,
                                       const CoverageVertex& third) {
        if (failed_) {
            return;
        }
        if (!ValidateVertex(first) || !ValidateVertex(second) || !ValidateVertex(third)) {
            Fail("generated terrain vertex is not finite");
            return;
        }

        constexpr float threshold = SAND::kSandSurfaceVisibleCoverageThreshold;
        const std::array<const CoverageVertex*, 3u> input{&first, &second, &third};
        std::array<uint32_t, 4u> polygon{};
        uint32_t count = 0u;
        const CoverageVertex* previous = input.back();
        bool previousInside = previous->coverage >= threshold;
        for (const CoverageVertex* current : input) {
            const bool currentInside = current->coverage >= threshold;
            if (currentInside != previousInside) {
                AppendUnique(polygon, count, GetOrCreateIntersectionVertex(*previous, *current));
            }
            if (currentInside) {
                AppendUnique(polygon, count, GetOrCreateSourceVertex(*current));
            }
            previous = current;
            previousInside = currentInside;
        }
        if (failed_) {
            return;
        }
        if (count > 1u && polygon.front() == polygon[count - 1u]) {
            --count;
        }
        if (count < 3u) {
            return;
        }
        for (uint32_t index = 1u; index + 1u < count; ++index) {
            AppendTriangle(polygon[0u], polygon[index], polygon[index + 1u]);
            if (failed_) {
                return;
            }
        }
    }

    bool Finalize(PhysicsGeometryBuffer& outGeometry, std::string& diagnostic) {
        if (failed_) {
            diagnostic = diagnostic_;
            return false;
        }
        if (geometry_.indices.empty()) {
            outGeometry = {};
            return true;
        }

        std::vector<uint32_t> remap(geometry_.vertices.size(), kInvalidVertexIndex);
        PhysicsGeometryBuffer compacted{};
        compacted.vertices.reserve(geometry_.vertices.size());
        compacted.indices.reserve(geometry_.indices.size());
        for (const uint32_t sourceIndex : geometry_.indices) {
            if (sourceIndex >= geometry_.vertices.size()) {
                diagnostic = "internal terrain index is out of range";
                return false;
            }
            uint32_t& targetIndex = remap[sourceIndex];
            if (targetIndex == kInvalidVertexIndex) {
                if (compacted.vertices.size() >= (std::numeric_limits<uint32_t>::max)()) {
                    diagnostic = "terrain collision exceeds uint32 vertex capacity";
                    return false;
                }
                targetIndex = static_cast<uint32_t>(compacted.vertices.size());
                compacted.vertices.push_back(geometry_.vertices[sourceIndex]);
            }
            compacted.indices.push_back(targetIndex);
        }
        outGeometry = std::move(compacted);
        return true;
    }

private:
    bool ValidateVertex(const CoverageVertex& vertex) const noexcept {
        return MATH::IsFinite(vertex.position) && std::isfinite(vertex.coverage);
    }

    void Fail(std::string diagnostic) {
        if (!failed_) {
            failed_ = true;
            diagnostic_ = std::move(diagnostic);
        }
    }

    uint32_t GetOrCreateSourceVertex(const CoverageVertex& vertex) {
        if (!ValidateVertex(vertex)) {
            Fail("source terrain vertex is not finite");
            return kInvalidVertexIndex;
        }
        const auto found = sourceVertices_.find(vertex.key);
        if (found != sourceVertices_.end()) {
            const SourceVertexRecord& record = found->second;
            if (record.index >= geometry_.vertices.size() ||
                !SameVertexValue(geometry_.vertices[record.index], vertex.position) ||
                record.coverage != vertex.coverage) {
                Fail("shared terrain lattice vertex has inconsistent "
                     "height or coverage");
                return kInvalidVertexIndex;
            }
            return record.index;
        }
        if (geometry_.vertices.size() >= (std::numeric_limits<uint32_t>::max)()) {
            Fail("terrain collision exceeds uint32 vertex capacity");
            return kInvalidVertexIndex;
        }
        const uint32_t index = static_cast<uint32_t>(geometry_.vertices.size());
        geometry_.vertices.push_back(vertex.position);
        sourceVertices_.emplace(vertex.key, SourceVertexRecord{index, vertex.coverage});
        return index;
    }

    uint32_t GetOrCreateIntersectionVertex(const CoverageVertex& first, const CoverageVertex& second) {
        const uint32_t firstIndex = GetOrCreateSourceVertex(first);
        const uint32_t secondIndex = GetOrCreateSourceVertex(second);
        if (failed_) {
            return kInvalidVertexIndex;
        }

        constexpr float threshold = SAND::kSandSurfaceVisibleCoverageThreshold;
        if (first.coverage == threshold) {
            return firstIndex;
        }
        if (second.coverage == threshold) {
            return secondIndex;
        }
        if (first.key == second.key) {
            Fail("coverage clip edge collapses to one lattice vertex");
            return kInvalidVertexIndex;
        }

        const SourceEdgeKey edge = MakeSourceEdgeKey(first.key, second.key);
        const CoverageVertex& low = LatticeKeyLess(first.key, second.key) ? first : second;
        const CoverageVertex& high = LatticeKeyLess(first.key, second.key) ? second : first;
        const double denominator = static_cast<double>(high.coverage) - low.coverage;
        if (!std::isfinite(denominator) || denominator == 0.0) {
            Fail("coverage clip edge has an invalid denominator");
            return kInvalidVertexIndex;
        }
        const double t = std::clamp((static_cast<double>(threshold) - low.coverage) / denominator, 0.0, 1.0);
        const MATH::Vec3 position{static_cast<float>(static_cast<double>(low.position.x) +
                                                     (static_cast<double>(high.position.x) - low.position.x) * t),
                                  static_cast<float>(static_cast<double>(low.position.y) +
                                                     (static_cast<double>(high.position.y) - low.position.y) * t),
                                  static_cast<float>(static_cast<double>(low.position.z) +
                                                     (static_cast<double>(high.position.z) - low.position.z) * t)};
        if (!MATH::IsFinite(position)) {
            Fail("coverage clip produced a non-finite vertex");
            return kInvalidVertexIndex;
        }

        const auto found = intersectionVertices_.find(edge);
        if (found != intersectionVertices_.end()) {
            if (found->second >= geometry_.vertices.size() ||
                !SameVertexValue(geometry_.vertices[found->second], position)) {
                Fail("shared coverage edge produced inconsistent "
                     "intersection positions");
                return kInvalidVertexIndex;
            }
            return found->second;
        }
        if (geometry_.vertices.size() >= (std::numeric_limits<uint32_t>::max)()) {
            Fail("terrain collision exceeds uint32 vertex capacity");
            return kInvalidVertexIndex;
        }
        const uint32_t index = static_cast<uint32_t>(geometry_.vertices.size());
        geometry_.vertices.push_back(position);
        intersectionVertices_.emplace(edge, index);
        return index;
    }

    void AppendUnique(std::array<uint32_t, 4u>& polygon, uint32_t& count, uint32_t index) {
        if (failed_ || index == kInvalidVertexIndex) {
            return;
        }
        if (count > 0u && polygon[count - 1u] == index) {
            return;
        }
        if (count >= polygon.size()) {
            Fail("coverage clip exceeded triangle polygon capacity");
            return;
        }
        polygon[count++] = index;
    }

    void AppendTriangle(uint32_t a, uint32_t b, uint32_t c) {
        if (a == b || b == c || c == a) {
            return;
        }
        if (a >= geometry_.vertices.size() || b >= geometry_.vertices.size() || c >= geometry_.vertices.size()) {
            Fail("generated terrain triangle references an invalid vertex");
            return;
        }

        const MATH::Vec3& first = geometry_.vertices[a];
        const MATH::Vec3& second = geometry_.vertices[b];
        const MATH::Vec3& third = geometry_.vertices[c];
        const double abX = static_cast<double>(second.x) - first.x;
        const double abY = static_cast<double>(second.y) - first.y;
        const double abZ = static_cast<double>(second.z) - first.z;
        const double acX = static_cast<double>(third.x) - first.x;
        const double acY = static_cast<double>(third.y) - first.y;
        const double acZ = static_cast<double>(third.z) - first.z;
        const double bcX = static_cast<double>(third.x) - second.x;
        const double bcY = static_cast<double>(third.y) - second.y;
        const double bcZ = static_cast<double>(third.z) - second.z;
        const double normalX = abY * acZ - abZ * acY;
        const double normalY = abZ * acX - abX * acZ;
        const double normalZ = abX * acY - abY * acX;
        const double areaSquared = normalX * normalX + normalY * normalY + normalZ * normalZ;
        const double maxEdgeSquared = (std::max)(
            {abX * abX + abY * abY + abZ * abZ, acX * acX + acY * acY + acZ * acZ, bcX * bcX + bcY * bcY + bcZ * bcZ});
        const double areaThreshold = (std::max)(1.0e-24, maxEdgeSquared * maxEdgeSquared * 1.0e-14);
        if (!std::isfinite(areaSquared) || !std::isfinite(maxEdgeSquared)) {
            Fail("generated terrain triangle area is not finite");
            return;
        }
        if (areaSquared <= areaThreshold || normalY == 0.0) {
            return;
        }
        if (normalY < 0.0) {
            Fail("generated terrain triangle has negative Y winding");
            return;
        }

        TriangleKey key{{a, b, c}};
        std::sort(key.indices.begin(), key.indices.end());
        if (!triangles_.insert(key).second) {
            return;
        }
        if (geometry_.indices.size() > geometry_.indices.max_size() - 3u) {
            Fail("terrain collision index buffer exceeds capacity");
            return;
        }
        geometry_.indices.push_back(a);
        geometry_.indices.push_back(b);
        geometry_.indices.push_back(c);
    }

    PhysicsGeometryBuffer geometry_{};
    std::unordered_map<AuthoredLatticeKey, SourceVertexRecord, AuthoredLatticeKeyHash> sourceVertices_{};
    std::unordered_map<SourceEdgeKey, uint32_t, SourceEdgeKeyHash> intersectionVertices_{};
    std::unordered_set<TriangleKey, TriangleKeyHash> triangles_{};
    bool failed_ = false;
    std::string diagnostic_{};
};

void AppendTerrainChunkGeometry(const TerrainSurfaceComponent& terrain, const TERRAIN::TerrainSurface& surface,
                                const TERRAIN::TerrainChunkKey& chunkKey, const TERRAIN::TerrainChunk& chunk,
                                TerrainGeometryBuilder& builder) {
    const uint32_t cells = terrain.GetChunkCells();
    const uint32_t stride = cells + 1u;
    const std::size_t vertexCount = static_cast<std::size_t>(stride) * stride;
    const float cellSize = (std::max)(surface.chunkCellSize, 0.01f);
    std::vector<CoverageVertex> vertices(vertexCount);
    const int64_t globalOriginX = static_cast<int64_t>(chunkKey.x) * cells;
    const int64_t globalOriginZ = static_cast<int64_t>(chunkKey.z) * cells;
    for (uint32_t z = 0u; z <= cells; ++z) {
        for (uint32_t x = 0u; x <= cells; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) * stride + x;
            const AuthoredLatticeKey lattice{globalOriginX + x, globalOriginZ + z};
            const float localX = static_cast<float>(static_cast<double>(lattice.x) * cellSize);
            const float localZ = static_cast<float>(static_cast<double>(lattice.z) * cellSize);
            const float coverage = terrain.SampleLocalCoverage({localX, 0.0f, localZ});
            float authoredHeight = chunk.heights[index];
            TERRAIN::TerrainCanonicalVertexSource canonical{};
            if (TERRAIN::TryResolveTerrainCanonicalVertex(surface.chunks, chunkKey, x, z, canonical) &&
                canonical.chunk != nullptr) {
                const uint32_t canonicalStride = canonical.chunk->cells + 1u;
                const std::size_t canonicalIndex =
                    static_cast<std::size_t>(canonical.localZ) * canonicalStride + canonical.localX;
                authoredHeight = canonical.chunk->heights[canonicalIndex];
            }
            const float effectiveHeight = SAND::EvaluateSandSurfaceEffectiveHeight(
                authoredHeight, coverage, surface.shoreline.edgeDepth, surface.deform.rimStrength);
            vertices[index] = CoverageVertex{lattice, {localX, effectiveHeight, localZ}, coverage};
        }
    }
    const auto at = [&](uint32_t x, uint32_t z) -> const CoverageVertex& {
        return vertices[static_cast<std::size_t>(z) * stride + x];
    };
    for (uint32_t z = 0u; z < cells; ++z) {
        for (uint32_t x = 0u; x < cells; ++x) {
            // +Y winding。Jolt / Recast の terrain projection は同じ
            // coverage-clipped topology を消費する。
            builder.AppendCoverageClippedTriangle(at(x, z), at(x, z + 1u), at(x + 1u, z));
            builder.AppendCoverageClippedTriangle(at(x + 1u, z), at(x, z + 1u), at(x + 1u, z + 1u));
        }
    }
}

bool FinalizeProjectionGeometry(TerrainGeometryBuilder& builder, TerrainCollisionProjection& result) {
    PhysicsGeometryBuffer geometry{};
    if (!builder.Finalize(geometry, result.diagnostic)) {
        return false;
    }
    if (geometry.indices.empty()) {
        result.buildSucceeded = true;
        return true;
    }
    const std::size_t triangleCount = geometry.indices.size() / 3u;
    if (triangleCount > (std::numeric_limits<uint32_t>::max)()) {
        result.diagnostic = "terrain collision exceeds uint32 triangle capacity";
        return false;
    }
    result.triangleCount = static_cast<uint32_t>(triangleCount);
    result.geometry = std::make_shared<PhysicsGeometryBuffer>(std::move(geometry));
    result.buildSucceeded = true;
    return true;
}

TerrainCollisionProjection BuildTerrainChunkCollisionProjectionImpl(RuntimeObjectHandle object,
                                                                    const TerrainSurfaceComponent& terrain,
                                                                    const TERRAIN::TerrainChunkKey& key) {
    TerrainCollisionProjection result = MakeProjectionBase(object, terrain);
    result.sourceRevision = terrain.GetChunkRevision(key);
    result.authoredRevision = result.sourceRevision;
    result.contentRevision = result.sourceRevision == (std::numeric_limits<uint64_t>::max)()
                                 ? (std::numeric_limits<uint64_t>::max)()
                                 : result.sourceRevision + 1u;
    const TERRAIN::TerrainSurface& surface = terrain.GetSurface();
    const auto found = surface.chunks.find(key);
    if (found == surface.chunks.end()) {
        result.buildSucceeded = true;
        return result;
    }
    if (!ValidateTerrainChunkInputOnly(terrain, key, found->second, result.diagnostic)) {
        return result;
    }
    TerrainGeometryBuilder builder{};
    AppendTerrainChunkGeometry(terrain, surface, key, found->second, builder);
    (void)FinalizeProjectionGeometry(builder, result);
    return result;
}
} // namespace

TerrainCollisionProjection BuildTerrainChunkCollisionProjection(RuntimeObjectHandle object,
                                                                const TerrainSurfaceComponent& terrain,
                                                                const TERRAIN::TerrainChunkKey& key) {
    try {
        return BuildTerrainChunkCollisionProjectionImpl(object, terrain, key);
    } catch (const std::exception& error) {
        TerrainCollisionProjection result = MakeProjectionBase(object, terrain);
        result.diagnostic = "terrain chunk collision build exception: " + std::string(error.what());
        return result;
    } catch (...) {
        TerrainCollisionProjection result = MakeProjectionBase(object, terrain);
        result.diagnostic = "terrain chunk collision build failed unexpectedly";
        return result;
    }
}

void TerrainCollisionProjectionCache::Refresh(World& world) {
    std::unordered_set<uint64_t> active{};
    world.ForEachObjectWith<TerrainSurfaceComponent>([&](GameObject& object, TerrainSurfaceComponent& terrain) {
        const RuntimeObjectHandle handle = object.GetRuntimeHandle();
        const uint64_t ownerKey = handle.ToValue();
        const uint64_t sourceRevision = terrain.GetCollisionRevision();
        active.insert(ownerKey);

        auto [ownerIt, inserted] = entries_.try_emplace(ownerKey);
        OwnerCacheEntry& owner = ownerIt->second;
        if (!inserted && owner.projection.object == handle && owner.sourceRevision == sourceRevision) {
            return;
        }

        const TERRAIN::TerrainSurface& surface = terrain.GetSurface();
        std::vector<TERRAIN::TerrainChunkKey> changedKeys{};
        bool fullRebuild = inserted || owner.projection.object != handle;
        if (!fullRebuild) {
            fullRebuild = !terrain.TryGetChangedChunkKeys(owner.chunkChangeRevision, changedKeys) ||
                          (changedKeys.empty() && owner.sourceRevision != sourceRevision);
        }

        std::set<TERRAIN::TerrainChunkKey> dirtyKeys{};
        if (fullRebuild) {
            for (const auto& [chunkKey, chunk] : surface.chunks) {
                (void)chunk;
                dirtyKeys.insert(chunkKey);
            }
            for (const auto& [chunkKey, shape] : owner.chunks) {
                (void)shape;
                dirtyKeys.insert(chunkKey);
            }
        } else {
            const float chunkSize = terrain.GetChunkWorldSize();
            const float falloff = (std::max)(surface.shoreline.edgeFalloff, surface.chunkCellSize);
            const int32_t dependencyRadius =
                (std::max)(1, static_cast<int32_t>(std::ceil(falloff / (std::max)(chunkSize, 1.0e-4f))));
            for (const TERRAIN::TerrainChunkKey changed : changedKeys) {
                for (int32_t dz = -dependencyRadius; dz <= dependencyRadius; ++dz) {
                    for (int32_t dx = -dependencyRadius; dx <= dependencyRadius; ++dx) {
                        const int64_t candidateX = static_cast<int64_t>(changed.x) + dx;
                        const int64_t candidateZ = static_cast<int64_t>(changed.z) + dz;
                        if (candidateX < (std::numeric_limits<int32_t>::min)() ||
                            candidateX > (std::numeric_limits<int32_t>::max)() ||
                            candidateZ < (std::numeric_limits<int32_t>::min)() ||
                            candidateZ > (std::numeric_limits<int32_t>::max)()) {
                            continue;
                        }
                        dirtyKeys.insert({
                            static_cast<int32_t>(candidateX),
                            static_cast<int32_t>(candidateZ),
                        });
                    }
                }
            }
        }

        bool buildFailed = false;
        std::string firstDiagnostic{};
        for (const TERRAIN::TerrainChunkKey chunkKey : dirtyKeys) {
            const auto authored = surface.chunks.find(chunkKey);
            if (authored == surface.chunks.end()) {
                owner.chunks.erase(chunkKey);
                owner.rejectedChunkRevisions.erase(chunkKey);
                continue;
            }
            uint64_t attemptRevision = sourceRevision;
            const uint64_t chunkRevision = terrain.GetChunkRevision(chunkKey);
            attemptRevision ^=
                chunkRevision + 0x9E3779B97F4A7C15ull + (attemptRevision << 6u) + (attemptRevision >> 2u);
            const auto rejected = owner.rejectedChunkRevisions.find(chunkKey);
            if (rejected != owner.rejectedChunkRevisions.end() && rejected->second == attemptRevision) {
                buildFailed = true;
                continue;
            }

            TerrainCollisionProjection chunkProjection =
                BuildTerrainChunkCollisionProjection(handle, terrain, chunkKey);
            if (!chunkProjection.buildSucceeded) {
                buildFailed = true;
                owner.rejectedChunkRevisions.insert_or_assign(chunkKey, attemptRevision);
                if (firstDiagnostic.empty()) {
                    firstDiagnostic = chunkProjection.diagnostic;
                }
                HIKARI_LOG_WARN("[TerrainCollision] chunk projection rejected object=" + std::to_string(ownerKey) +
                                " chunk=(" + std::to_string(chunkKey.x) + "," + std::to_string(chunkKey.z) +
                                ") revision=" + std::to_string(sourceRevision) + " reason=" +
                                (chunkProjection.diagnostic.empty() ? std::string("unknown build failure")
                                                                    : chunkProjection.diagnostic));
                // 当該 chunk の既存 shape は last-known-good として保持。
                continue;
            }

            owner.rejectedChunkRevisions.erase(chunkKey);
            if (chunkProjection.geometry == nullptr || chunkProjection.triangleCount == 0u) {
                owner.chunks.erase(chunkKey);
                continue;
            }
            TerrainCollisionChunkShape shape{};
            shape.key = chunkKey;
            shape.sourceRevision = chunkRevision;
            shape.contentRevision = attemptRevision != 0u ? attemptRevision : 1u;
            shape.triangleCount = chunkProjection.triangleCount;
            shape.geometry = std::move(chunkProjection.geometry);
            owner.chunks.insert_or_assign(chunkKey, std::move(shape));
        }

        for (auto it = owner.chunks.begin(); it != owner.chunks.end();) {
            if (surface.chunks.contains(it->first)) {
                ++it;
            } else {
                owner.rejectedChunkRevisions.erase(it->first);
                it = owner.chunks.erase(it);
            }
        }

        TerrainCollisionProjection projection = MakeProjectionBase(handle, terrain);
        projection.sourceRevision = sourceRevision;
        projection.authoredRevision = terrain.GetGeometryRevision();
        projection.buildSucceeded = !buildFailed;
        projection.diagnostic =
            buildFailed ? (firstDiagnostic.empty()
                               ? std::string("one or more terrain chunks retained last-known-good collision")
                               : firstDiagnostic)
                        : std::string{};
        projection.chunkShapes.reserve(owner.chunks.size());
        std::size_t vertexCount = 0u;
        uint64_t contentRevision = sourceRevision != 0u ? sourceRevision : 1u;
        for (const auto& [chunkKey, shape] : owner.chunks) {
            projection.chunkShapes.push_back(shape);
            if (projection.triangleCount <= (std::numeric_limits<uint32_t>::max)() - shape.triangleCount) {
                projection.triangleCount += shape.triangleCount;
            } else {
                projection.triangleCount = (std::numeric_limits<uint32_t>::max)();
            }
            if (shape.geometry != nullptr) {
                vertexCount += shape.geometry->vertices.size();
            }
            contentRevision ^= static_cast<uint64_t>(static_cast<uint32_t>(chunkKey.x)) + 0x9E3779B97F4A7C15ull +
                               (contentRevision << 6u) + (contentRevision >> 2u);
            contentRevision ^= static_cast<uint64_t>(static_cast<uint32_t>(chunkKey.z)) + shape.contentRevision +
                               (contentRevision << 6u) + (contentRevision >> 2u);
        }
        projection.contentRevision = contentRevision != 0u ? contentRevision : 1u;
        owner.projection = std::move(projection);
        owner.sourceRevision = sourceRevision;
        owner.chunkChangeRevision = terrain.GetChunkChangeRevision();

        HIKARI_LOG_INFO("[TerrainCollision] object=" + std::to_string(ownerKey) +
                        " shapes=" + std::to_string(owner.projection.chunkShapes.size()) + " vertices=" +
                        std::to_string(vertexCount) + " triangles=" + std::to_string(owner.projection.triangleCount) +
                        " revision=" + std::to_string(owner.projection.authoredRevision));
    });

    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!active.contains(it->first)) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

const TerrainCollisionProjection* TerrainCollisionProjectionCache::Find(RuntimeObjectHandle object) const noexcept {
    const auto found = entries_.find(object.ToValue());
    return found != entries_.end() ? &found->second.projection : nullptr;
}

void TerrainCollisionProjectionCache::Clear() noexcept { entries_.clear(); }

} // namespace HIKARI::PHYSICS
