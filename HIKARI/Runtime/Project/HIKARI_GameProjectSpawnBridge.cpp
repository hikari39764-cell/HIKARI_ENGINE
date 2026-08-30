#include "HIKARI/Project/WorldView.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectTransformBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {

    static_assert(kWorldSpawnBatchMaximumCapacity ==
        kGameProjectWorldMaximumSpawnBatchCount);
    static_assert(kWorldSpawnMaximumComponentsPerObject ==
        kGameProjectWorldMaximumSpawnComponentsPerObject);
    static_assert(kWorldSpawnMaximumComponentPayloadBytes ==
        kGameProjectWorldMaximumSpawnComponentBytes);
    static_assert(kWorldSpawnMaximumBatchPayloadBytes ==
        kGameProjectWorldMaximumSpawnBatchBytes);

    bool TryReadObjectName(
        const GameProjectSpawnObjectDesc& desc,
        std::string_view& outName) noexcept {
        const void* terminator = std::memchr(
            desc.name,
            '\0',
            kGameProjectObjectNameCapacity);
        if (terminator == nullptr) {
            return false;
        }
        const auto* end = static_cast<const char*>(terminator);
        if (end == desc.name) {
            return false;
        }
        outName = std::string_view(
            desc.name,
            static_cast<size_t>(end - desc.name));
        return true;
    }

    template<size_t Count>
    bool HasNonZeroReserved(const uint32_t (&values)[Count]) noexcept {
        for (const uint32_t value : values) {
            if (value != 0u) {
                return true;
            }
        }
        return false;
    }

    GameProjectWorldResultCode TryBuildBatchSpawnRequest(
        World& world,
        const GameProjectSpawnObjectDesc& desc,
        std::span<const WorldSpawnComponentRequest> components,
        WorldSpawnRequest& outRequest) noexcept {
        if (HasNonZeroReserved(desc.reserved)) {
            return GameProjectWorldResultCode::InvalidRequest;
        }
        std::string_view name{};
        if (!TryReadObjectName(desc, name)) {
            return GameProjectWorldResultCode::InvalidName;
        }
        if (!PROJECT::INTERNAL::IsValidProjectTransform(
                desc.localTransform)) {
            return GameProjectWorldResultCode::InvalidTransform;
        }
        RuntimeObjectHandle parent{};
        if (!desc.parent.IsNull()) {
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(
                    desc.parent,
                    world)) {
                return GameProjectWorldResultCode::InvalidParent;
            }
            parent = PROJECT::INTERNAL::DecodeRuntimeHandle(desc.parent);
            if (world.FindObject(parent) == nullptr) {
                return GameProjectWorldResultCode::InvalidParent;
            }
        }
        const Transform3D transform =
            PROJECT::INTERNAL::ToRuntimeTransform(desc.localTransform);
        outRequest = WorldSpawnRequest{
            name,
            WorldCommandLocalTransform{
                transform.position,
                transform.rotation,
                transform.scale
            },
            parent,
            components
        };
        return GameProjectWorldResultCode::Success;
    }

    bool SubmitPreparedBatch(
        World& world,
        std::span<const WorldSpawnRequest> requests,
        std::span<GameProjectWorldResult> outResults) noexcept {
        auto fillResults = [&](GameProjectWorldResult result) noexcept {
            for (GameProjectWorldResult& output : outResults) {
                output = result;
            }
        };
        std::array<WorldCommandSubmission,
            kGameProjectWorldMaximumSpawnBatchCount> submissions{};
        try {
            const WorldMutationResult admission = world.SubmitSpawnBatch(
                requests,
                std::span<WorldCommandSubmission>(
                    submissions.data(),
                    requests.size()));
            if (admission != WorldMutationResult::Queued) {
                fillResults(PROJECT::INTERNAL::ToProjectMutationResult(
                    world,
                    admission));
                return false;
            }
            for (size_t index = 0u; index < requests.size(); ++index) {
                outResults[index] =
                    PROJECT::INTERNAL::ToProjectCommandSubmission(
                        world,
                        submissions[index]);
            }
            return true;
        }
        catch (...) {
            fillResults(PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError));
            return false;
        }
    }

    GameProjectWorldResult SubmitSingleSpawn(
        World& world,
        const GameProjectSpawnObjectDesc& desc) noexcept {
        if (HasNonZeroReserved(desc.reserved)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest);
        }
        std::string_view name{};
        if (!TryReadObjectName(desc, name)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidName);
        }
        if (!PROJECT::INTERNAL::IsValidProjectTransform(
                desc.localTransform)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidTransform);
        }
        RuntimeObjectHandle parent{};
        if (!desc.parent.IsNull()) {
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(
                    desc.parent,
                    world)) {
                return PROJECT::INTERNAL::MakeWorldResult(
                    GameProjectWorldResultCode::InvalidParent);
            }
            parent = PROJECT::INTERNAL::DecodeRuntimeHandle(desc.parent);
        }
        try {
            return PROJECT::INTERNAL::ToProjectCommandSubmission(
                world,
                world.SubmitSpawn(
                    name,
                    PROJECT::INTERNAL::ToRuntimeTransform(
                        desc.localTransform),
                    parent));
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError);
        }
    }

} // namespace

    GameProjectWorldResult WorldView::SpawnObject(
        const GameProjectSpawnObjectDesc& desc) const noexcept {
        RuntimeWorldAccessResult access{};
        World* world = GameProjectSdkAccess::GetWorld(*this, &access);
        return world != nullptr
            ? SubmitSingleSpawn(*world, desc)
            : PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }

    bool WorldView::SpawnObjects(
        std::span<const GameProjectSpawnObjectDesc> objects,
        std::span<GameProjectWorldResult> outResults) const noexcept {
        if (objects.size() != outResults.size()) {
            return false;
        }
        auto fillResults = [&](GameProjectWorldResult result) noexcept {
            for (GameProjectWorldResult& output : outResults) {
                output = result;
            }
        };
        if (objects.size() > kGameProjectWorldMaximumSpawnBatchCount) {
            fillResults(PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest));
            return false;
        }
        if (objects.empty()) {
            return true;
        }
        RuntimeWorldAccessResult access{};
        World* world = GameProjectSdkAccess::GetWorld(*this, &access);
        if (world == nullptr) {
            fillResults(PROJECT::INTERNAL::MakeWorldAccessFailure(access));
            return false;
        }
        std::array<WorldSpawnRequest,
            kGameProjectWorldMaximumSpawnBatchCount> requests{};
        for (size_t index = 0u; index < objects.size(); ++index) {
            const GameProjectWorldResultCode rejection =
                TryBuildBatchSpawnRequest(
                    *world,
                    objects[index],
                    {},
                    requests[index]);
            if (rejection != GameProjectWorldResultCode::Success) {
                fillResults(PROJECT::INTERNAL::MakeWorldResult(
                    GameProjectWorldResultCode::MutationRejected));
                outResults[index] =
                    PROJECT::INTERNAL::MakeWorldResult(rejection);
                return false;
            }
        }
        return SubmitPreparedBatch(
            *world,
            std::span<const WorldSpawnRequest>(
                requests.data(), objects.size()),
            outResults);
    }

    bool WorldView::SpawnObjectsWithComponents(
        const GameProjectSpawnObjectComponentsDesc* objects,
        uint32_t objectCount,
        GameProjectWorldResult* outResults,
        uint32_t resultCount) const noexcept {
        if (objectCount != resultCount) {
            return false;
        }
        if (objectCount == 0u) {
            return true;
        }
        const auto fillResults = [&](GameProjectWorldResult result) noexcept {
            if (outResults != nullptr) {
                for (uint32_t index = 0u; index < resultCount; ++index) {
                    outResults[index] = result;
                }
            }
        };
        if (objects == nullptr || outResults == nullptr ||
            objectCount > kGameProjectWorldMaximumSpawnBatchCount) {
            fillResults(PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest));
            return false;
        }
        RuntimeWorldAccessResult access{};
        World* world = GameProjectSdkAccess::GetWorld(*this, &access);
        if (world == nullptr) {
            fillResults(PROJECT::INTERNAL::MakeWorldAccessFailure(access));
            return false;
        }

        std::array<WorldSpawnRequest,
            kGameProjectWorldMaximumSpawnBatchCount> requests{};
        std::array<size_t,
            kGameProjectWorldMaximumSpawnBatchCount> componentOffsets{};
        std::array<uint32_t,
            kGameProjectWorldMaximumSpawnBatchCount> componentCounts{};
        std::vector<WorldSpawnComponentRequest> components{};
        try {
            size_t totalComponents = 0u;
            size_t totalBytes = 0u;
            for (uint32_t objectIndex = 0u;
                objectIndex < objectCount;
                ++objectIndex) {
                const auto& object = objects[objectIndex];
                GameProjectWorldResultCode rejection =
                    GameProjectWorldResultCode::Success;
                if (object.version != kGameProjectSpawnComponentVersion) {
                    rejection = GameProjectWorldResultCode::Unsupported;
                }
                else if (object.structSize !=
                        kGameProjectSpawnObjectComponentsDescV1Size ||
                    HasNonZeroReserved(object.reserved) ||
                    object.componentCount >
                        kGameProjectWorldMaximumSpawnComponentsPerObject ||
                    (object.componentCount != 0u &&
                     object.components == nullptr)) {
                    rejection = GameProjectWorldResultCode::InvalidRequest;
                }
                if (rejection != GameProjectWorldResultCode::Success) {
                    fillResults(PROJECT::INTERNAL::MakeWorldResult(
                        GameProjectWorldResultCode::MutationRejected));
                    outResults[objectIndex] =
                        PROJECT::INTERNAL::MakeWorldResult(rejection);
                    return false;
                }
                componentOffsets[objectIndex] = totalComponents;
                componentCounts[objectIndex] = object.componentCount;
                totalComponents += object.componentCount;
                components.reserve(totalComponents);
                for (uint32_t componentIndex = 0u;
                    componentIndex < object.componentCount;
                    ++componentIndex) {
                    const auto& component =
                        object.components[componentIndex];
                    if (component.version !=
                        kGameProjectSpawnComponentVersion) {
                        rejection = GameProjectWorldResultCode::Unsupported;
                    }
                    else if (component.structSize !=
                            kGameProjectSpawnComponentDescV1Size ||
                        !component.typeId.IsValid() ||
                        component.initialValuesFormat !=
                            GameProjectSpawnComponentValuesFormat::
                                JsonUtf8Object ||
                        component.initialValuesSize >
                            kGameProjectWorldMaximumSpawnComponentBytes ||
                        (component.initialValuesSize != 0u &&
                         component.initialValues == nullptr) ||
                        HasNonZeroReserved(component.reserved) ||
                        totalBytes >
                            kGameProjectWorldMaximumSpawnBatchBytes -
                                component.initialValuesSize) {
                        rejection =
                            GameProjectWorldResultCode::InvalidRequest;
                    }
                    if (rejection != GameProjectWorldResultCode::Success) {
                        fillResults(PROJECT::INTERNAL::MakeWorldResult(
                            GameProjectWorldResultCode::MutationRejected));
                        outResults[objectIndex] =
                            PROJECT::INTERNAL::MakeWorldResult(rejection);
                        return false;
                    }
                    totalBytes += component.initialValuesSize;
                    components.push_back(WorldSpawnComponentRequest{
                        component.typeId,
                        component.initialValuesSize != 0u
                            ? std::string_view(
                                reinterpret_cast<const char*>(
                                    component.initialValues),
                                component.initialValuesSize)
                            : std::string_view{}
                    });
                }
            }
            for (uint32_t objectIndex = 0u;
                objectIndex < objectCount;
                ++objectIndex) {
                const uint32_t count = componentCounts[objectIndex];
                const std::span<const WorldSpawnComponentRequest> values =
                    count != 0u
                    ? std::span<const WorldSpawnComponentRequest>(
                        components.data() + componentOffsets[objectIndex],
                        count)
                    : std::span<const WorldSpawnComponentRequest>{};
                const GameProjectWorldResultCode rejection =
                    TryBuildBatchSpawnRequest(
                        *world,
                        objects[objectIndex].object,
                        values,
                        requests[objectIndex]);
                if (rejection != GameProjectWorldResultCode::Success) {
                    fillResults(PROJECT::INTERNAL::MakeWorldResult(
                        GameProjectWorldResultCode::MutationRejected));
                    outResults[objectIndex] =
                        PROJECT::INTERNAL::MakeWorldResult(rejection);
                    return false;
                }
            }
        }
        catch (...) {
            fillResults(PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError));
            return false;
        }
        return SubmitPreparedBatch(
            *world,
            std::span<const WorldSpawnRequest>(
                requests.data(), objectCount),
            std::span<GameProjectWorldResult>(outResults, resultCount));
    }

} // namespace HIKARI
