#ifndef HIKARI_MATERIAL_EXPRESSION_INCLUDED
#define HIKARI_MATERIAL_EXPRESSION_INCLUDED

static const uint HIKARI_MATERIAL_EXPRESSION_MAGIC = 0x484D4558u;
static const uint HIKARI_MATERIAL_EXPRESSION_VERSION = 1u;
static const uint HIKARI_MATERIAL_EXPRESSION_HEADER_BASE = 30u;
static const uint HIKARI_MATERIAL_EXPRESSION_VALUE_BASE = 34u;
static const uint HIKARI_MATERIAL_EXPRESSION_INSTRUCTION_BASE = 82u;
static const uint HIKARI_MATERIAL_EXPRESSION_MAX_INSTRUCTIONS = 96u;
static const uint HIKARI_MATERIAL_EXPRESSION_MAX_REGISTERS = 128u;
static const uint HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER = 255u;

static const uint HIKARI_EXPRESSION_CONSTANT = 0u;
static const uint HIKARI_EXPRESSION_PARAMETER = 1u;
static const uint HIKARI_EXPRESSION_TEXTURE_SAMPLE = 2u;
static const uint HIKARI_EXPRESSION_ADD = 3u;
static const uint HIKARI_EXPRESSION_SUBTRACT = 4u;
static const uint HIKARI_EXPRESSION_MULTIPLY = 5u;
static const uint HIKARI_EXPRESSION_DIVIDE = 6u;
static const uint HIKARI_EXPRESSION_LERP = 7u;
static const uint HIKARI_EXPRESSION_MINIMUM = 8u;
static const uint HIKARI_EXPRESSION_MAXIMUM = 9u;
static const uint HIKARI_EXPRESSION_CLAMP = 10u;
static const uint HIKARI_EXPRESSION_SATURATE = 11u;
static const uint HIKARI_EXPRESSION_ONE_MINUS = 12u;
static const uint HIKARI_EXPRESSION_POWER = 13u;
static const uint HIKARI_EXPRESSION_ABSOLUTE = 14u;
static const uint HIKARI_EXPRESSION_NEGATE = 15u;
static const uint HIKARI_EXPRESSION_FLOOR = 16u;
static const uint HIKARI_EXPRESSION_CEIL = 17u;
static const uint HIKARI_EXPRESSION_FRACTION = 18u;
static const uint HIKARI_EXPRESSION_MODULO = 19u;
static const uint HIKARI_EXPRESSION_SQUARE_ROOT = 20u;
static const uint HIKARI_EXPRESSION_SINE = 21u;
static const uint HIKARI_EXPRESSION_COSINE = 22u;
static const uint HIKARI_EXPRESSION_STEP = 23u;
static const uint HIKARI_EXPRESSION_SMOOTH_STEP = 24u;
static const uint HIKARI_EXPRESSION_REMAP = 25u;
static const uint HIKARI_EXPRESSION_DOT = 26u;
static const uint HIKARI_EXPRESSION_CROSS = 27u;
static const uint HIKARI_EXPRESSION_LENGTH = 28u;
static const uint HIKARI_EXPRESSION_NORMALIZE = 29u;
static const uint HIKARI_EXPRESSION_DISTANCE = 30u;
static const uint HIKARI_EXPRESSION_REFLECT = 31u;
static const uint HIKARI_EXPRESSION_MAKE_FLOAT2 = 32u;
static const uint HIKARI_EXPRESSION_MAKE_FLOAT3 = 33u;
static const uint HIKARI_EXPRESSION_MAKE_FLOAT4 = 34u;
static const uint HIKARI_EXPRESSION_SPLIT = 35u;
static const uint HIKARI_EXPRESSION_SELECT = 36u;
static const uint HIKARI_EXPRESSION_NORMAL_UNPACK = 37u;
static const uint HIKARI_EXPRESSION_TEXTURE_COORDINATE = 38u;
static const uint HIKARI_EXPRESSION_TIME = 39u;
static const uint HIKARI_EXPRESSION_WORLD_POSITION = 40u;
static const uint HIKARI_EXPRESSION_WORLD_NORMAL = 41u;
static const uint HIKARI_EXPRESSION_WORLD_TANGENT = 42u;
static const uint HIKARI_EXPRESSION_VIEW_DIRECTION = 43u;
static const uint HIKARI_EXPRESSION_CAMERA_POSITION = 44u;
static const uint HIKARI_EXPRESSION_SCREEN_POSITION = 45u;
static const uint HIKARI_EXPRESSION_DERIVATIVE_X = 46u;
static const uint HIKARI_EXPRESSION_DERIVATIVE_Y = 47u;
static const uint HIKARI_EXPRESSION_FILTER_WIDTH = 48u;
static const uint HIKARI_EXPRESSION_ATAN2 = 49u;
static const uint HIKARI_EXPRESSION_EXP = 50u;
static const uint HIKARI_EXPRESSION_LOG2 = 51u;
static const uint HIKARI_EXPRESSION_SIGN = 52u;
static const uint HIKARI_EXPRESSION_ROUND = 53u;
static const uint HIKARI_EXPRESSION_TRUNC = 54u;
static const uint HIKARI_EXPRESSION_UV_TRANSFORM = 55u;
static const uint HIKARI_EXPRESSION_VALUE_NOISE = 56u;
static const uint HIKARI_EXPRESSION_VORONOI = 57u;
static const uint HIKARI_EXPRESSION_FRESNEL = 58u;

