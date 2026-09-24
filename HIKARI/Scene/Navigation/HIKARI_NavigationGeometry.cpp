#include "HIKARI_NavigationBuild.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "Physics/HIKARI_TerrainCollisionProjection.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI::NAVIGATION::BUILD {
constexpr uint64_t kHashOffset = 1469598103934665603ull;
constexpr uint64_t kHashPrime = 1099511628211ull;

uint64_t HashValue(uint64_t hash, uint64_t value) noexcept {
    for (uint32_t byte = 0u; byte < 8u; ++byte) {
        hash ^= (value >> (byte * 8u)) & 0xFFu;
        hash *= kHashPrime;
    }
    return hash;
}

uint64_t HashFloat(uint64_t hash, float value) noexcept {
    uint32_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return HashValue(hash, bits);
}

uint64_t ComputeTerrainObjectSignature(const TerrainSurfaceComponent& terrain) noexcept {
    uint64_t hash = kHashOffset;
    constexpr std::array<MATH::Vec3, 4u> probes{MATH::Vec3{0.0f, 0.0f, 0.0f}, MATH::Vec3{1.0f, 0.0f, 0.0f},
                                                MATH::Vec3{0.0f, 1.0f, 0.0f}, MATH::Vec3{0.0f, 0.0f, 1.0f}};
    for (const MATH::Vec3& probe : probes) {
        const MATH::Vec3 world = terrain.LocalToWorld(probe);
        hash = HashFloat(hash, world.x);
        hash = HashFloat(hash, world.y);
        hash = HashFloat(hash, world.z);
    }
    hash = HashFloat(hash, terrain.GetChunkWorldSize());
    return HashValue(hash, terrain.GetChunkCells());
}

NavigationSourceStamp GatherNavigationSourceStamp(World& world) {
    NavigationSourceStamp result{};
    world.ForEachObjectWith<TerrainSurfaceComponent>([&](GameObject& object, TerrainSurfaceComponent& terrain) {
        result.hasTerrain = true;
        const RuntimeObjectHandle handle = object.GetRuntimeHandle();
        const uint64_t objectValue = handle.ToValue();
        result.objectSignatures[objectValue] = ComputeTerrainObjectSignature(terrain);
        result.collisionRevisions[objectValue] = terrain.GetCollisionRevision();
    });

    uint64_t hash = kHashOffset;
    for (const auto& [object, signature] : result.objectSignatures) {
        hash = HashValue(hash, object);
        hash = HashValue(hash, signature);
        const auto collision = result.collisionRevisions.find(object);
        hash = HashValue(hash, collision != result.collisionRevisions.end() ? collision->second : 0u);
    }
    result.revision = result.hasTerrain ? hash : 0u;
    return result;
}

CurrentNavigationSources GatherCurrentSources(World& world, const NavigationSourceStamp& stamp) {
    CurrentNavigationSources result{};
    world.ForEachObjectWith<TerrainSurfaceComponent>([&](GameObject& object, TerrainSurfaceComponent& terrain) {
        const RuntimeObjectHandle handle = object.GetRuntimeHandle();
        const uint64_t objectValue = handle.ToValue();
        for (const auto& [key, chunk] : terrain.GetChunks()) {
            (void)chunk;
            const NavigationChunkKey navigationKey{objectValue, key.x, key.z};
            result.chunks.emplace(navigationKey,
                                  NavigationChunkSource{handle, &terrain, key, terrain.GetChunkRevision(key)});
        }
    });

    uint64_t hash = stamp.revision;
    for (const auto& [key, source] : result.chunks) {
        hash = HashValue(hash, key.object);
        hash = HashValue(hash, static_cast<uint64_t>(static_cast<int64_t>(key.x)));
        hash = HashValue(hash, static_cast<uint64_t>(static_cast<int64_t>(key.z)));
        hash = HashValue(hash, source.revision);
    }
    result.revision = stamp.hasTerrain ? hash : 0u;
    return result;
}

