#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include "Core/Entity.h"  // MaterialType

namespace WT {

using namespace DirectX;

// ============================================================
// Emission shape
// ============================================================
enum class EmitShape : uint32_t {
    Point   = 0,
    Sphere  = 1,
    Cone    = 2,
    Box     = 3,
};

// ============================================================
// Particle render mode
// ============================================================
enum class ParticleRenderMode : uint32_t {
    Billboard = 0,   // Camera-facing quad
    Mesh      = 1,   // Instanced mesh
    Trail     = 2,   // Ribbon trail strip
    Beam      = 3,   // Stretched billboard between two points
};

// ============================================================
// Emitter Definition — data-driven particle preset
// ============================================================
struct ParticleEmitterDef {
    // --- Emission ---
    float spawnRate       = 20.0f;  // Particles per second (continuous)
    int   burstCount      = 0;      // One-shot burst count (0 = continuous only)
    EmitShape shape       = EmitShape::Point;
    float shapeRadius     = 0.0f;
    float shapeConeAngle  = 30.0f;  // Degrees
    XMFLOAT3 shapeBoxHalf = { 0.5f, 0.5f, 0.5f };

    // --- Initial velocity ---
    XMFLOAT3 velocityMin  = { -1.0f, 1.0f, -1.0f };
    XMFLOAT3 velocityMax  = {  1.0f, 3.0f,  1.0f };
    float    speed        = 5.0f;

    // --- Lifetime ---
    float lifetimeMin     = 0.5f;
    float lifetimeMax     = 2.0f;

    // --- Size over life ---
    float sizeStartMin    = 0.1f;
    float sizeStartMax    = 0.2f;
    float sizeEndMin      = 0.0f;
    float sizeEndMax      = 0.05f;

    // --- Color over life ---
    XMFLOAT4 colorStart   = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT4 colorEnd     = { 1.0f, 1.0f, 1.0f, 0.0f };

    // --- Physics ---
    float gravity         = 9.8f;
    float drag            = 0.0f;

    // --- Rotation ---
    float rotationMin     = 0.0f;
    float rotationMax     = 6.283f;
    float rotSpeedMin     = 0.0f;
    float rotSpeedMax     = 3.14f;

    // --- Depth-buffer collision ---
    bool  depthCollision       = false;
    float bounciness           = 0.5f;
    float collisionLifetimeLoss = 0.2f;
    bool  spawnDecalOnCollision = false;

    // --- Rendering ---
    ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
    bool  additive                = false;      // Additive blending (sparks, fire)