static const uint HIKARI_EXPRESSION_OUTPUT_BASE_COLOR = 1u << 0u;
static const uint HIKARI_EXPRESSION_OUTPUT_OPACITY = 1u << 1u;
static const uint HIKARI_EXPRESSION_OUTPUT_NORMAL = 1u << 2u;
static const uint HIKARI_EXPRESSION_OUTPUT_METALLIC = 1u << 3u;
static const uint HIKARI_EXPRESSION_OUTPUT_ROUGHNESS = 1u << 4u;
static const uint HIKARI_EXPRESSION_OUTPUT_OCCLUSION = 1u << 5u;
static const uint HIKARI_EXPRESSION_OUTPUT_EMISSIVE = 1u << 6u;
static const uint HIKARI_EXPRESSION_OUTPUT_SPECULAR = 1u << 7u;
static const uint HIKARI_EXPRESSION_OUTPUT_SPECULAR_COLOR = 1u << 8u;
static const uint HIKARI_EXPRESSION_OUTPUT_ALPHA_CLIP = 1u << 9u;

struct HikariMaterialExpressionContext
{
    float2 uv0;
    float2 uv1;
    float4 time;
    float3 worldPosition;
    float3 worldNormal;
    float4 worldTangent;
    float3 viewDirection;
    float3 cameraPosition;
    float4 screenPosition;
};

struct HikariMaterialExpressionResult
{
    uint outputMask;
    float4 baseColor;
    float opacity;
    float3 normalTS;
    float metallic;
    float roughness;
    float occlusion;
    float3 emissive;
    float specular;
    float3 specularColor;
    float alphaClipThreshold;
};

uint HikariExpressionByte(uint packed, uint index)
{
    return (packed >> (index * 8u)) & 0xffu;
}

bool HikariExpressionSourceValid(uint source, uint registerCount)
{
    return source < registerCount || source == HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER;
}

float4 HikariExpressionRegister(
    float4 registers[HIKARI_MATERIAL_EXPRESSION_MAX_REGISTERS],
    uint index)
{
    return index < HIKARI_MATERIAL_EXPRESSION_MAX_REGISTERS
        ? registers[index]
        : 0.0f.xxxx;
}

float4 HikariExpressionSplatScalar(float4 value, uint components)
{
    return components == 1u ? value.xxxx : value;
}

float4 HikariExpressionSafeDenominator(float4 value)
{
    const float4 direction = step(0.0f.xxxx, value) * 2.0f - 1.0f;
    return direction * max(abs(value), 1.0e-6f.xxxx);
}

