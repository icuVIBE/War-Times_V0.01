// ============================================================
// ParticleBillboardVS.hlsl — Expand point particles to
//   camera-facing quads using SV_VertexID + SV_InstanceID
// ============================================================

#include "Common.hlsli"
#include "ParticleCommon.hlsli"

StructuredBuffer<GPUParticle> ParticlePool : register(t2);
StructuredBuffer<uint>        AliveList    : register(t3);

struct VSOutput {
    float4 position  : SV_POSITION;
    float2 texCoord  : TEXCOORD0;
    float4 color     : COLOR0;
    float  life      : TEXCOORD1; // normalized 0..1
};

// Two-triangle quad corners and UVs (6 vertices = 2 triangles)
static const float2 kCorners[6] = {
    float2(-0.5, -0.5), float2(-0.5,  0.5), float2( 0.5,  0.5),
    float2(-0.5, -0.5), float2( 0.5,  0.5), float2( 0.5, -0.5)
};
static const float2 kUVs[6] = {
    float2(0, 1), float2(0, 0), float2(1, 0),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput o;

    uint particleIdx = AliveList[instanceID];
    GPUParticle p = ParticlePool[particleIdx];

    float t = saturate(p.age / max(p.lifetime, 0.001));

    // Interpolate size and color over lifetime
    float size = lerp(p.sizeStart, p.sizeEnd, t);
    float4 color = lerp(p.colorStart, p.colorEnd, t);

    // Fade alpha smoothly in last 20% of life
    if (t > 0.8)
        color.a *= 1.0 - (t - 0.8) / 0.2;

    // Billboard corner
    float2 corner = kCorners[vertexID];

    // Apply rotation
    float s, c;
    sincos(p.rotation, s, c);
    float2 rotated = float2(
        corner.x * c - corner.y * s,
        corner.x * s + corner.y * c
    );

    // Camera right & up for billboarding (from view matrix)
    float3 right = float3(gView[0][0], gView[1][0], gView[2][0]);
    float3 up    = float3(gView[0][1], gView[1][1], gView[2][1]);

    float3 worldPos = p.position
        + right * rotated.x * size
        + up    * rotated.y * size;

    o.position = mul(float4(worldPos, 1.0), gViewProjection);
    o.texCoord = kUVs[vertexID];
    o.color    = color;
    o.life     = t;

    return o;
}
