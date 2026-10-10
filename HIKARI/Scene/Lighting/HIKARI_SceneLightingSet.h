#pragma once

#include <span>
#include <string>

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

struct SceneDocument;
struct SceneObjectData;
class World;

namespace LIGHTING_AUTHORING {

// 場面の物体の灯の SceneLightProvider::id。
std::string LightId(SceneObjectId objectId);

// Scene component から renderer 非依存の意味情報だけを読み取り、
// runtime loader へ渡す不変スナップショットを構築する。
RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(const SceneDocument& document);
RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(std::span<const SceneObjectData> objects);
RENDER3D::LIGHTING::SceneLightingSet BuildRuntimeSceneLightingSet(const SceneDocument& document, const World& world);
bool AreSceneLightingSetsEquivalent(const RENDER3D::LIGHTING::SceneLightingSet& lhs,
                                    const RENDER3D::LIGHTING::SceneLightingSet& rhs) noexcept;

} // namespace LIGHTING_AUTHORING

} // namespace HIKARI