float HikariExpressionDot(float4 a, float4 b, uint components)
{
    if (components <= 1u) return a.x * b.x;
    if (components == 2u) return dot(a.xy, b.xy);
    if (components == 3u) return dot(a.xyz, b.xyz);
    return dot(a, b);
}

float4 HikariExpressionNormalize(float4 value, uint components)
{
    if (components <= 1u) return sign(value.x).xxxx;
    const float lengthSquared = HikariExpressionDot(
        value, value, components);
    if (lengthSquared <= 1.0e-10f) return 0.0f.xxxx;
    return value * rsqrt(lengthSquared);
}

float2 HikariExpressionHash22(float2 p)
{
    uint2 q = asuint(int2(p));
    q = q * uint2(1597334673u, 3812015801u);
    uint n0 = (q.x ^ q.y) * 1597334673u;
    n0 = n0 ^ (n0 >> 15u);
    n0 = n0 * 2246822519u;
    n0 = n0 ^ (n0 >> 13u);
    uint n1 = n0 * 3266489917u;
    n0 = n0 ^ (n0 >> 16u);
    n1 = n1 ^ (n1 >> 16u);
    return float2(
        float(n0 & 0xffffffu) / 16777215.0f,
        float(n1 & 0xffffffu) / 16777215.0f);
}

