#pragma once

// 資産の種類。項目表の AssetRef など、資産の定義ごと読み込みたくない所が使う。
namespace HIKARI {

enum class AssetType {
    Model,
    Scene,
    Prefab,
    Sky,
    Texture,
    Sprite,
    SpriteMaterial,
    Material,
    Animation,
    Particle,
    VfxEffect,
    Sequence,
    AnimationStateMachine,
    Dialogue,
    ModelMaterialBinding,
    Shader,
    GameplayCamera,
    Font,
    TextStyle,
    PostEffect,
    PostProfile,
    Environment,
    Weather,
    RenderStyle,
    ColorLut,
    LightProfile,
    Audio,
    // モジュールが登記した資産。具体の種類は拡張子から ASSETS::SEMANTICS の登記表で引く。
    ModuleAsset,
    Unknown,
};

} // namespace HIKARI
