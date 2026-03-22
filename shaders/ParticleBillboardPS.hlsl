// ============================================================
// ParticleBillboardPS.hlsl — Soft-circle billboard fragment shader
//   Uses procedural soft circle when no texture is bound,
//   otherwise samples gParticleTexture.
//   Includes soft-particle depth fade for polished blending.
// ============================================================

#include "Common.hlsli"

Texture2D    gParticleTexture : register(t4);
Texture2D<float> gSceneDepth : register(t5); // Scene depth for soft particles
SamplerState gSamplerLinearP  : register(s1); // re-use engine linear sampler

struct PSInput {
    float4 position  : SV_POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 color     : COLOR0;
    float  life      : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Procedural soft circle falloff — multi-ring for a richer look
    float2 uv = input.texCoord * 2.0 - 1.0; // -1..1
    float dist2 = dot(uv, uv);

    // Smooth radial falloff with a bright core and soft outer glow
    float coreAlpha = 1.0 - smoothstep(0.0, 0.35, dist2);   // Bright hot center
    float outerAlpha = 1.0 - smoothstep(0.35, 1.0, dist2);  // Soft outer ring
    float alpha = coreAlpha * 0.6 + outerAlpha * 0.4;

    // Texture overlay (if bound — adds detail; if 1x1 white, no effect)
    float4 texColor = gParticleTexture.Sample(gSamplerLinearP, input.texCoord);

    float4 result = input.color;
    result.a *= alpha * texColor.a;
    result.rgb *= texColor.rgb;

    // ---- Soft-particle depth fade ----
    // Fade particle near scene geometry to avoid hard cutoffs
    uint2 depthDims;
    gSceneDepth.GetDimensions(depthDims.x, depthDims.y);
    if (depthDims.x > 1) { // Only if a real depth texture is bound
        float2 screenUV = input.position.xy / float2(depthDims);
        float sceneZ = gSceneDepth.Load(int3(input.position.xy, 0));
        float particleZ = input.position.z;
        float depthDiff = sceneZ - particleZ;
        // Soft fade over a narrow depth range
        float softFade = saturate(depthDiff * 800.0);
        result.a *= softFade;
    }

    // Discard fully transparent
    if (result.a < 0.004) discard;

    return result;
}
