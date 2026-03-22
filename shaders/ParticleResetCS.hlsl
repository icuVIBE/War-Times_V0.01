// ============================================================
// ParticleResetCS.hlsl — Reset per-frame alive counter
// Dispatch: (1, 1, 1)
// ============================================================

#include "ParticleCommon.hlsli"

RWByteAddressBuffer Counters : register(u2);

[numthreads(1, 1, 1)]
void CSMain() {
    Counters.Store(4, 0); // reset aliveCount
}
