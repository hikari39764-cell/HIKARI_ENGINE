// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>
#include <cmath>

#include "Authoring/HIKARI_FieldInspector.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
namespace {

bool MeasureSceneExtent(const SceneDocument* document, MATH::Vec3& outSize) {
    if (document == nullptr || document->objects.empty()) {
        return false;
    }
    MATH::Vec3 minimum = document->objects.front().transform.position;
    MATH::Vec3 maximum = minimum;
    for (const SceneObjectData& object : document->objects) {
        minimum.x = (std::min)(minimum.x, object.transform.position.x);
        minimum.y = (std::min)(minimum.y, object.transform.position.y);
        minimum.z = (std::min)(minimum.z, object.transform.position.z);
        maximum.x = (std::max)(maximum.x, object.transform.position.x);
        maximum.y = (std::max)(maximum.y, object.transform.position.y);
        maximum.z = (std::max)(maximum.z, object.transform.position.z);
    }
    outSize = {(std::max)(maximum.x - minimum.x, 1.0f), (std::max)(maximum.y - minimum.y, 1.0f),
               (std::max)(maximum.z - minimum.z, 1.0f)};
    return true;
}
} // namespace

void ReflectionProbeComponent::BuildInspector(IInspectorBuilder& builder) {
    ReflectionProbeComponentSettings next = settings_;
    bool changed = InspectFields(builder, next, kReflectionProbeFields);
    MATH::Vec3 sceneSize{};
    if (builder.Button("Fit Influence Size To Scene") &&
        MeasureSceneExtent(builder.GetContext().sceneDocument, sceneSize)) {
        next.influenceBoxSize = sceneSize;
        next.projectionBoxSize = sceneSize;
        next.radius =
            0.5f * std::sqrt(sceneSize.x * sceneSize.x + sceneSize.y * sceneSize.y + sceneSize.z * sceneSize.z);
        changed = true;
    }
    if (!next.projectionFollowsInfluence && builder.Button("Match Projection To Influence")) {
        AlignReflectionProbeProjection(next);
        changed = true;
    }
    if (changed) {
        settings_ = NormalizeReflectionProbeSettings(next);
    }
}

void IrradianceVolumeComponent::BuildInspector(IInspectorBuilder& builder) {
    IrradianceVolumeComponentSettings next = settings_;
    bool changed = InspectFields(builder, next, kIrradianceVolumeFields);
    MATH::Vec3 sceneSize{};
    if (builder.Button("Fit Size To Scene") && MeasureSceneExtent(builder.GetContext().sceneDocument, sceneSize)) {
        next.size = sceneSize;
        changed = true;
    }
    if (changed) {
        settings_ = NormalizeIrradianceVolumeSettings(next);
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
