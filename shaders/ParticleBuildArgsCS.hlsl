// ============================================================
// ParticleBuildArgsCS.hlsl — Write DrawInstancedIndirect args
// Dispatch: (1, 1, 1)
// ============================================================

#include "ParticleCommon.hlsli"

RWByteAddressBuffer Counters : register(u2); // [0]=deadCount, [4]=aliveCount
RWByteAddressBuffer DrawArgs : register(u4); // DrawInstancedIndirect args (16 bytes)

[numthreads(1, 1, 1)]
void CSMain() {
    uint aliveCount = Counters.Load(4);

    // DrawInstancedIndirect args:
    //   [0] VertexCountPerInstance = 6 (two triangles per billboard quad)
    //   [4] InstanceCount          = aliveCount
    //   [8] StartVertexLocation    = 0
    //  [12] StartInstanceLocation  = 0
    DrawArgs.Store(0,  6);
    DrawArgs.Store(4,  aliveCount);
    DrawArgs.Store(8,  0);
    DrawArgs.Store(12, 0);
}
