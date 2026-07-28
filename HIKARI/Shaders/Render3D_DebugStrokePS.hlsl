struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    noperspective float edgeDistancePixels : TEXCOORD0;
    nointerpolation float halfWidthPixels : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    const float coverage = saturate(
        input.halfWidthPixels + 1.0f -
        abs(input.edgeDistancePixels));
    return float4(
        input.color.rgb,
        input.color.a * coverage);
}
