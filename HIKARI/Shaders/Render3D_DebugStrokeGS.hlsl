struct GSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    nointerpolation float thicknessPixels : TEXCOORD0;
};

struct GSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    noperspective float edgeDistancePixels : TEXCOORD0;
    nointerpolation float halfWidthPixels : TEXCOORD1;
};

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
    float4 gViewport;
};

void ClipEndpointToNearPlane(inout float4 outsidePoint, float4 insidePoint)
{
    const float denominator = insidePoint.z - outsidePoint.z;
    if (abs(denominator) > 1.0e-6f)
    {
        const float t = saturate(-outsidePoint.z / denominator);
        outsidePoint = lerp(outsidePoint, insidePoint, t);
    }
}

[maxvertexcount(4)]
void main(line GSInput input[2], inout TriangleStream<GSOutput> stream)
{
    float4 p0 = input[0].position;
    float4 p1 = input[1].position;
    if (p0.z < 0.0f && p1.z < 0.0f)
    {
        return;
    }
    if (p0.z < 0.0f)
    {
        ClipEndpointToNearPlane(p0, p1);
    }
    else if (p1.z < 0.0f)
    {
        ClipEndpointToNearPlane(p1, p0);
    }
    if (p0.w <= 1.0e-5f || p1.w <= 1.0e-5f)
    {
        return;
    }

    const float2 ndc0 = p0.xy / p0.w;
    const float2 ndc1 = p1.xy / p1.w;
    const float2 pixelDelta =
        (ndc1 - ndc0) * max(gViewport.xy * 0.5f, float2(1.0f, 1.0f));
    const float lengthPixels = length(pixelDelta);
    if (lengthPixels <= 1.0e-4f)
    {
        return;
    }

    const float2 perpendicular =
        float2(-pixelDelta.y, pixelDelta.x) / lengthPixels;
    const float halfWidth =
        max(input[0].thicknessPixels * 0.5f, 0.5f);
    const float expandedHalfWidth = halfWidth + 1.0f;
    const float2 ndcOffset =
        perpendicular * expandedHalfWidth * 2.0f *
        max(gViewport.zw, float2(1.0e-6f, 1.0e-6f));

    GSOutput output;
    output.color = input[0].color;
    output.halfWidthPixels = halfWidth;

    output.position = p0;
    output.position.xy -= ndcOffset * p0.w;
    output.edgeDistancePixels = -expandedHalfWidth;
    stream.Append(output);

    output.position = p0;
    output.position.xy += ndcOffset * p0.w;
    output.edgeDistancePixels = expandedHalfWidth;
    stream.Append(output);

    output.position = p1;
    output.position.xy -= ndcOffset * p1.w;
    output.edgeDistancePixels = -expandedHalfWidth;
    stream.Append(output);

    output.position = p1;
    output.position.xy += ndcOffset * p1.w;
    output.edgeDistancePixels = expandedHalfWidth;
    stream.Append(output);
}
