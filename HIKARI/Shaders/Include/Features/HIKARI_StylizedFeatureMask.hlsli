#ifndef HIKARI_STYLIZED_FEATURE_MASK_INCLUDED
#define HIKARI_STYLIZED_FEATURE_MASK_INCLUDED

static const uint HIKARI_STYLIZED_FEATURE_GEOMETRY_OUTLINE = 1u << 0u;
static const uint HIKARI_STYLIZED_FEATURE_SCREEN_OUTLINE = 1u << 1u;
static const uint HIKARI_STYLIZED_FEATURE_VISIBILITY_OVERLAY = 1u << 2u;
static const uint HIKARI_STYLIZED_FEATURE_FACE_HAIR_SHADOW = 1u << 3u;

static const uint HIKARI_STYLIZED_ROLE_FACE = 1u << 8u;
static const uint HIKARI_STYLIZED_ROLE_HAIR = 1u << 9u;
static const uint HIKARI_STYLIZED_ROLE_EYE = 1u << 10u;
static const uint HIKARI_STYLIZED_ROLE_SKIN = 1u << 11u;
static const uint HIKARI_STYLIZED_ROLE_CLOTH = 1u << 12u;
static const uint HIKARI_STYLIZED_ROLE_HYBRID = 1u << 13u;
static const uint HIKARI_STYLIZED_AUTO_GROUP = 1u << 31u;
static const uint HIKARI_STYLIZED_FACE_HAIR_STRENGTH_SHIFT = 16u;
static const uint HIKARI_STYLIZED_FACE_HAIR_OFFSET_SHIFT = 24u;

static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_SKIN_MASK = 1u << 17u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_FACE_MASK = 1u << 18u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_HAIR_MASK = 1u << 19u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_EYE_MASK = 1u << 20u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_CLOTH_MASK = 1u << 21u;
static const uint HIKARI_PROGRAM_FLAG_TEMPLATE_HYBRID_MASK = 1u << 22u;
static const uint HIKARI_PROGRAM_FLAG_GEOMETRY_OUTLINE_MASK = 1u << 23u;
static const uint HIKARI_PROGRAM_FLAG_SCREEN_OUTLINE_MASK = 1u << 24u;
static const uint HIKARI_PROGRAM_FLAG_VISIBILITY_OVERLAY_MASK = 1u << 25u;
static const uint HIKARI_PROGRAM_FLAG_FACE_HAIR_SHADOW_MASK = 1u << 26u;

uint HikariBuildStylizedFeatureMask(uint materialProgramFlags)
{
    uint mask = 0u;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_GEOMETRY_OUTLINE_MASK) != 0u)
        mask |= HIKARI_STYLIZED_FEATURE_GEOMETRY_OUTLINE;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_SCREEN_OUTLINE_MASK) != 0u)
        mask |= HIKARI_STYLIZED_FEATURE_SCREEN_OUTLINE;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_VISIBILITY_OVERLAY_MASK) != 0u)
        mask |= HIKARI_STYLIZED_FEATURE_VISIBILITY_OVERLAY;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_FACE_HAIR_SHADOW_MASK) != 0u)
        mask |= HIKARI_STYLIZED_FEATURE_FACE_HAIR_SHADOW;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_FACE_MASK) != 0u)
        mask |= HIKARI_STYLIZED_ROLE_FACE;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_HAIR_MASK) != 0u)
        mask |= HIKARI_STYLIZED_ROLE_HAIR;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_EYE_MASK) != 0u)
        mask |= HIKARI_STYLIZED_ROLE_EYE;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_SKIN_MASK) != 0u)
        mask |= HIKARI_STYLIZED_ROLE_SKIN;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_CLOTH_MASK) != 0u)
        mask |= HIKARI_STYLIZED_ROLE_CLOTH;
    if ((materialProgramFlags & HIKARI_PROGRAM_FLAG_TEMPLATE_HYBRID_MASK) != 0u)
        mask |= HIKARI_STYLIZED_ROLE_HYBRID;
    return mask;
}

uint HikariPackStylizedFaceHairParameters(
    uint featureMask,
    float strength,
    float offsetPixels)
{
    const uint strengthValue = (uint)round(saturate(strength) * 255.0f);
    const uint offsetValue = (uint)round(
        saturate(offsetPixels / 16.0f) * 127.0f);
    return featureMask |
        (strengthValue << HIKARI_STYLIZED_FACE_HAIR_STRENGTH_SHIFT) |
        (offsetValue << HIKARI_STYLIZED_FACE_HAIR_OFFSET_SHIFT);
}

float HikariDecodeStylizedFaceHairStrength(uint featureMask)
{
    return float((featureMask >>
        HIKARI_STYLIZED_FACE_HAIR_STRENGTH_SHIFT) & 0xffu) / 255.0f;
}

float HikariDecodeStylizedFaceHairOffset(uint featureMask)
{
    return float((featureMask >>
        HIKARI_STYLIZED_FACE_HAIR_OFFSET_SHIFT) & 0x7fu) *
        (16.0f / 127.0f);
}

uint HikariResolveStylizedGroupKey(
    uint postGroupMask,
    uint objectIdLow,
    uint objectIdHigh)
{
    if (postGroupMask != 0u)
        return postGroupMask & ~HIKARI_STYLIZED_AUTO_GROUP;
    uint hash = objectIdLow * 1664525u + objectIdHigh * 1013904223u + 0x9e3779b9u;
    hash ^= hash >> 16u;
    return HIKARI_STYLIZED_AUTO_GROUP | (hash & ~HIKARI_STYLIZED_AUTO_GROUP);
}

bool HikariStylizedGroupsMatch(uint left, uint right)
{
    const bool autoGroup =
        ((left | right) & HIKARI_STYLIZED_AUTO_GROUP) != 0u;
    return autoGroup ? left == right : (left & right) != 0u;
}

#endif
