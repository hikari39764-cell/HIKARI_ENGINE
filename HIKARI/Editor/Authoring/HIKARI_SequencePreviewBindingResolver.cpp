#include "Editor/Authoring/HIKARI_SequencePreviewBindingResolver.h"

#include <algorithm>
#include <string_view>

#include "HIKARI/Project/SequenceTypes.h"
#include "Scene/Sequencer/HIKARI_SequenceBindingNaming.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI::EDITOR {

namespace {
enum class PersistentBindingResult {
    NotFound,
    Resolved,
    Conflict,
};

const SceneObjectData* FindSceneObject(const SceneDocument& document, SceneObjectId objectId) noexcept {
    const auto found = std::find_if(document.objects.begin(), document.objects.end(),
                                    [objectId](const SceneObjectData& object) { return object.id == objectId; });
    return found != document.objects.end() ? &*found : nullptr;
}

bool HasCameraComponent(const SceneObjectData& object) noexcept {
    return std::any_of(object.components.begin(), object.components.end(),
                       [](const SceneComponentData& component) { return component.type == "CameraComponent"; });
}

bool IsMatchingSequencePlayer(const SceneComponentData& component, const AssetGuid& sequenceAssetGuid) {
    if (!sequenceAssetGuid.IsValid() || component.type != "SequencePlayerComponent" ||
        !component.properties.is_object()) {
        return false;
    }
    const auto guid = component.properties.find("sequenceAssetGuid");
    return guid != component.properties.end() && guid->is_string() &&
           guid->get<std::string>() == sequenceAssetGuid.value;
}

bool IsCameraPreviewSlot(std::string_view requestedSlot, std::string_view hintedSlot) noexcept {
    if (!hintedSlot.empty() && requestedSlot == hintedSlot) {
        return true;
    }
    return requestedSlot == "CinematicCamera" || requestedSlot == "Camera.Camera" ||
           requestedSlot.starts_with("Camera.");
}

bool ResolveGameProjectCameraHint(const SceneDocument& document, const SceneObjectData& object,
                                  const SceneComponentData& component, const AssetGuid& sequenceAssetGuid,
                                  std::string_view requestedSlot, SceneObjectId& cameraId) {
    if (!sequenceAssetGuid.IsValid() || !component.properties.is_object()) {
        return false;
    }
    const auto guid = component.properties.find(kGameProjectSequencePreviewAssetGuidProperty);
    if (guid == component.properties.end() || !guid->is_string() ||
        guid->get<std::string>() != sequenceAssetGuid.value) {
        return false;
    }
    const auto slot = component.properties.find(kGameProjectSequencePreviewSlotNameProperty);
    const std::string hintedSlot =
        slot != component.properties.end() && slot->is_string() ? slot->get<std::string>() : std::string{};
    if (!IsCameraPreviewSlot(requestedSlot, hintedSlot))
        return false;
    cameraId = object.id;
    const auto reference = component.properties.find(kGameProjectSequencePreviewCameraProperty);
    if (reference != component.properties.end())
        cameraId = SceneObjectId{SCENE::SERIALIZATION::SceneObjectReferenceOr(*reference)};
    const auto* camera = FindSceneObject(document, cameraId);
    return camera && HasCameraComponent(*camera);
}

PersistentBindingResult TryResolvePersistentBinding(const SceneDocument& document, const AssetGuid& sequenceAssetGuid,
                                                    std::string_view slotName, SceneObjectId& outObjectId) {
    bool foundCandidate = false;
    SceneObjectId candidate{};
    for (const SceneObjectData& object : document.objects) {
        for (const SceneComponentData& component : object.components) {
            SceneObjectId cameraId{};
            if (ResolveGameProjectCameraHint(document, object, component, sequenceAssetGuid, slotName, cameraId)) {
                if (foundCandidate && candidate != cameraId) {
                    return PersistentBindingResult::Conflict;
                }
                candidate = cameraId;
                foundCandidate = true;
            }
            if (!IsMatchingSequencePlayer(component, sequenceAssetGuid)) {
                continue;
            }
            const auto bindings = component.properties.find("bindings");
            if (bindings == component.properties.end() || !bindings->is_array()) {
                continue;
            }
            for (const nlohmann::json& node : *bindings) {
                if (!node.is_object()) {
                    continue;
                }
                const auto savedSlot = node.find("slotName");
                const auto savedObject = node.find("sceneObjectId");
                if (savedSlot == node.end() || !savedSlot->is_string() || savedSlot->get<std::string>() != slotName ||
                    savedObject == node.end()) {
                    continue;
                }
                SceneObjectId objectId{SCENE::SERIALIZATION::SceneObjectReferenceOr(*savedObject)};
                const SceneObjectData* target = FindSceneObject(document, objectId);
                // SequencePlayer の明示的な slot は、注視対象や VFX 主体にも使う。
                // CameraComponent の検証は Camera track driver が担当する。
                if (target == nullptr) {
                    continue;
                }
                if (foundCandidate && candidate != objectId) {
                    return PersistentBindingResult::Conflict;
                }
                candidate = objectId;
                foundCandidate = true;
            }
        }
    }
    if (!foundCandidate) {
        return PersistentBindingResult::NotFound;
    }
    outObjectId = candidate;
    return PersistentBindingResult::Resolved;
}

bool TryResolveGeneratedCameraSlot(const SceneDocument& document, const std::string& slotName,
                                   SceneObjectId& outObjectId) {
    bool foundCandidate = false;
    SceneObjectId candidate{};
    for (const SceneObjectData& object : document.objects) {
        if (!HasCameraComponent(object)) {
            continue;
        }
        const std::string canonicalSlotName = BuildCameraPreviewSlotName(document, object.id);
        if (!IsGeneratedCameraPreviewSlot(slotName, canonicalSlotName)) {
            continue;
        }
        if (foundCandidate && candidate != object.id) {
            return false;
        }
        candidate = object.id;
        foundCandidate = true;
    }
    if (!foundCandidate) {
        return false;
    }
    outObjectId = candidate;
    return true;
}
} // namespace

