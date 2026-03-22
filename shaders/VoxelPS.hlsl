// Voxel Pixel Shader — Blinn-Phong or Cel-shaded lighting with fog and tone mapping
#include "Common.hlsli"

float4 PSMain(VoxelPSInput input) : SV_TARGET {
    // Determine UV coordinates
    float2 uv = input.TexCoord;

    // When ObjectColor.a > 0, this is a level entity — use world-space UVs for proper tiling.
    // For hard-surface cubes, dominant-axis projection is more stable than blended triplanar
    // and avoids diagonal smearing on rotated walls.
    if (gObjectColor.a > 0.0f) {
        float3 absN = abs(input.Normal);
        if (absN.x > absN.y && absN.x > absN.z) {
            uv = input.WorldPos.yz;   // X-facing wall
        } else if (absN.y > absN.z) {
            uv = input.WorldPos.xz;   // Floor / roof
        } else {
            uv = input.WorldPos.xy;   // Z-facing wall
        }
    }

    // Sample diffuse texture (white 1x1 when no texture bound = no effect)
    float3 texColor = gDiffuseTexture.Sample(gSamplerLinear, uv).rgb;

    float3 baseColor = (gObjectColor.a > 0.0f) ? gObjectColor.rgb : input.Color.rgb;
    float3 albedo = baseColor * texColor;

    // Hit decal darkening — bullet scars burn dark spots on surfaces
    int decalCount = (int)gHitDecalCount;
    for (int di = 0; di < decalCount; di++) {
        float3 decalPos = gHitDecals[di].xyz;
        float  intensity = gHitDecals[di].w;
        float  dist = length(input.WorldPos - decalPos);
        float  radius = 0.35f;  // decal radius in world units
        float  falloff = 1.0f - saturate(dist / radius);
        falloff = falloff * falloff;  // quadratic falloff for softer edges
        albedo *= lerp(1.0f, 0.2f, falloff * intensity);  // darken up to 80%
    }

    // Choose lighting model
    float3 litColor;
    if (gCelEnabled > 0.5f) {
        litColor = CalcCelShadowed(input.Normal, input.WorldPos, albedo, 64.0f);
    } else {
        litColor = CalcBlinnPhongShadowed(input.Normal, input.WorldPos, albedo, 32.0f);
    }

    // Distance fog
    litColor = ApplyFog(litColor, input.WorldPos);

    // Tone mapping for HDR-like feel
    litColor = ToneMapReinhard(litColor);

    return float4(litColor, input.Color.a);
}
