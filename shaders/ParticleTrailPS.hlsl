// ============================================================
// ParticleTrailPS.hlsl — Trail / ribbon pixel shader
//   Soft-edge fade on ribbon edges
// ============================================================

#include "Common.hlsli"

Texture2D    gTrailTexture : register(t4);
SamplerState gSamplerLin   : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float4 tex = gTrailTexture.Sample(gSamplerLin, input.texCoord);

    // Soft edge fade across ribbon width (U axis)
    float edgeFade = 1.0 - abs(input.texCoord.x * 2.0 - 1.0);
    edgeFade = smoothstep(0.0, 0.3, edgeFade);

    float4 result = input.color * tex;
    result.a *= edgeFade;

    if (result.a < 0.004) discard;
    return result;
}
