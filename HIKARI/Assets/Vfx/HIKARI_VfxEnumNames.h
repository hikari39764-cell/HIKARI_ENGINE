#pragma once

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Core/Text/HIKARI_EnumNames.h"

// VFX source の列挙名。source JSON・エディター・AI 操作が同じ表を参照し、
// 表の並びはエディターの選択肢の表示順を兼ねる。
namespace HIKARI::VFX {

inline constexpr TEXT::EnumName<VfxLayerKind> kVfxLayerKindNames[]{
    {VfxLayerKind::Shape, "Shape"},
    {VfxLayerKind::Particles, "Particles"},
    {VfxLayerKind::Group, "Group"},
    {VfxLayerKind::Adjustment, "Adjustment"},
};
inline constexpr auto kVfxLayerKindChoices = TEXT::EnumNameList(kVfxLayerKindNames);

inline constexpr TEXT::EnumName<VfxValueType> kVfxValueTypeNames[]{
    {VfxValueType::Bool, "Bool"},     {VfxValueType::Int, "Int"},       {VfxValueType::Float, "Float"},
    {VfxValueType::Float2, "Float2"}, {VfxValueType::Float3, "Float3"}, {VfxValueType::Float4, "Float4"},
    {VfxValueType::Color, "Color"},
};
inline constexpr auto kVfxValueTypeChoices = TEXT::EnumNameList(kVfxValueTypeNames);

inline constexpr TEXT::EnumName<VfxShapeType> kVfxShapeTypeNames[]{
    {VfxShapeType::Point, "Point"}, {VfxShapeType::Sphere, "Sphere"},     {VfxShapeType::Cone, "Cone"},
    {VfxShapeType::Box, "Box"},     {VfxShapeType::Cylinder, "Cylinder"}, {VfxShapeType::Circle, "Circle"},
    {VfxShapeType::Edge, "Edge"},
};
inline constexpr auto kVfxShapeTypeChoices = TEXT::EnumNameList(kVfxShapeTypeNames);

inline constexpr TEXT::EnumName<VfxRenderShapeType> kVfxRenderShapeTypeNames[]{
    {VfxRenderShapeType::Sprite, "Sprite"},
    {VfxRenderShapeType::Ribbon, "Ribbon"},
    {VfxRenderShapeType::Ring, "Ring"},
    {VfxRenderShapeType::MeshBox, "MeshBox"},
    {VfxRenderShapeType::MeshSphere, "MeshSphere"},
    {VfxRenderShapeType::MeshCylinder, "MeshCylinder"},
    {VfxRenderShapeType::MeshCone, "MeshCone"},
    {VfxRenderShapeType::MeshHemisphere, "MeshHemisphere"},
    {VfxRenderShapeType::CrossPlane, "CrossPlane"},
    {VfxRenderShapeType::MeshTorus, "MeshTorus"},
    {VfxRenderShapeType::MeshCapsule, "MeshCapsule"},
};
inline constexpr auto kVfxRenderShapeTypeChoices = TEXT::EnumNameList(kVfxRenderShapeTypeNames);

inline constexpr TEXT::EnumName<VfxShapeUvMode> kVfxShapeUvModeNames[]{
    {VfxShapeUvMode::Parameter, "Parameter"},
    {VfxShapeUvMode::WorldLength, "WorldLength"},
};
inline constexpr auto kVfxShapeUvModeChoices = TEXT::EnumNameList(kVfxShapeUvModeNames);

inline constexpr TEXT::EnumName<VfxBlendMode> kVfxBlendModeNames[]{
    {VfxBlendMode::Alpha, "Alpha"},
    {VfxBlendMode::Additive, "Additive"},
    {VfxBlendMode::Distortion, "Distortion"},
};
inline constexpr auto kVfxBlendModeChoices = TEXT::EnumNameList(kVfxBlendModeNames);

inline constexpr TEXT::EnumName<VfxMaterialMode> kVfxMaterialModeNames[]{
    {VfxMaterialMode::StandardUnlit, "StandardUnlit"},
    {VfxMaterialMode::StandardLit, "StandardLit"},
    {VfxMaterialMode::CustomMaterial, "CustomMaterial"},
    {VfxMaterialMode::CustomLit, "CustomLit"},
};
inline constexpr auto kVfxMaterialModeChoices = TEXT::EnumNameList(kVfxMaterialModeNames);

inline constexpr TEXT::EnumName<VfxParticleDepthMode> kVfxParticleDepthModeNames[]{
    {VfxParticleDepthMode::WorldOccluded, "WorldOccluded"},
    {VfxParticleDepthMode::SoftIntersection, "SoftIntersection"},
    {VfxParticleDepthMode::ContactSurface, "ContactSurface"},
    {VfxParticleDepthMode::ForegroundAccent, "ForegroundAccent"},
};
inline constexpr auto kVfxParticleDepthModeChoices = TEXT::EnumNameList(kVfxParticleDepthModeNames);

inline constexpr TEXT::EnumName<VfxParticleCapacityMode> kVfxParticleCapacityModeNames[]{
    {VfxParticleCapacityMode::Automatic, "Automatic"},
    {VfxParticleCapacityMode::Fixed, "Fixed"},
};
inline constexpr auto kVfxParticleCapacityModeChoices = TEXT::EnumNameList(kVfxParticleCapacityModeNames);

inline constexpr TEXT::EnumName<VfxBillboardMode> kVfxBillboardModeNames[]{
    {VfxBillboardMode::RotatedBillboard, "RotatedBillboard"},
    {VfxBillboardMode::FixedYAxis, "FixedYAxis"},
    {VfxBillboardMode::DirectionalBillboard, "DirectionalBillboard"},
    {VfxBillboardMode::Fixed, "Fixed"},
    {VfxBillboardMode::CylindricalBillboard, "CylindricalBillboard"},
    {VfxBillboardMode::FaceCameraPosition, "FaceCameraPosition"},
};
inline constexpr auto kVfxBillboardModeChoices = TEXT::EnumNameList(kVfxBillboardModeNames);

inline constexpr TEXT::EnumName<VfxBillboardAxis> kVfxBillboardAxisNames[]{
    {VfxBillboardAxis::LocalX, "LocalX"},
    {VfxBillboardAxis::LocalY, "LocalY"},
    {VfxBillboardAxis::LocalZ, "LocalZ"},
};
inline constexpr auto kVfxBillboardAxisChoices = TEXT::EnumNameList(kVfxBillboardAxisNames);

inline constexpr TEXT::EnumName<VfxCurveInterpolation> kVfxCurveInterpolationNames[]{
    {VfxCurveInterpolation::Linear, "Linear"},
    {VfxCurveInterpolation::Smooth, "Smooth"},
    {VfxCurveInterpolation::Step, "Step"},
};
inline constexpr auto kVfxCurveInterpolationChoices = TEXT::EnumNameList(kVfxCurveInterpolationNames);

inline constexpr TEXT::EnumName<VfxDistributionMode> kVfxDistributionModeNames[]{
    {VfxDistributionMode::Constant, "Constant"},
    {VfxDistributionMode::RandomRange, "RandomRange"},
};
inline constexpr auto kVfxDistributionModeChoices = TEXT::EnumNameList(kVfxDistributionModeNames);

inline constexpr TEXT::EnumName<VfxAnimatedValueMode> kVfxAnimatedValueModeNames[]{
    {VfxAnimatedValueMode::Fixed, "Fixed"},
    {VfxAnimatedValueMode::Pva, "Pva"},
    {VfxAnimatedValueMode::Easing, "Easing"},
    {VfxAnimatedValueMode::Curve, "Curve"},
};
inline constexpr auto kVfxAnimatedValueModeChoices = TEXT::EnumNameList(kVfxAnimatedValueModeNames);

inline constexpr TEXT::EnumName<VfxAnimatedColorMode> kVfxAnimatedColorModeNames[]{
    {VfxAnimatedColorMode::Fixed, "Fixed"},       {VfxAnimatedColorMode::RandomRange, "RandomRange"},
    {VfxAnimatedColorMode::Easing, "Easing"},     {VfxAnimatedColorMode::Curve, "Curve"},
    {VfxAnimatedColorMode::Gradient, "Gradient"},
};
inline constexpr auto kVfxAnimatedColorModeChoices = TEXT::EnumNameList(kVfxAnimatedColorModeNames);

inline constexpr TEXT::EnumName<VfxEasingFunction> kVfxEasingFunctionNames[]{
    {VfxEasingFunction::Linear, "Linear"},
    {VfxEasingFunction::EaseIn, "EaseIn"},
    {VfxEasingFunction::EaseOut, "EaseOut"},
    {VfxEasingFunction::EaseInOut, "EaseInOut"},
};
inline constexpr auto kVfxEasingFunctionChoices = TEXT::EnumNameList(kVfxEasingFunctionNames);

inline constexpr TEXT::EnumName<VfxAttachmentMode> kVfxAttachmentModeNames[]{
    {VfxAttachmentMode::World, "World"},
    {VfxAttachmentMode::Owner, "Owner"},
    {VfxAttachmentMode::OwnerSocket, "OwnerSocket"},
};
inline constexpr auto kVfxAttachmentModeChoices = TEXT::EnumNameList(kVfxAttachmentModeNames);

inline constexpr TEXT::EnumName<VfxFollowMode> kVfxFollowModeNames[]{
    {VfxFollowMode::None, "None"},
    {VfxFollowMode::Position, "Position"},
    {VfxFollowMode::Rotation, "Rotation"},
    {VfxFollowMode::Transform, "Transform"},
};
inline constexpr auto kVfxFollowModeChoices = TEXT::EnumNameList(kVfxFollowModeNames);

inline constexpr TEXT::EnumName<VfxSpecialOutputType> kVfxSpecialOutputTypeNames[]{
    {VfxSpecialOutputType::WeaponPathDistortion, "WeaponPathDistortion"},
    {VfxSpecialOutputType::ScreenImpulse, "ScreenImpulse"},
    {VfxSpecialOutputType::SkinnedAfterimage, "SkinnedAfterimage"},
    {VfxSpecialOutputType::Light, "Light"},
};
inline constexpr auto kVfxSpecialOutputTypeChoices = TEXT::EnumNameList(kVfxSpecialOutputTypeNames);

inline constexpr TEXT::EnumName<VfxHistoryMode> kVfxHistoryModeNames[]{
    {VfxHistoryMode::None, "None"},
    {VfxHistoryMode::Afterimage, "Afterimage"},
    {VfxHistoryMode::Trail, "Trail"},
};
inline constexpr auto kVfxHistoryModeChoices = TEXT::EnumNameList(kVfxHistoryModeNames);

inline constexpr TEXT::EnumName<VfxConnectTargetMode> kVfxConnectTargetModeNames[]{
    {VfxConnectTargetMode::None, "None"},
    {VfxConnectTargetMode::LayerOrigin, "LayerOrigin"},
    {VfxConnectTargetMode::LocalPosition, "LocalPosition"},
};
inline constexpr auto kVfxConnectTargetModeChoices = TEXT::EnumNameList(kVfxConnectTargetModeNames);

} // namespace HIKARI::VFX
