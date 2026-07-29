#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

struct QualityPanelResult {
  bool environmentChanged = false;
  bool renderQualityChanged = false;
  bool editActive = false;
};

class QualityPanel {
public:
  QualityPanelResult Draw(SceneEnvironment &environment) const;
};

} // namespace HIKARI
