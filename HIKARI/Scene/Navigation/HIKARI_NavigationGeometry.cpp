#include "HIKARI_NavigationBuild.h"

#include <algorithm>
#include "HIKARI/Project/NavigationSource.h"
#include "Scene/HIKARI_World.h"

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

NavigationSourceStamp GatherNavigationSourceStamp(World& world) {
    NavigationSourceStamp result{};
    for (const INavigationGeometrySource* source : world.GetNavigationGeometrySources()) {
        result.hasSources = true;
        const uint64_t sourceValue = reinterpret_cast<uintptr_t>(source);
        const uint32_t chunkCount = source->GetNavigationChunkCount();
        uint64_t revisions = kHashOffset;
        for (uint32_t index = 0u; index < chunkCount; ++index) {
            int32_t x = 0;
            int32_t z = 0;
            uint64_t revision = 0u;
            if (source->GetNavigationChunk(index, x, z, revision)) {
                revisions = HashValue(revisions, revision);
            }
        }
        result.objectSignatures[sourceValue] = chunkCount;
        result.collisionRevisions[sourceValue] = revisions;
    }

    uint64_t hash = kHashOffset;
    for (const auto& [object, signature] : result.objectSignatures) {
        hash = HashValue(hash, object);
        hash = HashValue(hash, signature);
        const auto collision = result.collisionRevisions.find(object);
        hash = HashValue(hash, collision != result.collisionRevisions.end() ? collision->second : 0u);
    }
    result.revision = result.hasSources ? hash : 0u;
    return result;
}

CurrentNavigationSources GatherCurrentSources(World& world, const NavigationSourceStamp& stamp) {
    CurrentNavigationSources result{};
    for (const INavigationGeometrySource* source : world.GetNavigationGeometrySources()) {
        const uint64_t sourceValue = reinterpret_cast<uintptr_t>(source);
        const uint32_t chunkCount = source->GetNavigationChunkCount();
        for (uint32_t index = 0u; index < chunkCount; ++index) {
            int32_t x = 0;
            int32_t z = 0;
            uint64_t revision = 0u;
            if (source->GetNavigationChunk(index, x, z, revision)) {
                result.chunks.emplace(NavigationChunkKey{sourceValue, x, z},
                                      NavigationChunkSource{source, {x, z}, revision});
            }
        }
    }

    uint64_t hash = stamp.revision;
    for (const auto& [key, source] : result.chunks) {
        hash = HashValue(hash, key.object);
        hash = HashValue(hash, static_cast<uint64_t>(static_cast<int64_t>(key.x)));
        hash = HashValue(hash, static_cast<uint64_t>(static_cast<int64_t>(key.z)));
        hash = HashValue(hash, source.revision);
    }
    result.revision = stamp.hasSources ? hash : 0u;
    return result;
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

namespace {

class ModuleGeometrySink final : public INavigationGeometrySink {
public:
    explicit ModuleGeometrySink(NavigationGeometry& out) : out_(out) {}
    void Append(std::span<const float> xyz, std::span<const int32_t> triangles) override {
        const int baseVertex = static_cast<int>(out_.vertices.size() / 3u);
        out_.vertices.insert(out_.vertices.end(), xyz.begin(), xyz.end());
        for (const int32_t index : triangles) {
            out_.triangles.push_back(baseVertex + index);
        }
        for (size_t offset = 0u; offset + 2u < xyz.size(); offset += 3u) {
            for (size_t axis = 0u; axis < 3u; ++axis) {
                out_.boundsMin[axis] = (std::min)(out_.boundsMin[axis], xyz[offset + axis]);
                out_.boundsMax[axis] = (std::max)(out_.boundsMax[axis], xyz[offset + axis]);
            }
        }
    }

private:
    NavigationGeometry& out_;
};

} // namespace

bool BuildChunkGeometry(const NavigationChunkSource& source, NavigationGeometry& out, std::string& outDiagnostic) {
    if (source.moduleSource == nullptr) {
        outDiagnostic = "navigation chunk lost its source";
        return false;
    }
    ModuleGeometrySink sink(out);
    source.moduleSource->AppendNavigationChunk(source.key.x, source.key.z, sink);
    return true;
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
