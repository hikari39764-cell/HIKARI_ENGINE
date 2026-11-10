#pragma once

#include "HIKARI/Project/AssetKind.h"
#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"

// 公開 SDK の宣言を、引擎の中（検査器・編集器の作業区）が引擎の型で扱うための入口。
namespace HIKARI {

class IInspectorBuilder;
class IPropertyFieldDeclaration;

constexpr AssetType ToAssetType(GameProjectAssetKind kind) noexcept {
    switch (kind) {
    case GameProjectAssetKind::Scene: return AssetType::Scene;
    case GameProjectAssetKind::Prefab: return AssetType::Prefab;
    case GameProjectAssetKind::Model: return AssetType::Model;
    case GameProjectAssetKind::ModelMaterialSet: return AssetType::ModelMaterialBinding;
    case GameProjectAssetKind::Material: return AssetType::Material;
    case GameProjectAssetKind::Texture: return AssetType::Texture;
    case GameProjectAssetKind::Sky: return AssetType::Sky;
    case GameProjectAssetKind::Sprite: return AssetType::Sprite;
    case GameProjectAssetKind::SpriteMaterial: return AssetType::SpriteMaterial;
    case GameProjectAssetKind::Animation: return AssetType::Animation;
    case GameProjectAssetKind::AnimationStateMachine: return AssetType::AnimationStateMachine;
    case GameProjectAssetKind::Sequence: return AssetType::Sequence;
    case GameProjectAssetKind::Dialogue: return AssetType::Dialogue;
    case GameProjectAssetKind::GameplayCamera: return AssetType::GameplayCamera;
    case GameProjectAssetKind::VfxEffect: return AssetType::VfxEffect;
    case GameProjectAssetKind::Shader: return AssetType::Shader;
    case GameProjectAssetKind::PostEffect: return AssetType::PostEffect;
    case GameProjectAssetKind::PostProfile: return AssetType::PostProfile;
    case GameProjectAssetKind::Font: return AssetType::Font;
    case GameProjectAssetKind::TextStyle: return AssetType::TextStyle;
    case GameProjectAssetKind::Environment: return AssetType::Environment;
    case GameProjectAssetKind::Weather: return AssetType::Weather;
    case GameProjectAssetKind::RenderStyle: return AssetType::RenderStyle;
    case GameProjectAssetKind::ColorLut: return AssetType::ColorLut;
    case GameProjectAssetKind::LightProfile: return AssetType::LightProfile;
    case GameProjectAssetKind::Audio: return AssetType::Audio;
    }
    return AssetType::Unknown;
}

// 項目の宣言を検査器の構築器へ当てて、画面の控件を描く（部品以外のデータの面板用）。
void InspectPropertyFields(IPropertyFieldDeclaration& declaration, IInspectorBuilder& builder);

} // namespace HIKARI
