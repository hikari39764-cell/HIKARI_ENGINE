#pragma once

#include <memory>

#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"

namespace HIKARI {

class IRuntimeFeature;

// 組み込み Feature の生成関数。カタログ構築と単体テストが個別に使う。
std::unique_ptr<IRuntimeFeature> CreateAnimationRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateBuoyancyRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateCameraRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateGameplayRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateLightingRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreatePhysicsRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateRenderingRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateSceneFlowRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateSequenceRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateSurfaceInteractionRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateTerrainRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateVfxRuntimeFeature();
std::unique_ptr<IRuntimeFeature> CreateWaterRuntimeFeature();

RuntimeFeatureCatalog CreateBuiltInRuntimeFeatureCatalog();

} // namespace HIKARI
