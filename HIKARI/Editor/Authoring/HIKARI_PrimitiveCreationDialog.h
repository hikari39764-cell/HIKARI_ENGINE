#pragma once

#include <array>

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"

namespace HIKARI::EDITOR {

    class PrimitiveCreationDialog {
    public:
        void Open(ProceduralMeshKind kind);
        bool Draw(CreatePrimitiveRequest& outRequest);

    private:
        std::array<char, 128> nameBuffer_{};
        ProceduralMeshSettings settings_{};
        bool addCollider_ = true;
        bool openRequested_ = false;
    };

} // namespace HIKARI::EDITOR
