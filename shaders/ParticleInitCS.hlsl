// ============================================================
// ParticleInitCS.hlsl — Initialize dead list with all indices
// Dispatch: ceil(maxParticles / 256)
// ============================================================

#include "ParticleCommon.hlsli"

RWStructuredBuffer<GPUParticle> ParticlePool : register(u0);
RWStructuredBuffer<uint>        DeadList     : register(u1);
RWByteAddressBuffer             Counters     : register(u2);
// Counters layout (bytes): [0]=deadCount, [4]=aliveCount

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    uint idx = dtid.x;
    uint maxP = (uint)gPMaxParticles;
    if (idx >= maxP) return;

    // Clear particle
    GPUParticle p = (GPUParticle)0;
    p.alive = 0;
    ParticlePool[idx] = p;

    // Fill dead list (reversed so index 0 is popped last)
    DeadList[idx] = maxP - 1u - idx;

    // Thread 0 sets counters
    if (idx == 0) {
        Counters.Store(0, maxP); // deadCount = maxParticles
        Counters.Store(4, 0);   // aliveCount = 0
    }
}
