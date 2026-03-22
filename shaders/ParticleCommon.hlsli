// ============================================================
// ParticleCommon.hlsli — Shared GPU particle structures
// Included by all particle compute and render shaders
// ============================================================

#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

// ============================================================
// GPU Particle — 96 bytes, tightly packed
// ============================================================
struct GPUParticle {
    float3 position;     // 12
    float  age;          //  4
    float3 velocity;     // 12
    float  lifetime;     //  4
    float4 colorStart;   // 16
    float4 colorEnd;     // 16
    float  sizeStart;    //  4
    float  sizeEnd;      //  4
    float  rotation;     //  4
    float  rotationSpeed;//  4
    uint   alive;        //  4  (bit0=alive, bit1=depthCollision)
    float  gravity;      //  4  (per-particle gravity from emitter)
    float  bounciness;   //  4
    float  drag;         //  4
};                       // = 96 bytes

// ============================================================
// Emitter CB (b5) — set per-emitter before spawn dispatch
// ============================================================
cbuffer CBParticleEmitter : register(b5) {
    float3   gEmitPosition;     float gSpawnCount;
    float3   gEmitVelocityMin;  float gEmitLifetimeMin;
    float3   gEmitVelocityMax;  float gEmitLifetimeMax;
    float4   gEmitColorStart;
    float4   gEmitColorEnd;
    float    gEmitSizeStartMin; float gEmitSizeStartMax;
    float    gEmitSizeEndMin;   float gEmitSizeEndMax;
    float    gEmitGravity;      float gEmitDrag;
    float    gEmitRotMin;       float gEmitRotMax;
    float    gEmitRotSpeedMin;  float gEmitRotSpeedMax;
    float    gEmitShapeRadius;  float gEmitBounciness;
    uint     gEmitShape;        uint  gEmitFlags;       // bit0=depthCollision, bit1=additive
    float    gEmitConeAngle;    float gEmitSpeed;
    float3   gEmitShapeBoxHalf; float gEmitRandomSeed;
    float3   gEmitDirection;    float gEmitPad0;        // Cone/directional aim axis
};

// ============================================================
// Simulation CB (b6) — set once per frame for simulate
// ============================================================
cbuffer CBParticleSim : register(b6) {
    float gDeltaTime;
    float gPMaxParticles;
    float gPad0;
    float gPad1;
};

// ============================================================
// Hash-based RNG
// ============================================================
uint WangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float RandomFloat(uint seed) {
    return float(WangHash(seed)) / 4294967295.0;
}

float RandomRange(float lo, float hi, uint seed) {
    return lo + RandomFloat(seed) * (hi - lo);
}

float3 RandomDirection(uint seed) {
    float theta = RandomRange(0.0, 6.2831853, seed);
    float phi   = acos(RandomRange(-1.0, 1.0, seed + 7919u));
    float sp    = sin(phi);
    return float3(sp * cos(theta), cos(phi), sp * sin(theta));
}

float3 RandomInSphere(float radius, uint seed) {
    float3 dir = RandomDirection(seed);
    float r = pow(RandomFloat(seed + 104729u), 1.0 / 3.0) * radius;
    return dir * r;
}

float3 RandomInBox(float3 halfExt, uint seed) {
    return float3(
        RandomRange(-halfExt.x, halfExt.x, seed),
        RandomRange(-halfExt.y, halfExt.y, seed + 3571u),
        RandomRange(-halfExt.z, halfExt.z, seed + 7127u)
    );
}

float3 RandomInCone(float3 dir, float halfAngle, uint seed) {
    float cosA = cos(halfAngle);
    float z = RandomRange(cosA, 1.0, seed);
    float phi = RandomRange(0.0, 6.2831853, seed + 5381u);
    float sinT = sqrt(1.0 - z * z);
    float3 local = float3(sinT * cos(phi), sinT * sin(phi), z);

    // Build basis from dir
    float3 up = abs(dir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, dir));
    up = cross(dir, right);

    return right * local.x + up * local.y + dir * local.z;
}

#endif // PARTICLE_COMMON_HLSLI