    // --- LOD ---
    float lodFadeStart    = 50.0f;   // Start alpha-fading
    float lodFadeEnd      = 100.0f;  // Fully culled
};

// ============================================================
// GPU-side emitter constant buffer (matches CBParticleEmitter in HLSL)
// Must be 16-byte aligned.
// ============================================================
struct alignas(16) CBParticleEmitter {
    XMFLOAT3 EmitPosition;      float SpawnCount;
    XMFLOAT3 VelocityMin;       float LifetimeMin;
    XMFLOAT3 VelocityMax;       float LifetimeMax;
    XMFLOAT4 ColorStart;
    XMFLOAT4 ColorEnd;
    float SizeStartMin;  float SizeStartMax;  float SizeEndMin;  float SizeEndMax;
    float Gravity;        float Drag;          float RotMin;      float RotMax;
    float RotSpeedMin;    float RotSpeedMax;   float ShapeRadius; float Bounciness;
    uint32_t Shape;       uint32_t Flags;      float ConeAngle;   float Speed;
    XMFLOAT3 ShapeBoxHalf; float RandomSeed;
    XMFLOAT3 EmitDirection; float Pad0;
};

// ============================================================
// GPU-side simulation constant buffer (matches CBParticleSim in HLSL)
// ============================================================
struct alignas(16) CBParticleSim {
    float    DeltaTime;       float    MaxParticles;    float    Pad0;             float    Pad1;
};

// ============================================================
// Emitter Instance — a live emitter in the world
// ============================================================
struct EmitterInstance {
    int                id         = 0;
    ParticleEmitterDef def;
    XMFLOAT3           position   = { 0, 0, 0 };
    XMFLOAT3           direction  = { 0, 1, 0 }; // For cone / directional emission
    float              spawnAccum = 0.0f;         // Fractional spawn accumulator
    float              age        = 0.0f;
    float              duration   = -1.0f;        // <0 = infinite
    bool               alive      = true;
};

// ============================================================
// Built-in preset emitters — AAA quality GPU particle presets
// ============================================================
namespace ParticlePresets {

// ================================================================
//  FIRE — rising hot gas with bright core fading to deep red
// ================================================================
inline ParticleEmitterDef Fire() {
    ParticleEmitterDef d;
    d.spawnRate = 80; d.shape = EmitShape::Sphere; d.shapeRadius = 0.12f;
    d.velocityMin = { -0.4f, 1.2f, -0.4f }; d.velocityMax = { 0.4f, 3.5f, 0.4f };
    d.speed = 1.0f; d.lifetimeMin = 0.15f; d.lifetimeMax = 0.55f;
    d.sizeStartMin = 0.06f; d.sizeStartMax = 0.12f; d.sizeEndMin = 0.18f; d.sizeEndMax = 0.28f;
    d.colorStart = { 1.0f, 0.85f, 0.3f, 1.0f }; d.colorEnd = { 0.9f, 0.12f, 0.0f, 0.0f };
    d.gravity = -2.0f; d.drag = 0.8f; d.additive = true;
    d.rotSpeedMin = -4.0f; d.rotSpeedMax = 4.0f;
    return d;
}

// ================================================================
//  SMOKE — slow billowing clouds that expand and drift upward
// ================================================================
inline ParticleEmitterDef Smoke() {
    ParticleEmitterDef d;
    d.spawnRate = 20; d.shape = EmitShape::Sphere; d.shapeRadius = 0.15f;
    d.velocityMin = { -0.3f, 0.4f, -0.3f }; d.velocityMax = { 0.3f, 1.8f, 0.3f };
    d.speed = 1.0f; d.lifetimeMin = 2.0f; d.lifetimeMax = 4.0f;
    d.sizeStartMin = 0.08f; d.sizeStartMax = 0.15f; d.sizeEndMin = 0.6f; d.sizeEndMax = 1.2f;
    d.colorStart = { 0.35f, 0.33f, 0.30f, 0.45f }; d.colorEnd = { 0.25f, 0.25f, 0.25f, 0.0f };
    d.gravity = -0.6f; d.drag = 1.2f;
    d.rotSpeedMin = -1.5f; d.rotSpeedMax = 1.5f;
    return d;
}

// ================================================================
//  SPARKS — fast hot metal fragments spraying outward, bouncing
// ================================================================
inline ParticleEmitterDef Sparks() {
    ParticleEmitterDef d;
    d.burstCount = 24; d.spawnRate = 0;
    d.velocityMin = { -4.0f, 1.5f, -4.0f }; d.velocityMax = { 4.0f, 8.0f, 4.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.12f; d.lifetimeMax = 0.5f;
    d.sizeStartMin = 0.012f; d.sizeStartMax = 0.03f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.005f;
    d.colorStart = { 1.0f, 0.9f, 0.45f, 1.0f }; d.colorEnd = { 1.0f, 0.25f, 0.02f, 0.0f };
    d.gravity = 12.0f; d.additive = true;
    d.depthCollision = true; d.bounciness = 0.35f;
    return d;
}

// ================================================================
//  EXPLOSION — multi-phase: bright flash core → expanding smoke cloud
// ================================================================

// Phase 1: Brief flash core (additive, very short-lived)
inline ParticleEmitterDef ExplosionFlash() {
    ParticleEmitterDef d;
    d.burstCount = 8; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.08f;
    d.velocityMin = { -1.0f, -0.5f, -1.0f }; d.velocityMax = { 1.0f, 1.5f, 1.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.04f; d.lifetimeMax = 0.12f;
    d.sizeStartMin = 0.06f; d.sizeStartMax = 0.12f; d.sizeEndMin = 0.15f; d.sizeEndMax = 0.25f;
    d.colorStart = { 1.0f, 0.85f, 0.5f, 0.6f }; d.colorEnd = { 1.0f, 0.4f, 0.0f, 0.0f };
    d.gravity = -0.3f; d.drag = 3.0f; d.additive = true;
    d.rotSpeedMin = -5.0f; d.rotSpeedMax = 5.0f;
    return d;
}

// Phase 2: Expanding smoke ball (alpha-blended, longer-lived)
inline ParticleEmitterDef ExplosionSmoke() {
    ParticleEmitterDef d;
    d.burstCount = 20; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.2f;
    d.velocityMin = { -3.0f, 0.5f, -3.0f }; d.velocityMax = { 3.0f, 5.0f, 3.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.8f; d.lifetimeMax = 2.0f;
    d.sizeStartMin = 0.12f; d.sizeStartMax = 0.25f; d.sizeEndMin = 0.5f; d.sizeEndMax = 1.0f;
    d.colorStart = { 0.4f, 0.35f, 0.25f, 0.6f }; d.colorEnd = { 0.2f, 0.2f, 0.2f, 0.0f };
    d.gravity = -0.3f; d.drag = 1.5f;
    d.rotSpeedMin = -2.0f; d.rotSpeedMax = 2.0f;
    return d;
}

// Phase 3: Hot debris shrapnel (bouncing physical chunks)
inline ParticleEmitterDef ExplosionShrapnel() {
    ParticleEmitterDef d;
    d.burstCount = 40; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.15f;
    d.velocityMin = { -5.0f, 2.0f, -5.0f }; d.velocityMax = { 5.0f, 10.0f, 5.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.4f; d.lifetimeMax = 1.2f;
    d.sizeStartMin = 0.015f; d.sizeStartMax = 0.04f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.008f;
    d.colorStart = { 1.0f, 0.8f, 0.3f, 1.0f }; d.colorEnd = { 0.9f, 0.2f, 0.0f, 0.0f };
    d.gravity = 14.0f; d.additive = true;
    d.depthCollision = true; d.bounciness = 0.3f;
    return d;
}

// Backwards compat: "Explosion" spawns all three phases at once
inline ParticleEmitterDef Explosion() { return ExplosionFlash(); }

// ================================================================
//  DUST — surface dust kicked up on impacts / collapses
// ================================================================
inline ParticleEmitterDef Dust() {
    ParticleEmitterDef d;
    d.spawnRate = 10; d.shape = EmitShape::Sphere; d.shapeRadius = 0.25f;
    d.velocityMin = { -0.6f, 0.1f, -0.6f }; d.velocityMax = { 0.6f, 0.8f, 0.6f };
    d.speed = 1.0f; d.lifetimeMin = 1.2f; d.lifetimeMax = 3.0f;
    d.sizeStartMin = 0.04f; d.sizeStartMax = 0.08f; d.sizeEndMin = 0.18f; d.sizeEndMax = 0.35f;
    d.colorStart = { 0.6f, 0.55f, 0.42f, 0.35f }; d.colorEnd = { 0.5f, 0.5f, 0.45f, 0.0f };
    d.gravity = -0.2f; d.drag = 0.6f;
    d.rotSpeedMin = -1.0f; d.rotSpeedMax = 1.0f;
    return d;
}

// ================================================================
//  MUZZLE FLASH — 3-part: hot core + radial sparks + barrel smoke
// ================================================================

// Part 1: Small hot core (very short, additive)
inline ParticleEmitterDef MuzzleFlashCore() {
    ParticleEmitterDef d;
    d.burstCount = 2; d.spawnRate = 0;
    d.shape = EmitShape::Cone; d.shapeConeAngle = 6.0f;
    d.speed = 10.0f; d.lifetimeMin = 0.015f; d.lifetimeMax = 0.035f;
    d.sizeStartMin = 0.02f; d.sizeStartMax = 0.04f; d.sizeEndMin = 0.05f; d.sizeEndMax = 0.08f;
    d.colorStart = { 1.0f, 0.9f, 0.55f, 0.65f }; d.colorEnd = { 1.0f, 0.5f, 0.05f, 0.0f };
    d.gravity = 0.0f; d.drag = 4.0f; d.additive = true;
    return d;
}

// Part 2: Radial sparks spraying from barrel
inline ParticleEmitterDef MuzzleFlashSparks() {
    ParticleEmitterDef d;
    d.burstCount = 8; d.spawnRate = 0;
    d.shape = EmitShape::Cone; d.shapeConeAngle = 25.0f;
    d.speed = 15.0f; d.lifetimeMin = 0.03f; d.lifetimeMax = 0.12f;
    d.sizeStartMin = 0.008f; d.sizeStartMax = 0.018f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.003f;
    d.colorStart = { 1.0f, 0.9f, 0.5f, 1.0f }; d.colorEnd = { 1.0f, 0.35f, 0.0f, 0.0f };
    d.gravity = 4.0f; d.additive = true;
    return d;
}

// Part 3: Wispy barrel smoke (alpha blended, drifts up)
inline ParticleEmitterDef MuzzleSmoke() {
    ParticleEmitterDef d;
    d.burstCount = 3; d.spawnRate = 0;
    d.shape = EmitShape::Cone; d.shapeConeAngle = 12.0f;
    d.speed = 2.5f; d.lifetimeMin = 0.15f; d.lifetimeMax = 0.5f;
    d.sizeStartMin = 0.02f; d.sizeStartMax = 0.04f; d.sizeEndMin = 0.08f; d.sizeEndMax = 0.15f;
    d.colorStart = { 0.55f, 0.55f, 0.55f, 0.3f }; d.colorEnd = { 0.4f, 0.4f, 0.4f, 0.0f };
    d.gravity = -0.8f; d.drag = 2.0f;
    d.rotSpeedMin = -3.0f; d.rotSpeedMax = 3.0f;
    return d;
}

// Backwards compat
inline ParticleEmitterDef MuzzleFlash() { return MuzzleFlashCore(); }

// ================================================================
//  BULLET TRACER — fast bright streak along the flight path
// ================================================================
inline ParticleEmitterDef BulletTracer() {
    ParticleEmitterDef d;
    d.burstCount = 3; d.spawnRate = 0;
    d.shape = EmitShape::Cone; d.shapeConeAngle = 0.5f; // Nearly straight
    d.speed = 80.0f; d.lifetimeMin = 0.04f; d.lifetimeMax = 0.08f;
    d.sizeStartMin = 0.008f; d.sizeStartMax = 0.012f; d.sizeEndMin = 0.002f; d.sizeEndMax = 0.004f;
    d.colorStart = { 1.0f, 0.95f, 0.6f, 0.9f }; d.colorEnd = { 1.0f, 0.6f, 0.1f, 0.0f };
    d.gravity = 0.0f; d.drag = 0.0f; d.additive = true;
    return d;
}

// ================================================================
//  IMPACT SPARKS — bright ricochet sparks from bullet hits
// ================================================================
inline ParticleEmitterDef ImpactSparks() {
    ParticleEmitterDef d;
    d.burstCount = 12; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.03f;
    d.velocityMin = { -3.0f, 2.0f, -3.0f }; d.velocityMax = { 3.0f, 7.0f, 3.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.08f; d.lifetimeMax = 0.35f;
    d.sizeStartMin = 0.01f; d.sizeStartMax = 0.025f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.003f;
    d.colorStart = { 1.0f, 0.9f, 0.45f, 1.0f }; d.colorEnd = { 1.0f, 0.3f, 0.05f, 0.0f };
    d.gravity = 10.0f; d.additive = true;
    d.depthCollision = true; d.bounciness = 0.3f;
    return d;
}

// ================================================================
//  IMPACT DUST — surface dust puff at bullet hit point
// ================================================================
inline ParticleEmitterDef ImpactDust(const float color[4]) {
    ParticleEmitterDef d;
    d.burstCount = 6; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.06f;
    d.velocityMin = { -0.8f, 0.3f, -0.8f }; d.velocityMax = { 0.8f, 1.5f, 0.8f };
    d.speed = 1.0f; d.lifetimeMin = 0.3f; d.lifetimeMax = 0.8f;
    d.sizeStartMin = 0.02f; d.sizeStartMax = 0.05f; d.sizeEndMin = 0.1f; d.sizeEndMax = 0.2f;
    // Tint dust from entity color, desaturated
    d.colorStart = { color[0] * 0.6f + 0.3f, color[1] * 0.6f + 0.3f, color[2] * 0.6f + 0.25f, 0.5f };
    d.colorEnd   = { color[0] * 0.4f + 0.2f, color[1] * 0.4f + 0.2f, color[2] * 0.4f + 0.2f, 0.0f };
    d.gravity = -0.3f; d.drag = 1.5f;
    d.rotSpeedMin = -2.0f; d.rotSpeedMax = 2.0f;
    return d;
}

// ================================================================
//  IMPACT FLASH — small bright flash at the hit point
// ================================================================
inline ParticleEmitterDef ImpactFlash() {
    ParticleEmitterDef d;
    d.burstCount = 1; d.spawnRate = 0;
    d.shape = EmitShape::Point;
    d.velocityMin = { 0.0f, 0.0f, 0.0f }; d.velocityMax = { 0.0f, 0.0f, 0.0f };
    d.speed = 0.0f; d.lifetimeMin = 0.02f; d.lifetimeMax = 0.04f;
    d.sizeStartMin = 0.03f; d.sizeStartMax = 0.05f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.01f;
    d.colorStart = { 1.0f, 0.85f, 0.5f, 0.5f }; d.colorEnd = { 1.0f, 0.5f, 0.05f, 0.0f };
    d.gravity = 0.0f; d.additive = true;
    return d;
}

// ================================================================
//  DEBRIS — heavy bouncing chunks from destruction
// ================================================================
inline ParticleEmitterDef Debris(const float color[4]) {
    ParticleEmitterDef d;
    d.burstCount = 10; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.25f;
    d.velocityMin = { -3.0f, 2.0f, -3.0f }; d.velocityMax = { 3.0f, 7.0f, 3.0f };
    d.speed = 1.0f; d.lifetimeMin = 1.5f; d.lifetimeMax = 3.5f;
    d.sizeStartMin = 0.06f; d.sizeStartMax = 0.18f; d.sizeEndMin = 0.03f; d.sizeEndMax = 0.10f;
    d.colorStart = { color[0], color[1], color[2], 1.0f };
    d.colorEnd   = { color[0] * 0.6f, color[1] * 0.6f, color[2] * 0.6f, 0.0f };
    d.gravity = 14.0f;
    d.depthCollision = true; d.bounciness = 0.35f;
    d.rotSpeedMin = -6.0f; d.rotSpeedMax = 6.0f;
    return d;
}

// ================================================================
//  WALL CHUNKS — large visible fragments when breaking walls/voxels
// ================================================================
inline ParticleEmitterDef WallChunks(const float color[4]) {
    ParticleEmitterDef d;
    d.burstCount = 6; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.15f;
    d.velocityMin = { -2.5f, 1.0f, -2.5f }; d.velocityMax = { 2.5f, 5.0f, 2.5f };
    d.speed = 1.0f; d.lifetimeMin = 2.0f; d.lifetimeMax = 4.0f;
    d.sizeStartMin = 0.08f; d.sizeStartMax = 0.22f; d.sizeEndMin = 0.06f; d.sizeEndMax = 0.15f;
    d.colorStart = { color[0], color[1], color[2], 1.0f };
    d.colorEnd   = { color[0] * 0.5f, color[1] * 0.5f, color[2] * 0.5f, 0.0f };
    d.gravity = 10.0f;
    d.depthCollision = true; d.bounciness = 0.25f;
    d.rotSpeedMin = -8.0f; d.rotSpeedMax = 8.0f;
    return d;
}

// Small fast chips that spray outward from wall break
inline ParticleEmitterDef WallChips(const float color[4]) {
    ParticleEmitterDef d;
    d.burstCount = 12; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.08f;
    d.velocityMin = { -4.0f, 1.5f, -4.0f }; d.velocityMax = { 4.0f, 6.0f, 4.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.4f; d.lifetimeMax = 1.2f;
    d.sizeStartMin = 0.015f; d.sizeStartMax = 0.05f; d.sizeEndMin = 0.005f; d.sizeEndMax = 0.02f;
    d.colorStart = { color[0] * 0.9f + 0.1f, color[1] * 0.9f + 0.1f, color[2] * 0.9f + 0.1f, 1.0f };
    d.colorEnd   = { color[0] * 0.5f, color[1] * 0.5f, color[2] * 0.5f, 0.0f };
    d.gravity = 12.0f;
    d.depthCollision = true; d.bounciness = 0.4f;
    d.rotSpeedMin = -10.0f; d.rotSpeedMax = 10.0f;
    return d;
}

// Dust cloud puff when wall breaks
inline ParticleEmitterDef WallDust(const float color[4]) {
    ParticleEmitterDef d;
    d.burstCount = 5; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.12f;
    d.velocityMin = { -1.0f, 0.2f, -1.0f }; d.velocityMax = { 1.0f, 1.5f, 1.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.5f; d.lifetimeMax = 1.5f;
    d.sizeStartMin = 0.04f; d.sizeStartMax = 0.1f; d.sizeEndMin = 0.15f; d.sizeEndMax = 0.3f;
    d.colorStart = { color[0] * 0.6f + 0.3f, color[1] * 0.6f + 0.3f, color[2] * 0.6f + 0.25f, 0.4f };
    d.colorEnd   = { color[0] * 0.4f + 0.2f, color[1] * 0.4f + 0.2f, color[2] * 0.4f + 0.2f, 0.0f };
    d.gravity = -0.3f; d.drag = 1.0f;
    d.rotSpeedMin = -2.0f; d.rotSpeedMax = 2.0f;
    return d;
}

// ================================================================
//  DAMAGE SMOKE — dark smoke rising from wounded entities
// ================================================================
inline ParticleEmitterDef DamageSmoke() {
    ParticleEmitterDef d;
    d.burstCount = 4; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.2f;
    d.velocityMin = { -0.3f, 0.6f, -0.3f }; d.velocityMax = { 0.3f, 2.0f, 0.3f };
    d.speed = 1.0f; d.lifetimeMin = 1.2f; d.lifetimeMax = 3.0f;
    d.sizeStartMin = 0.06f; d.sizeStartMax = 0.12f; d.sizeEndMin = 0.35f; d.sizeEndMax = 0.65f;
    d.colorStart = { 0.22f, 0.20f, 0.18f, 0.5f }; d.colorEnd = { 0.18f, 0.18f, 0.18f, 0.0f };
    d.gravity = -0.5f; d.drag = 0.8f;
    d.rotSpeedMin = -1.5f; d.rotSpeedMax = 1.5f;
    return d;
}

// ================================================================
//  DAMAGE EMBERS — glowing hot particles rising from critical HP
// ================================================================
inline ParticleEmitterDef DamageEmbers() {
    ParticleEmitterDef d;
    d.burstCount = 5; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.2f;
    d.velocityMin = { -1.2f, 1.5f, -1.2f }; d.velocityMax = { 1.2f, 4.0f, 1.2f };
    d.speed = 1.0f; d.lifetimeMin = 0.2f; d.lifetimeMax = 0.7f;
    d.sizeStartMin = 0.015f; d.sizeStartMax = 0.04f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.008f;
    d.colorStart = { 1.0f, 0.7f, 0.15f, 1.0f }; d.colorEnd = { 0.85f, 0.12f, 0.0f, 0.0f };
    d.gravity = -1.5f; d.additive = true;
    return d;
}

// ================================================================
//  DRONE DOWNWASH — wispy air displacement below hovering drone
// ================================================================
inline ParticleEmitterDef DroneDownwash() {
    ParticleEmitterDef d;
    d.burstCount = 16; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.6f;
    d.velocityMin = { -1.2f, -2.5f, -1.2f }; d.velocityMax = { 1.2f, -0.5f, 1.2f };
    d.speed = 1.0f; d.lifetimeMin = 0.1f; d.lifetimeMax = 0.4f;
    d.sizeStartMin = 0.008f; d.sizeStartMax = 0.015f; d.sizeEndMin = 0.025f; d.sizeEndMax = 0.05f;
    d.colorStart = { 0.85f, 0.85f, 0.88f, 0.12f }; d.colorEnd = { 0.85f, 0.85f, 0.88f, 0.0f };
    d.gravity = -0.05f; d.drag = 0.3f;
    return d;
}

// ================================================================
//  DRONE GROUND DUST — kicked-up particles under hovering drone
// ================================================================
inline ParticleEmitterDef DroneGroundDust() {
    ParticleEmitterDef d;
    d.burstCount = 8; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 1.2f;
    d.velocityMin = { -2.0f, 0.2f, -2.0f }; d.velocityMax = { 2.0f, 1.5f, 2.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.5f; d.lifetimeMax = 1.2f;
    d.sizeStartMin = 0.015f; d.sizeStartMax = 0.04f; d.sizeEndMin = 0.08f; d.sizeEndMax = 0.18f;
    d.colorStart = { 0.55f, 0.45f, 0.3f, 0.22f }; d.colorEnd = { 0.5f, 0.48f, 0.42f, 0.0f };
    d.gravity = -0.2f; d.drag = 0.6f;
    d.rotSpeedMin = -1.0f; d.rotSpeedMax = 1.0f;
    return d;
}

// ================================================================
//  DRONE CRASH SMOKE — black trail while falling
// ================================================================
inline ParticleEmitterDef DroneCrashSmoke() {
    ParticleEmitterDef d;
    d.burstCount = 5; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.1f;
    d.velocityMin = { -0.6f, 0.2f, -0.6f }; d.velocityMax = { 0.6f, 1.8f, 0.6f };
    d.speed = 1.0f; d.lifetimeMin = 0.6f; d.lifetimeMax = 2.0f;
    d.sizeStartMin = 0.04f; d.sizeStartMax = 0.08f; d.sizeEndMin = 0.2f; d.sizeEndMax = 0.4f;
    d.colorStart = { 0.15f, 0.14f, 0.13f, 0.55f }; d.colorEnd = { 0.2f, 0.2f, 0.2f, 0.0f };
    d.gravity = -0.4f; d.drag = 0.6f;
    d.rotSpeedMin = -2.0f; d.rotSpeedMax = 2.0f;
    return d;
}

// ================================================================
//  DRONE CRASH EXPLOSION — 3-phase like main explosion system
// ================================================================
inline ParticleEmitterDef DroneCrashExplosion() {
    ParticleEmitterDef d;
    d.burstCount = 35; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.2f;
    d.velocityMin = { -4.0f, 1.0f, -4.0f }; d.velocityMax = { 4.0f, 8.0f, 4.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.1f; d.lifetimeMax = 0.4f;
    d.sizeStartMin = 0.1f; d.sizeStartMax = 0.2f; d.sizeEndMin = 0.35f; d.sizeEndMax = 0.6f;
    d.colorStart = { 1.0f, 0.75f, 0.2f, 1.0f }; d.colorEnd = { 0.5f, 0.2f, 0.05f, 0.0f };
    d.gravity = 3.0f; d.drag = 1.0f; d.additive = true;
    d.depthCollision = true; d.bounciness = 0.15f;
    d.rotSpeedMin = -4.0f; d.rotSpeedMax = 4.0f;
    return d;
}

// Drone crash metal debris
inline ParticleEmitterDef DroneCrashDebris() {
    ParticleEmitterDef d;
    d.burstCount = 16; d.spawnRate = 0;
    d.shape = EmitShape::Sphere; d.shapeRadius = 0.15f;
    d.velocityMin = { -5.0f, 2.0f, -5.0f }; d.velocityMax = { 5.0f, 9.0f, 5.0f };
    d.speed = 1.0f; d.lifetimeMin = 0.5f; d.lifetimeMax = 1.5f;
    d.sizeStartMin = 0.012f; d.sizeStartMax = 0.035f; d.sizeEndMin = 0.0f; d.sizeEndMax = 0.005f;
    d.colorStart = { 0.7f, 0.7f, 0.75f, 1.0f }; d.colorEnd = { 0.4f, 0.4f, 0.45f, 0.0f };
    d.gravity = 14.0f;
    d.depthCollision = true; d.bounciness = 0.4f;
    d.rotSpeedMin = -8.0f; d.rotSpeedMax = 8.0f;
    return d;
}

// ============================================================
// Material-aware helpers — multi-layer impact/explosion system
// ============================================================

// Impact FX by material (bullet hit)
inline void GetMaterialImpactDef(MaterialType mat, const float color[4],
                                  ParticleEmitterDef& outSparks, int& sparkCount,
                                  ParticleEmitterDef& outDust, int& dustCount) {
    outSparks = ImpactSparks();
    outDust   = ImpactDust(color);

    switch (mat) {
    case MaterialType::Wood:
        sparkCount = 3; dustCount = 8;
        outSparks.colorStart = { 1.0f, 0.75f, 0.25f, 1.0f };
        outSparks.colorEnd   = { 0.9f, 0.4f, 0.05f, 0.0f };
        outSparks.gravity = 8.0f;
        // Warmer, browner dust for wood
        outDust.colorStart = { color[0]*0.7f+0.2f, color[1]*0.5f+0.15f, color[2]*0.3f+0.1f, 0.5f };
        outDust.colorEnd   = { color[0]*0.5f+0.15f, color[1]*0.4f+0.1f, color[2]*0.2f+0.08f, 0.0f };
        break;
    case MaterialType::Metal:
        sparkCount = 16; dustCount = 2;
        outSparks.gravity = 14.0f; // Heavy sparks fly further
        outSparks.velocityMin = { -4.0f, 2.0f, -4.0f };
        outSparks.velocityMax = { 4.0f, 8.0f, 4.0f };
        break;
    case MaterialType::Glass:
        sparkCount = 14; dustCount = 0;
        outSparks.colorStart = { 0.9f, 0.95f, 1.0f, 0.85f };
        outSparks.colorEnd   = { 0.85f, 0.92f, 1.0f, 0.0f };
        outSparks.sizeStartMin = 0.006f; outSparks.sizeStartMax = 0.012f;
        outSparks.lifetimeMin = 0.1f; outSparks.lifetimeMax = 0.4f;
        outSparks.gravity = 8.0f;
        break;
    default: // Concrete
        sparkCount = 8; dustCount = 6;
        break;
    }
}

// Explosion FX by material (entity destroyed) — multi-phase
inline void GetMaterialExplosionDef(MaterialType mat, const float color[4],
                                     ParticleEmitterDef& outDebris, int& debrisCount,
                                     ParticleEmitterDef& outSparks, int& sparkCount,
                                     ParticleEmitterDef& outSmoke, int& smokeCount) {
    outDebris  = Debris(color);
    outSparks  = ExplosionShrapnel();
    outSmoke   = ExplosionSmoke();

    switch (mat) {
    case MaterialType::Wood: {
        float wc[4] = { color[0]*0.8f, color[1]*0.6f, color[2]*0.4f, 1.0f };
        outDebris = Debris(wc);
        outDebris.gravity = 12.0f;
        debrisCount = 12; sparkCount = 6; smokeCount = 8;
        // Wood explosions produce more embers (warm sparks)
        outSparks.colorStart = { 1.0f, 0.7f, 0.15f, 1.0f };
        outSparks.colorEnd   = { 0.85f, 0.2f, 0.0f, 0.0f };
        outSparks.gravity = 10.0f;
        break;
    }
    case MaterialType::Metal:
        debrisCount = 10; sparkCount = 24; smokeCount = 5;
        outSparks.gravity = 14.0f;
        outSparks.velocityMin = { -6.0f, 2.0f, -6.0f };
        outSparks.velocityMax = { 6.0f, 12.0f, 6.0f };
        outSmoke.colorStart = { 0.3f, 0.3f, 0.32f, 0.5f };
        break;
    case MaterialType::Glass:
        debrisCount = 4; sparkCount = 30; smokeCount = 0;
        outSparks.colorStart = { 0.9f, 0.95f, 1.0f, 0.9f };
        outSparks.colorEnd   = { 0.85f, 0.92f, 1.0f, 0.0f };
        outSparks.sizeStartMin = 0.008f; outSparks.sizeStartMax = 0.02f;
        outSparks.lifetimeMin = 0.15f; outSparks.lifetimeMax = 0.5f;
        break;
    default: // Concrete
        debrisCount = 10; sparkCount = 12; smokeCount = 8;
        break;
    }
}

} // namespace ParticlePresets
} // namespace WT
