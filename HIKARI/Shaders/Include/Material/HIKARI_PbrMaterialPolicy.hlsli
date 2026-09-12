#ifndef HIKARI_PBR_MATERIAL_POLICY_INCLUDED
#define HIKARI_PBR_MATERIAL_POLICY_INCLUDED

void HikariApplyLegacySpecularResponse(bool legacyResponse, bool alphaMasked,
    inout float roughness, inout float3 specularColor, inout float specularFactor)
{
    // 旧 cooked material が明示した応答だけを再現する。Mask は被覆の意味のみを持つ。
    if (!legacyResponse) return;
    roughness = max(roughness, alphaMasked ? 0.86f : 0.72f);
    specularFactor *= alphaMasked ? (0.75f * 0.45f) : 0.65f;
    specularColor *= alphaMasked ? 0.70f : 0.85f;
}

#endif
