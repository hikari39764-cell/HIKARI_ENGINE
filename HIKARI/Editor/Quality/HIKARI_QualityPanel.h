#pragma once

#include "Editor/Quality/HIKARI_GlobalPostEditor.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

struct QualityPanelResult {
    bool environmentChanged = false;
    bool renderQualityChanged = false;
    bool editActive = false;
};

class QualityPanel {
public:
    QualityPanelResult Draw(SceneEnvironment& environment, DocumentSceneBase& scene);
    EDITOR::GlobalPostEditor& GlobalPost() { return globalPost_; }

private:
    EDITOR::GlobalPostEditor globalPost_;
};

} // namespace HIKARI
