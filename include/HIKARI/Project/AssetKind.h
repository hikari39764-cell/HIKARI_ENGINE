#pragma once

#include <cstdint>

namespace HIKARI {

// GUID で参照する引擎の資産の種類。保存形式は共通で、選択候補だけが変わる。
// モジュールが清単に登記した種類は、この列挙ではなく清単の typeId で指す。
enum class GameProjectAssetKind : uint8_t {
    Scene,
    Prefab,
    Model,
    ModelMaterialSet,
    Material,
    Texture,
    Sky,
    Sprite,
    SpriteMaterial,
    Animation,
    AnimationStateMachine,
    Sequence,
    Dialogue,
    GameplayCamera,
    VfxEffect,
    Shader,
    PostEffect,
    PostProfile,
    Font,
    TextStyle,
    Environment,
    Weather,
    RenderStyle,
    ColorLut,
    LightProfile,
    Audio,
};

} // namespace HIKARI
