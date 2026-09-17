#pragma once
namespace HIKARI { class AssetDatabase; struct CinematicSequence; }
namespace HIKARI::EDITOR {
    bool DrawPresentationCuePanel(CinematicSequence& sequence,
        const AssetDatabase& assets, bool editingAllowed);
}
