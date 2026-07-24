#pragma once

#include <cstddef>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

    std::string BuildCameraPreviewSlotName(
        const SceneDocument& document,
        SceneObjectId cameraObjectId);

    bool IsGeneratedCameraPreviewSlot(
        const std::string& slotName,
        const std::string& canonicalSlotName) noexcept;

    size_t RestoreSequencePreviewBindings(
        const SceneDocument& document,
        const AssetGuid& sequenceAssetGuid,
        const CinematicSequence& sequence,
        SEQUENCER::SequenceBindingContext& outBindings);

} // namespace HIKARI::EDITOR