std::string BuildCameraPreviewSlotName(const SceneDocument& document, SceneObjectId cameraObjectId) {
    const SceneObjectData* object = FindSceneObject(document, cameraObjectId);
    return "Camera." + SEQUENCER::NormalizeSequenceBindingSlotName(
                           object != nullptr ? object->name : ("Object" + std::to_string(cameraObjectId.value)));
}

bool IsGeneratedCameraPreviewSlot(const std::string& slotName, const std::string& canonicalSlotName) noexcept {
    if (slotName == canonicalSlotName) {
        return true;
    }
    if (slotName.size() <= canonicalSlotName.size() + 1 ||
        slotName.compare(0, canonicalSlotName.size(), canonicalSlotName) != 0 ||
        slotName[canonicalSlotName.size()] != '.') {
        return false;
    }
    return std::all_of(slotName.begin() + canonicalSlotName.size() + 1, slotName.end(),
                       [](char character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; });
}

size_t RestoreSequencePreviewBindings(const SceneDocument& document, const AssetGuid& sequenceAssetGuid,
                                      const CinematicSequence& sequence,
                                      SEQUENCER::SequenceBindingContext& outBindings) {
    outBindings.Clear();
    size_t restoredCount = 0;
    for (const SEQUENCER::SequenceBinding& binding : sequence.bindings) {
        if (binding.targetKind != SEQUENCER::SequenceBindingTargetKind::Slot || binding.slotName.empty()) {
            continue;
        }
        SceneObjectId objectId{};
        const PersistentBindingResult persistentResult =
            TryResolvePersistentBinding(document, sequenceAssetGuid, binding.slotName, objectId);
        const bool restored = persistentResult == PersistentBindingResult::Resolved ||
                              (persistentResult == PersistentBindingResult::NotFound &&
                               TryResolveGeneratedCameraSlot(document, binding.slotName, objectId));
        if (restored && outBindings.Bind(binding.id, objectId)) {
            ++restoredCount;
        }
    }
    return restoredCount;
}

} // namespace HIKARI::EDITOR
