// ============================================================
// ParticleSpawnCS.hlsl — Spawn new particles from emitter
// Dispatch: ceil(spawnCount / 256)
// ============================================================

#include "ParticleCommon.hlsli"

RWStructuredBuffer<GPUParticle> ParticlePool : register(u0);
RWStructuredBuffer<uint>        DeadList     : register(u1);
RWByteAddressBuffer             Counters     : register(u2);

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    uint spawnIdx = dtid.x;
    if (spawnIdx >= (uint)gSpawnCount) return;

    // Pop from dead list (atomic decrement)
    uint origDeadCount;
    Counters.InterlockedAdd(0, 0xFFFFFFFF, origDeadCount); // subtract 1
    if (origDeadCount == 0) {
        // No free slots — undo decrement
        Counters.InterlockedAdd(0, 1, origDeadCount);
        return;
    }

    uint particleIdx = DeadList[origDeadCount - 1u];

    // Per-particle unique seed
    uint seed = asuint(gEmitRandomSeed) + spawnIdx * 104729u;

    // Compute spawn position based on shape
    float3 offset = (float3)0;
    if (gEmitShape == 1) {       // Sphere
        offset = RandomInSphere(gEmitShapeRadius, seed + 1u);
    } else if (gEmitShape == 2) { // Cone
        // Cone is forward direction randomized
    } else if (gEmitShape == 3) { // Box
        offset = RandomInBox(gEmitShapeBoxHalf, seed + 2u);
    }

    // Compute velocity
    float3 vel;
    if (gEmitShape == 2) {
        // Cone emission: random direction within cone angle, aimed along emitter direction
        float halfAngle = radians(gEmitConeAngle);
        float3 aimDir = normalize(gEmitDirection);
        float3 dir = RandomInCone(aimDir, halfAngle, seed + 3u);
        vel = dir * gEmitSpeed;
    } else {
        vel = float3(
            RandomRange(gEmitVelocityMin.x, gEmitVelocityMax.x, seed + 10u),
            RandomRange(gEmitVelocityMin.y, gEmitVelocityMax.y, seed + 20u),
            RandomRange(gEmitVelocityMin.z, gEmitVelocityMax.z, seed + 30u)
        ) * gEmitSpeed;
    }

    // Initialize particle
    GPUParticle p;
    p.position     = gEmitPosition + offset;
    p.age          = 0.0;
    p.velocity     = vel;
    p.lifetime     = RandomRange(gEmitLifetimeMin, gEmitLifetimeMax, seed + 40u);
    p.colorStart   = gEmitColorStart;
    p.colorEnd     = gEmitColorEnd;
    p.sizeStart    = RandomRange(gEmitSizeStartMin, gEmitSizeStartMax, seed + 50u);
    p.sizeEnd      = RandomRange(gEmitSizeEndMin,   gEmitSizeEndMax,   seed + 60u);
    p.rotation     = RandomRange(gEmitRotMin,        gEmitRotMax,       seed + 70u);
    p.rotationSpeed = RandomRange(gEmitRotSpeedMin,  gEmitRotSpeedMax,  seed + 80u);
    p.alive        = 1u | ((gEmitFlags & 1u) << 1u); // bit0=alive, bit1=depthCollision
    p.gravity      = gEmitGravity;
    p.bounciness   = gEmitBounciness;
    p.drag         = gEmitDrag;

    ParticlePool[particleIdx] = p;
}
