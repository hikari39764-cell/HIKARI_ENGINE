#pragma once

namespace HIKARI {

class AssetDatabase;
class AssetRegistry;
struct SceneEnvironment;

namespace SKYRENDERER {
struct SkyRendererDebugState;
}

struct EnvironmentPanelResult {
  bool environmentChanged = false;
  bool openLightingBakeRequested = false;
};

class EnvironmentPanel {
public:
  EnvironmentPanelResult
  Draw(SceneEnvironment &environment,
       const SKYRENDERER::SkyRendererDebugState *skyDebugState,
       const AssetRegistry *assetRegistry = nullptr,
       const AssetDatabase *assetDatabase = nullptr) const;
};

} // namespace HIKARI
