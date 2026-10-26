#include "HIKARI_ObjectTrackDriver.h"

#include <algorithm>

#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightSettings.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Tracks/HIKARI_ObjectTrack.h"

namespace HIKARI::SEQUENCER {
namespace {
bool SameTransform(const Transform3D& a, const Transform3D& b) noexcept {
    const auto near = [](float x, float y) { return std::abs(x - y) <= 1.0e-4f; };
    return near(a.position.x, b.position.x) && near(a.position.y, b.position.y) && near(a.position.z, b.position.z) &&
           near(a.rotation.x, b.rotation.x) && near(a.rotation.y, b.rotation.y) && near(a.rotation.z, b.rotation.z) &&
           near(a.rotation.w, b.rotation.w) && near(a.scale.x, b.scale.x) && near(a.scale.y, b.scale.y) &&
           near(a.scale.z, b.scale.z);
}
} // namespace

ObjectTrackDriver::ObjectTrackDriver(World& world) : world_(world) {}

void ObjectTrackDriver::OnSequenceStarted(const SequencePlaybackInstanceView& instance) {
    InstanceState state;
    state.handle = instance.handle;
    states_.push_back(std::move(state));
}

void ObjectTrackDriver::Validate(const SequencePlaybackInstanceView& instance,
                                 std::vector<SequencePlaybackDiagnostic>& diagnostics) const {
    if (!instance.asset)
        return;
    const auto& tracks = instance.asset->sequence.objectTracks;
    std::string error;
    if (!ValidateObjectTracks(tracks, error))
        diagnostics.push_back({SequenceDiagnosticSeverity::Error, "InvalidObjectTrack", "ObjectTrack", error});
    for (const auto& track : tracks) {
        if (!track.enabled || track.keys.empty())
            continue;
        SceneObjectId object;
        if (!instance.bindings || !instance.bindings->Resolve(instance.asset->sequence.bindings, track.target, object) ||
            !world_.FindObject(object))
            diagnostics.push_back({SequenceDiagnosticSeverity::Warning, "ObjectTrackTargetMissing", "ObjectTrack",
                                   "Object track target is unavailable: " + track.name, track.target});
    }
}

void ObjectTrackDriver::Evaluate(const SequencePlaybackInstanceView& instance,
                                 const SequenceEvaluationContext& context) {
    const auto found = std::find_if(states_.begin(), states_.end(),
                                    [&](const InstanceState& state) { return state.handle == instance.handle; });
    if (found == states_.end() || !instance.asset || !instance.bindings)
        return;
    found->preview = context.IsPreview();
    for (const auto& track : instance.asset->sequence.objectTracks) {
        ObjectTrackSample sample;
        if (!track.enabled || !SampleObjectTrack(track, context.currentTimeSeconds, sample))
            continue;
        SceneObjectId objectId;
        if (!instance.bindings->Resolve(instance.asset->sequence.bindings, track.target, objectId))
            continue;
        GameObject* object = world_.FindObject(objectId);
        if (!object)
            continue;
        const RuntimeObjectHandle handle = object->GetRuntimeHandle();
        auto touched = std::find_if(found->touched.begin(), found->touched.end(),
                                    [&](const Touched& t) { return t.object == objectId; });
        if (touched == found->touched.end()) {
            Touched entry;
            entry.object = objectId;
            if (!world_.TryGetLocalTransform(handle, entry.original))
                continue;
            entry.written = entry.original;
            if (const auto* light = object->GetComponent<LightComponent>()) {
                entry.hasLight = true;
                entry.originalLight = entry.writtenLight = light->GetIntensity();
            }
            found->touched.push_back(entry);
            touched = std::prev(found->touched.end());
        }
        touched->restore = track.restoreOnStop;
        Transform3D transform = touched->original;
        transform.position = sample.position;
        transform.rotation = sample.rotation;
        transform.scale = sample.scale;
        const WorldMutationResult result = world_.SetLocalTransform(handle, transform);
        if (result == WorldMutationResult::Success || result == WorldMutationResult::Queued)
            touched->written = transform;
        if (track.animateLight && touched->hasLight) {
            if (auto* light = object->GetComponent<LightComponent>()) {
                LightSettings settings = light->GetSettings();
                SetLightBrightness(settings, touched->originalLight * sample.light);
                light->SetSettings(settings);
                touched->writtenLight = light->GetIntensity();
            }
        }
    }
}

void ObjectTrackDriver::Restore(InstanceState& state) {
    for (const auto& touched : state.touched) {
        // Play で「止まった姿勢のまま残す」轨は戻さない。編集器のプレビューは必ず戻す。
        if (!touched.restore && !state.preview)
            continue;
        GameObject* object = world_.FindObject(touched.object);
        if (!object)
            continue;
        const RuntimeObjectHandle handle = object->GetRuntimeHandle();
        Transform3D current;
        if (world_.TryGetLocalTransform(handle, current) && SameTransform(current, touched.written))
            (void)world_.SetLocalTransform(handle, touched.original);
        if (touched.hasLight)
            if (auto* light = object->GetComponent<LightComponent>();
                light && std::abs(light->GetIntensity() - touched.writtenLight) <= 1.0e-3f) {
                LightSettings settings = light->GetSettings();
                SetLightBrightness(settings, touched.originalLight);
                light->SetSettings(settings);
            }
    }
    state.touched.clear();
}

void ObjectTrackDriver::OnSequenceStopped(SequencePlaybackHandle handle, SequenceStopReason) {
    for (auto& state : states_)
        if (state.handle == handle)
            Restore(state);
    std::erase_if(states_, [&](const InstanceState& state) { return state.handle == handle; });
}

void ObjectTrackDriver::Reset() {
    for (auto& state : states_)
        Restore(state);
    states_.clear();
}
} // namespace HIKARI::SEQUENCER
