// ============================================================
// ParticleSimulateCS.hlsl — Update particles, depth collision,
//                           build alive list for rendering
// Dispatch: ceil(maxParticles / 256)
// ============================================================

#include "Common.hlsli"          // CBPerFrame at b0 (gView, gProjection, etc.)
#include "ParticleCommon.hlsli"  // CBParticleSim at b6, GPUParticle, RNG

RWStructuredBuffer<GPUParticle> ParticlePool : register(u0);
RWStructuredBuffer<uint>        DeadList     : register(u1);
RWByteAddressBuffer             Counters     : register(u2);  // [0]=deadCount, [4]=aliveCount
RWStructuredBuffer<uint>        AliveList    : register(u3);

// Depth buffer for collision (scene depth, non-MSAA)
Texture2D<float> gDepthTexture : register(t0);

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    uint idx = dtid.x;
    uint maxP = (uint)gPMaxParticles;
    if (idx >= maxP) return;

    GPUParticle p = ParticlePool[idx];
    if ((p.alive & 1u) == 0) return;

    // ---- Age ----
    p.age += gDeltaTime;
    if (p.age >= p.lifetime) {
        p.alive = 0;
        ParticlePool[idx] = p;

        // Push to dead list
        uint deadIdx;
        Counters.InterlockedAdd(0, 1, deadIdx);
        DeadList[deadIdx] = idx;
        return;
    }

    // ---- Physics ----
    float dt = gDeltaTime;

    // Per-particle gravity (positive = downward, negative = rises)
    p.velocity.y -= p.gravity * dt;

    // Drag
    if (p.drag > 0.0) {
        float dragFactor = max(0.0, 1.0 - p.drag * dt);
        p.velocity *= dragFactor;
    }

    // Integrate
    float3 newPos = p.position + p.velocity * dt;

    // ---- Depth-Buffer Collision (only for particles with depthCollision enabled) ----
    bool hasDepthCollision = (p.alive & 2u) != 0;
    if (hasDepthCollision) {
    // Project new position to screen, sample depth, check if behind surface
    float4 clipPos = mul(float4(newPos, 1.0), gViewProjection);
    float ndcZ = clipPos.z / clipPos.w;
    float2 screenUV = float2(clipPos.x / clipPos.w * 0.5 + 0.5,
                              -clipPos.y / clipPos.w * 0.5 + 0.5);

    // Only check if on screen
    if (screenUV.x >= 0.0 && screenUV.x <= 1.0 &&
        screenUV.y >= 0.0 && screenUV.y <= 1.0 &&
        ndcZ > 0.0 && ndcZ < 1.0) {

        uint2 texSize;
        gDepthTexture.GetDimensions(texSize.x, texSize.y);
        uint2 texCoord = uint2(screenUV * float2(texSize));
        float sceneDepth = gDepthTexture.Load(int3(texCoord, 0));

        // If particle is behind scene geometry → collision
        float penetration = ndcZ - sceneDepth;
        if (penetration > 0.001) {
            // Bounce: reflect velocity, lose energy
            // Reconstruct approximate surface normal from depth gradient
            float depthL = gDepthTexture.Load(int3(max(0, (int)texCoord.x - 1), texCoord.y, 0));
            float depthR = gDepthTexture.Load(int3(min(texSize.x - 1, texCoord.x + 1), texCoord.y, 0));
            float depthU = gDepthTexture.Load(int3(texCoord.x, max(0, (int)texCoord.y - 1), 0));
            float depthD = gDepthTexture.Load(int3(texCoord.x, min(texSize.y - 1, texCoord.y + 1), 0));

            float3 screenNormal = normalize(float3(depthL - depthR, depthU - depthD, 0.02));

            // Transform screen-space normal to rough world-space estimate
            // (approximate — use camera basis)
            float3 camRight = float3(gView[0][0], gView[1][0], gView[2][0]);
            float3 camUp    = float3(gView[0][1], gView[1][1], gView[2][1]);
            float3 camFwd   = float3(gView[0][2], gView[1][2], gView[2][2]);
            float3 worldNormal = normalize(
                camRight * screenNormal.x +
                camUp    * screenNormal.y +
                camFwd   * screenNormal.z
            );

            // Reflect velocity
            float3 reflected = reflect(p.velocity, worldNormal);
            p.velocity = reflected * p.bounciness;

            // Push particle out of surface
            newPos = p.position; // revert to old position

            // Lose some lifetime on collision
            p.age += p.lifetime * 0.15;
        }
    }
    } // end hasDepthCollision

    // ---- Simple ground plane collision (y=0 fallback) ----
    if (newPos.y < 0.01) {
        newPos.y = 0.01;
        p.velocity.y = abs(p.velocity.y) * p.bounciness;
        p.velocity.xz *= 0.8;
        if (abs(p.velocity.y) < 0.1) {
            p.velocity = (float3)0;
        }
    }

    p.position = newPos;

    // ---- Rotation ----
    p.rotation += p.rotationSpeed * dt;

    // ---- Write back ----
    ParticlePool[idx] = p;

    // ---- Add to alive list for rendering ----
    uint aliveIdx;
    Counters.InterlockedAdd(4, 1, aliveIdx);
    AliveList[aliveIdx] = idx;
}