bool AppendTerrainGeometry(const TerrainSurfaceComponent& terrain,
                           const PHYSICS::TerrainCollisionProjection& projection, NavigationGeometry& out,
                           std::string& outDiagnostic) {
    if (projection.geometry == nullptr || projection.geometry->indices.empty()) {
        return true;
    }
    const MATH::Vec3 worldOrigin = terrain.LocalToWorld({});
    const MATH::Vec3 worldX = terrain.LocalToWorld({1.0f, 0.0f, 0.0f}) - worldOrigin;
    const MATH::Vec3 worldY = terrain.LocalToWorld({0.0f, 1.0f, 0.0f}) - worldOrigin;
    const MATH::Vec3 worldZ = terrain.LocalToWorld({0.0f, 0.0f, 1.0f}) - worldOrigin;
    const float determinant = MATH::Dot(MATH::Cross(worldX, worldY), worldZ);
    if (!std::isfinite(determinant) || std::fabs(determinant) <= 1.0e-8f) {
        outDiagnostic = "navigation terrain transform is singular or non-finite";
        return false;
    }
    const bool reverseWinding = determinant < 0.0f;
    if (projection.geometry->vertices.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        out.vertices.size() / 3u >
            static_cast<std::size_t>((std::numeric_limits<int>::max)()) - projection.geometry->vertices.size()) {
        outDiagnostic = "navigation terrain exceeds Recast int vertex capacity";
        return false;
    }

    const int baseVertex = static_cast<int>(out.vertices.size() / 3u);
    out.vertices.reserve(out.vertices.size() + projection.geometry->vertices.size() * 3u);
    for (const MATH::Vec3& local : projection.geometry->vertices) {
        const MATH::Vec3 world = terrain.LocalToWorld(local);
        if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
            outDiagnostic = "navigation terrain contains a non-finite world vertex";
            return false;
        }
        out.vertices.push_back(world.x);
        out.vertices.push_back(world.y);
        out.vertices.push_back(world.z);
        out.boundsMin[0] = (std::min)(out.boundsMin[0], world.x);
        out.boundsMin[1] = (std::min)(out.boundsMin[1], world.y);
        out.boundsMin[2] = (std::min)(out.boundsMin[2], world.z);
        out.boundsMax[0] = (std::max)(out.boundsMax[0], world.x);
        out.boundsMax[1] = (std::max)(out.boundsMax[1], world.y);
        out.boundsMax[2] = (std::max)(out.boundsMax[2], world.z);
    }
    out.triangles.reserve(out.triangles.size() + projection.geometry->indices.size());
    for (std::size_t triangle = 0u; triangle < projection.geometry->indices.size(); triangle += 3u) {
        const std::array<uint32_t, 3u> indices{projection.geometry->indices[triangle],
                                               projection.geometry->indices[triangle + 1u],
                                               projection.geometry->indices[triangle + 2u]};
        for (const uint32_t index : indices) {
            if (index < projection.geometry->vertices.size()) {
                continue;
            }
            outDiagnostic = "navigation terrain contains an out-of-range index";
            return false;
        }
        out.triangles.push_back(baseVertex + static_cast<int>(indices[0]));
        out.triangles.push_back(baseVertex + static_cast<int>(indices[reverseWinding ? 2u : 1u]));
        out.triangles.push_back(baseVertex + static_cast<int>(indices[reverseWinding ? 1u : 2u]));
    }
    return true;
}

bool AppendNavigationGeometry(const NavigationGeometry& source, NavigationGeometry& out, std::string& outDiagnostic) {
    if (source.triangles.empty()) {
        return true;
    }
    const std::size_t sourceVertexCount = source.vertices.size() / 3u;
    const std::size_t outputVertexCount = out.vertices.size() / 3u;
    if (source.vertices.size() % 3u != 0u || source.triangles.size() % 3u != 0u ||
        sourceVertexCount > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        outputVertexCount > static_cast<std::size_t>((std::numeric_limits<int>::max)()) - sourceVertexCount) {
        outDiagnostic = "navigation chunk geometry exceeds Recast int capacity";
        return false;
    }
    const int baseVertex = static_cast<int>(outputVertexCount);
    out.vertices.insert(out.vertices.end(), source.vertices.begin(), source.vertices.end());
    out.triangles.reserve(out.triangles.size() + source.triangles.size());
    for (const int index : source.triangles) {
        if (index < 0 || static_cast<std::size_t>(index) >= sourceVertexCount) {
            outDiagnostic = "navigation chunk contains an out-of-range index";
            return false;
        }
        out.triangles.push_back(baseVertex + index);
    }
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
        out.boundsMin[axis] = (std::min)(out.boundsMin[axis], source.boundsMin[axis]);
        out.boundsMax[axis] = (std::max)(out.boundsMax[axis], source.boundsMax[axis]);
    }
    return true;
}

bool BuildChunkGeometry(const NavigationChunkSource& source, NavigationGeometry& out, std::string& outDiagnostic) {
    if (source.terrain == nullptr) {
        outDiagnostic = "navigation chunk lost its terrain owner";
        return false;
    }
    const PHYSICS::TerrainCollisionProjection projection =
        PHYSICS::BuildTerrainChunkCollisionProjection(source.object, *source.terrain, source.key);
    if (!projection.buildSucceeded) {
        outDiagnostic =
            projection.diagnostic.empty() ? "terrain chunk collision projection failed" : projection.diagnostic;
        return false;
    }
    return AppendTerrainGeometry(*source.terrain, projection, out, outDiagnostic);
}

bool GatherGeometry(const CurrentNavigationSources& sources, NavigationGeometry& out, NavigationChunkCache& outChunks,
                    std::string& outDiagnostic) {
    for (const auto& [key, source] : sources.chunks) {
        CachedNavigationChunk cached{};
        cached.revision = source.revision;
        if (!BuildChunkGeometry(source, cached.geometry, outDiagnostic) ||
            !AppendNavigationGeometry(cached.geometry, out, outDiagnostic)) {
            return false;
        }
        outChunks.emplace(key, std::move(cached));
    }
    if (out.triangles.size() % 3u != 0u) {
        outDiagnostic = "navigation triangle stream is not divisible by three";
        return false;
    }
    return true;
}

} // namespace HIKARI::NAVIGATION::BUILD
