#include "Scene/HIKARI_SceneDocumentQueries.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "Scene/HIKARI_SceneDocument.h"

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

} // namespace HIKARI
