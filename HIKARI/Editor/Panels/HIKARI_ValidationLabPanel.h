#pragma once

#include "Editor/HIKARI_EditorContext.h"

namespace HIKARI {

    class ValidationLabPanel {
    public:
        void Draw(EditorContext& context, bool& open) const;
        void DrawContents(EditorContext& context) const;
    };

} // namespace HIKARI