HikariMaterialExpressionResult HikariEvaluateMaterialExpression(
    HikariMeshMaterialData materialData,
    HikariMaterialExpressionContext context,
    SamplerState materialSampler)
{
    HikariMaterialExpressionResult result =
        (HikariMaterialExpressionResult)0;
    const uint4 header = asuint(HikariLoadMaterialProgramParameter(
        materialData,
        HIKARI_MATERIAL_EXPRESSION_HEADER_BASE,
        0.0f.xxxx));
    if (header.x != HIKARI_MATERIAL_EXPRESSION_MAGIC ||
        header.y != HIKARI_MATERIAL_EXPRESSION_VERSION ||
        header.z > HIKARI_MATERIAL_EXPRESSION_MAX_INSTRUCTIONS ||
        header.w > HIKARI_MATERIAL_EXPRESSION_MAX_REGISTERS ||
        materialData.parameterBlockCount < HIKARI_MATERIAL_EXPRESSION_INSTRUCTION_BASE + header.z)
    {
        return result;
    }

    const uint4 outputHeader = asuint(HikariLoadMaterialProgramParameter(
        materialData,
        HIKARI_MATERIAL_EXPRESSION_HEADER_BASE + 1u,
        0.0f.xxxx));
    const uint4 valueHeader = asuint(HikariLoadMaterialProgramParameter(
        materialData, HIKARI_MATERIAL_EXPRESSION_HEADER_BASE + 2u, 0.0f.xxxx));
    if (valueHeader.x > HIKARI_MATERIAL_EXPRESSION_INSTRUCTION_BASE - HIKARI_MATERIAL_EXPRESSION_VALUE_BASE ||
        valueHeader.y != HIKARI_MATERIAL_EXPRESSION_VALUE_BASE ||
        valueHeader.z != HIKARI_MATERIAL_EXPRESSION_INSTRUCTION_BASE ||
        (outputHeader.x & ~0x3ffu) != 0u)
        return result;
    [unroll]
    for (uint output = 0u; output < 10u; ++output)
    {
        if ((outputHeader.x & (1u << output)) != 0u &&
            HikariExpressionByte(outputHeader[1u + output / 4u], output % 4u) >= header.w)
            return result;
    }
    float4 registers[HIKARI_MATERIAL_EXPRESSION_MAX_REGISTERS];
    [unroll]
    for (uint registerIndex = 0u;
         registerIndex < HIKARI_MATERIAL_EXPRESSION_MAX_REGISTERS;
         ++registerIndex)
    {
        registers[registerIndex] = 0.0f.xxxx;
    }
    [loop]
    for (uint instructionIndex = 0u;
         instructionIndex < header.z;
         ++instructionIndex)
    {
        const float4 encoded = HikariLoadMaterialProgramParameter(
            materialData,
            HIKARI_MATERIAL_EXPRESSION_INSTRUCTION_BASE + instructionIndex,
            0.0f.xxxx);
        const uint instructionHeader = asuint(encoded.x);
        const uint opcode = HikariExpressionByte(instructionHeader, 0u);
        const uint components = HikariExpressionByte(instructionHeader, 1u);
        const uint destination = HikariExpressionByte(instructionHeader, 2u);
        const uint outputCount = HikariExpressionByte(instructionHeader, 3u);
        const uint packedInputs = asuint(encoded.y);
        const uint packedData = asuint(encoded.z);
        const uint source0 = HikariExpressionByte(packedInputs, 0u);
        const uint source1 = HikariExpressionByte(packedInputs, 1u);
        const uint source2 = HikariExpressionByte(packedInputs, 2u);
        const uint source3 = HikariExpressionByte(packedInputs, 3u);
        const uint source4 = HikariExpressionByte(packedData, 0u);
        const uint binding = HikariExpressionByte(packedData, 1u);
        const uint valueIndex = HikariExpressionByte(packedData, 2u);
        const uint auxiliary = HikariExpressionByte(packedData, 3u);
        const uint maxOutputs = opcode == HIKARI_EXPRESSION_TEXTURE_SAMPLE ? 7u :
            opcode == HIKARI_EXPRESSION_SPLIT || opcode == HIKARI_EXPRESSION_TIME ? 4u :
            opcode == HIKARI_EXPRESSION_TEXTURE_COORDINATE ? 2u : 1u;
        // 書き込み前に範囲を検査し、不正なプログラムは部分結果を公開しない。
        if (opcode > HIKARI_EXPRESSION_FRESNEL || components < 1u || components > 4u ||
            outputCount < 1u || outputCount > maxOutputs ||
            destination + outputCount > header.w ||
            !HikariExpressionSourceValid(source0, header.w) ||
            !HikariExpressionSourceValid(source1, header.w) ||
            !HikariExpressionSourceValid(source2, header.w) ||
            !HikariExpressionSourceValid(source3, header.w) ||
            !HikariExpressionSourceValid(source4, header.w))
            return result;
        if ((opcode == HIKARI_EXPRESSION_CONSTANT || opcode == HIKARI_EXPRESSION_PARAMETER ||
             opcode == HIKARI_EXPRESSION_TEXTURE_SAMPLE) && valueIndex >= valueHeader.x)
            return result;
        if (opcode == HIKARI_EXPRESSION_TEXTURE_SAMPLE &&
            (binding >= HIKARI_MATERIAL_PROGRAM_RESOURCE_SLOTS || auxiliary > 1u))
            return result;
        const float4 a = HikariExpressionRegister(registers, source0);
        const float4 b = HikariExpressionRegister(registers, source1);
        const float4 c = HikariExpressionRegister(registers, source2);
        const float4 d = HikariExpressionRegister(registers, source3);
        const float4 e = HikariExpressionRegister(registers, source4);
        float4 value = 0.0f.xxxx;

        if (opcode == HIKARI_EXPRESSION_CONSTANT ||
            opcode == HIKARI_EXPRESSION_PARAMETER)
        {
            value = HikariLoadMaterialProgramParameter(
                materialData,
                HIKARI_MATERIAL_EXPRESSION_VALUE_BASE + valueIndex,
                0.0f.xxxx);
        }
        else if (opcode == HIKARI_EXPRESSION_TEXTURE_SAMPLE)
        {
            float2 uv = source0 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                ? a.xy
                : (auxiliary == 0u ? context.uv0 : context.uv1);
            const float4 transform = HikariLoadMaterialProgramParameter(
                materialData,
                HIKARI_MATERIAL_EXPRESSION_VALUE_BASE + valueIndex,
                float4(1.0f, 1.0f, 0.0f, 0.0f));
            uv = HikariApplyMaterialUvTransform(uv, transform, encoded.w);
            value = HikariSampleMaterialProgramTexture(
                materialData, binding, materialSampler, uv, 1.0f.xxxx);
            registers[destination] = value;
            if (outputCount > 1u) registers[destination + 1u] = float4(value.rgb, 0.0f);
            if (outputCount > 2u) registers[destination + 2u] = value.r.xxxx;
            if (outputCount > 3u) registers[destination + 3u] = value.g.xxxx;
            if (outputCount > 4u) registers[destination + 4u] = value.b.xxxx;
            if (outputCount > 5u) registers[destination + 5u] = value.a.xxxx;
            if (outputCount > 6u) registers[destination + 6u] = value.a.xxxx;
            continue;
        }
        else if (opcode == HIKARI_EXPRESSION_ADD) value = a + b;
        else if (opcode == HIKARI_EXPRESSION_SUBTRACT) value = a - b;
        else if (opcode == HIKARI_EXPRESSION_MULTIPLY) value = a * b;
        else if (opcode == HIKARI_EXPRESSION_DIVIDE) value = a / HikariExpressionSafeDenominator(b);
        else if (opcode == HIKARI_EXPRESSION_LERP) value = lerp(a, b, c);
        else if (opcode == HIKARI_EXPRESSION_MINIMUM) value = min(a, b);
        else if (opcode == HIKARI_EXPRESSION_MAXIMUM) value = max(a, b);
        else if (opcode == HIKARI_EXPRESSION_CLAMP) value = clamp(a, b, c);
        else if (opcode == HIKARI_EXPRESSION_SATURATE) value = saturate(a);
        else if (opcode == HIKARI_EXPRESSION_ONE_MINUS) value = 1.0f.xxxx - a;
        else if (opcode == HIKARI_EXPRESSION_POWER) value = pow(max(a, 0.0f.xxxx), b);
        else if (opcode == HIKARI_EXPRESSION_ABSOLUTE) value = abs(a);
        else if (opcode == HIKARI_EXPRESSION_NEGATE) value = -a;
        else if (opcode == HIKARI_EXPRESSION_FLOOR) value = floor(a);
        else if (opcode == HIKARI_EXPRESSION_CEIL) value = ceil(a);
        else if (opcode == HIKARI_EXPRESSION_FRACTION) value = frac(a);
        else if (opcode == HIKARI_EXPRESSION_MODULO) value = a - b * floor(a / HikariExpressionSafeDenominator(b));
        else if (opcode == HIKARI_EXPRESSION_SQUARE_ROOT) value = sqrt(max(a, 0.0f.xxxx));
        else if (opcode == HIKARI_EXPRESSION_SINE) value = sin(a);
        else if (opcode == HIKARI_EXPRESSION_COSINE) value = cos(a);
        else if (opcode == HIKARI_EXPRESSION_STEP) value = step(a, b);
        else if (opcode == HIKARI_EXPRESSION_SMOOTH_STEP) value = smoothstep(a, b, c);
        else if (opcode == HIKARI_EXPRESSION_REMAP)
            value = d + (a - b) /
                HikariExpressionSafeDenominator(c - b) * (e - d);
        else if (opcode == HIKARI_EXPRESSION_DOT) value = HikariExpressionDot(a, b, components).xxxx;
        else if (opcode == HIKARI_EXPRESSION_CROSS) value = float4(cross(a.xyz, b.xyz), 0.0f);
        else if (opcode == HIKARI_EXPRESSION_LENGTH)
            value = sqrt(max(
                HikariExpressionDot(a, a, components), 0.0f)).xxxx;
        else if (opcode == HIKARI_EXPRESSION_NORMALIZE) value = HikariExpressionNormalize(a, components);
        else if (opcode == HIKARI_EXPRESSION_DISTANCE)
            value = sqrt(max(HikariExpressionDot(
                a - b, a - b, components), 0.0f)).xxxx;
        else if (opcode == HIKARI_EXPRESSION_REFLECT) value = float4(reflect(a.xyz, b.xyz), 0.0f);
        else if (opcode == HIKARI_EXPRESSION_MAKE_FLOAT2) value = float4(a.x, b.x, 0.0f, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_MAKE_FLOAT3) value = float4(a.x, b.x, c.x, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_MAKE_FLOAT4) value = float4(a.x, b.x, c.x, d.x);
        else if (opcode == HIKARI_EXPRESSION_SPLIT)
        {
            registers[destination] = a.xxxx;
            if (outputCount > 1u) registers[destination + 1u] = a.yyyy;
            if (outputCount > 2u) registers[destination + 2u] = a.zzzz;
            if (outputCount > 3u) registers[destination + 3u] = a.wwww;
            continue;
        }
        else if (opcode == HIKARI_EXPRESSION_SELECT) value = c.x >= 0.5f ? b : a;
        else if (opcode == HIKARI_EXPRESSION_NORMAL_UNPACK)
        {
            const float2 xy = a.xy * 2.0f - 1.0f;
            value = float4(HikariExpressionNormalize(
                float4(xy, sqrt(saturate(1.0f - dot(xy, xy))), 0.0f),
                3u).xyz, 0.0f);
        }
        else if (opcode == HIKARI_EXPRESSION_TEXTURE_COORDINATE)
        {
            registers[destination] = float4(context.uv0, 0.0f, 0.0f);
            if (outputCount > 1u) registers[destination + 1u] = float4(context.uv1, 0.0f, 0.0f);
            continue;
        }
        else if (opcode == HIKARI_EXPRESSION_TIME)
        {
            registers[destination] = context.time.xxxx;
            if (outputCount > 1u) registers[destination + 1u] = context.time.yyyy;
            if (outputCount > 2u) registers[destination + 2u] = context.time.zzzz;
            if (outputCount > 3u) registers[destination + 3u] = context.time.wwww;
            continue;
        }
        else if (opcode == HIKARI_EXPRESSION_WORLD_POSITION) value = float4(context.worldPosition, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_WORLD_NORMAL) value = float4(context.worldNormal, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_WORLD_TANGENT) value = float4(context.worldTangent.xyz, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_VIEW_DIRECTION) value = float4(context.viewDirection, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_CAMERA_POSITION) value = float4(context.cameraPosition, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_SCREEN_POSITION) value = context.screenPosition;
#if !defined(HIKARI_MATERIAL_EXPLICIT_LOD)
        else if (opcode == HIKARI_EXPRESSION_DERIVATIVE_X) value = ddx(a);
        else if (opcode == HIKARI_EXPRESSION_DERIVATIVE_Y) value = ddy(a);
        else if (opcode == HIKARI_EXPRESSION_FILTER_WIDTH) value = fwidth(a);
#endif
        else if (opcode == HIKARI_EXPRESSION_ATAN2)
            value = float4(atan2(a.x, b.x), 0.0f, 0.0f, 0.0f);
        else if (opcode == HIKARI_EXPRESSION_EXP) value = exp(a);
        else if (opcode == HIKARI_EXPRESSION_LOG2)
            value = log2(max(a, 1.0e-6f.xxxx));
        else if (opcode == HIKARI_EXPRESSION_SIGN) value = sign(a);
        else if (opcode == HIKARI_EXPRESSION_ROUND) value = round(a);
        else if (opcode == HIKARI_EXPRESSION_TRUNC) value = trunc(a);
        else if (opcode == HIKARI_EXPRESSION_UV_TRANSFORM)
        {
            const float2 uv =
                source0 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? a.xy : context.uv0;
            const float2 pivot =
                source1 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? b.xy : 0.0f.xx;
            const float2 scale =
                source2 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? c.xy : 1.0f.xx;
            const float2 offset =
                source3 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? d.xy : 0.0f.xx;
            const float rotation =
                source4 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? e.x : 0.0f;
            const float s = sin(rotation);
            const float c2 = cos(rotation);
            const float2 centered = uv - pivot;
            const float2 rotated = float2(
                centered.x * c2 - centered.y * s,
                centered.x * s + centered.y * c2);
            value = float4(rotated * scale + pivot + offset, 0.0f, 0.0f);
        }
        else if (opcode == HIKARI_EXPRESSION_VALUE_NOISE)
        {
            const float scale =
                source1 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? b.x : 8.0f;
            const float2 uv =
                source0 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? a.xy : context.uv0;
            const float2 p = uv * scale;
            const float2 cell = floor(p);
            const float2 f = frac(p);
            const float2 u = f * f * (3.0f - 2.0f * f);
            const float h00 = HikariExpressionHash22(cell).x;
            const float h10 = HikariExpressionHash22(
                cell + float2(1.0f, 0.0f)).x;
            const float h01 = HikariExpressionHash22(
                cell + float2(0.0f, 1.0f)).x;
            const float h11 = HikariExpressionHash22(
                cell + float2(1.0f, 1.0f)).x;
            const float v0 = lerp(h00, h10, u.x);
            const float v1 = lerp(h01, h11, u.x);
            value = lerp(v0, v1, u.y).xxxx;
        }
        else if (opcode == HIKARI_EXPRESSION_VORONOI)
        {
            const float scale =
                source1 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? b.x : 8.0f;
            const float2 uv =
                source0 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? a.xy : context.uv0;
            const float2 p = uv * scale;
            const float2 cell = floor(p);
            const float2 f = frac(p);
            float minDistance = 8.0f;
            [unroll]
            for (int oy = -1; oy <= 1; ++oy)
            {
                [unroll]
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const float2 neighbor = float2((float)ox, (float)oy);
                    const float2 samplePoint = cell + neighbor;
                    const float2 hash = HikariExpressionHash22(samplePoint);
                    const float2 offset = neighbor + hash - f;
                    minDistance = min(
                        minDistance, dot(offset, offset));
                }
            }
            value = sqrt(minDistance).xxxx;
        }
        else if (opcode == HIKARI_EXPRESSION_FRESNEL)
        {
            const float3 n =
                source0 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? normalize(a.xyz) : normalize(context.worldNormal);
            const float3 v =
                source1 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? normalize(b.xyz) : normalize(context.viewDirection);
            const float f0 =
                source2 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? c.x : 0.04f;
            const float power =
                source3 != HIKARI_MATERIAL_EXPRESSION_INVALID_REGISTER
                    ? d.x : 5.0f;
            const float ndv = abs(dot(n, v));
            value = (f0 + (1.0f - f0) *
                pow(1.0f - saturate(ndv), power)).xxxx;
        }

        registers[destination] = HikariExpressionSplatScalar(value, components);
    }

    result.outputMask = outputHeader.x;
    const uint packed0 = outputHeader.y;
    const uint packed1 = outputHeader.z;
    const uint packed2 = outputHeader.w;
    result.baseColor = HikariExpressionRegister(registers, HikariExpressionByte(packed0, 0u));
    result.opacity = HikariExpressionRegister(registers, HikariExpressionByte(packed0, 1u)).x;
    result.normalTS = HikariExpressionRegister(registers, HikariExpressionByte(packed0, 2u)).xyz;
    result.metallic = HikariExpressionRegister(registers, HikariExpressionByte(packed0, 3u)).x;
    result.roughness = HikariExpressionRegister(registers, HikariExpressionByte(packed1, 0u)).x;
    result.occlusion = HikariExpressionRegister(registers, HikariExpressionByte(packed1, 1u)).x;
    result.emissive = HikariExpressionRegister(registers, HikariExpressionByte(packed1, 2u)).xyz;
    result.specular = HikariExpressionRegister(registers, HikariExpressionByte(packed1, 3u)).x;
    result.specularColor = HikariExpressionRegister(registers, HikariExpressionByte(packed2, 0u)).xyz;
    result.alphaClipThreshold = HikariExpressionRegister(registers, HikariExpressionByte(packed2, 1u)).x;
    return result;
}

#endif
