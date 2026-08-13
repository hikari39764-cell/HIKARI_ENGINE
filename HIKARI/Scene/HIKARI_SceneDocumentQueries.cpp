#include "Scene/HIKARI_SceneDocumentQueries.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneComponentId.h"

namespace HIKARI {

    SceneObjectId AllocateSceneObjectId(
        const SceneDocument& document) {

        std::unordered_set<uint64_t> usedIds{};
        uint64_t maximumId = 0u;
        for (const SceneObjectData& object : document.objects) {
            if (object.id.value == 0u) {
                continue;
            }
            usedIds.insert(object.id.value);
            maximumId = (std::max)(maximumId, object.id.value);
        }
        uint64_t candidate =
            maximumId == (std::numeric_limits<uint64_t>::max)()
                ? 1u
                : maximumId + 1u;
        while (candidate == 0u || usedIds.contains(candidate)) {
            ++candidate;
            if (candidate == 0u) {
                candidate = 1u;
            }
        }
        return SceneObjectId{ candidate };
    }

    SceneObjectData* FindSceneObjectData(
        SceneDocument& document,
        SceneObjectId id) noexcept {

        const auto found = std::find_if(
            document.objects.begin(),
            document.objects.end(),
            [id](const SceneObjectData& object) {
                return object.id == id;
            });
        return found != document.objects.end()
            ? &*found
            : nullptr;
    }

    const SceneObjectData* FindSceneObjectData(
        const SceneDocument& document,
        SceneObjectId id) noexcept {

        const auto found = std::find_if(
            document.objects.begin(),
            document.objects.end(),
            [id](const SceneObjectData& object) {
                return object.id == id;
            });
        return found != document.objects.end()
            ? &*found
            : nullptr;
    }

    SceneObjectData BuildDefaultGridFloorSceneObject(
        SceneDocument& document) {

        SceneObjectData object{};
        object.id = AllocateSceneObjectId(document);
        object.name = "Grid Floor";

        object.components.push_back(SceneComponentData{
            "ModelComponent",
            {
                { "assetId", "" },
                { "visible", true },
                { "castShadow", false },
                { "receiveShadow", true },
                { "renderStatic", true }
            }
        });
        object.components.push_back(SceneComponentData{
            "ProceduralMeshComponent",
            {
                { "kind", "GridPlane" },
                { "width", 100.0f },
                { "height", 1.0f },
                { "depth", 100.0f },
                { "segmentsX", 50u },
                { "segmentsY", 50u },
                { "segmentsZ", 1u },
                { "sphereSlices", 32u },
                { "sphereStacks", 16u },
                { "doubleSided", true },
                { "generateTangents", true }
            }
        });
        object.components.push_back(SceneComponentData{
            "ColliderComponent",
            {
                { "enabled", true },
                { "fitMode", "Geometry" },
                { "center", { 0.0f, 0.0f, 0.0f } },
                { "rotation", { 0.0f, 0.0f, 0.0f } },
                { "trigger", false },
                { "material", {
                    { "friction", 0.5f },
                    { "restitution", 0.0f },
                    { "density", 1.0f }
                } },
                { "filter", {
                    { "layer", 1u },
                    { "mask", 0xFFFFFFFFu }
                } },
                { "shape", "Box" },
                { "size", { 100.0f, 0.02f, 100.0f } },
                { "radius", 0.01f },
                { "height", 0.02f }
            }
        });

        NormalizeSceneComponentIds(object);
        return object;
    }

} // namespace HIKARI
