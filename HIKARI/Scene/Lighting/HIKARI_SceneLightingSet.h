#pragma once

#include <span>

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI {

    struct SceneDocument;
    struct SceneObjectData;

    namespace LIGHTING_AUTHORING {

        // Scene component から renderer 非依存の意味情報だけを読み取り、
        // runtime loader へ渡す不変スナップショットを構築する。
        RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(
            const SceneDocument& document);
        RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(
            std::span<const SceneObjectData> objects);
        bool AreSceneLightingSetsEquivalent(
            const RENDER3D::LIGHTING::SceneLightingSet& lhs,
            const RENDER3D::LIGHTING::SceneLightingSet& rhs) noexcept;

    } // namespace LIGHTING_AUTHORING

} // namespace HIKARI
