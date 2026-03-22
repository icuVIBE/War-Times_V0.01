#pragma once

#include <vector>
#include <DirectXMath.h>
#include <cstdint>

namespace WT {

class Scene;
class ParticleSystem;
enum class MaterialType : uint8_t;

// ============================================================
// Debris Fragment — a small physics-driven piece spawned when
// an entity is fully destroyed (health ≤ 0) or from fracture.
// ============================================================

struct DebrisFragment {
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };     // Euler degrees
    DirectX::XMFLOAT3 angularVel = { 0, 0, 0 };   // Degrees/sec
    DirectX::XMFLOAT3 scale = { 0.3f, 0.3f, 0.3f };
    float color[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

    float lifetime = 0.0f;
    float maxLifetime = 3.5f;
    float fadeStartTime = 2.5f;
    float alpha = 1.0f;

    float bounciness = 0.15f;
    float friction = 0.4f;
    float gravity = 18.0f;

    // Sleep policy
    float sleepThresholdSq = 0.02f;
    int   sleepFrameCount = 0;
    int   sleepFrameLimit = 4;
    bool  sleeping = false;
    bool  onGround = false;

    bool  alive = true;

    uint8_t materialType = 0;   // MaterialType cast
    int16_t batchGroup = -1;    // Assigned batch group for rendering
};

// ============================================================
// Debris Manager — lifecycle, physics, sleep, batching
// ============================================================

class DebrisManager {
public:
    void Init();
    void Shutdown();

    // Spawn debris from a destroyed entity.
    // center: world position of the destroyed entity
    // entityScale: entity's scale
    // entityColor: entity's color
    // count: number of fragments to spawn
    // debrisScale: scale factor for debris relative to entity
    // mat: material type of the source entity
    void SpawnFromDestruction(const DirectX::XMFLOAT3& center,
                              const DirectX::XMFLOAT3& entityScale,
                              const float entityColor[4],
                              int count, float debrisScale,
                              MaterialType mat);

    // Spawn a single debris piece at a specific position with velocity.
    void SpawnSingle(const DirectX::XMFLOAT3& pos,
                     const DirectX::XMFLOAT3& vel,
                     const DirectX::XMFLOAT3& scale,
                     const float color[4],
                     MaterialType mat);

    // Step all debris physics (gravity, collision, sleep, fade).
    void Update(float dt, float groundY);

    // Remove dead fragments.
    void Cleanup();

    // Clear all debris (level change).
    void Clear();

    // Access for rendering.
    const std::vector<DebrisFragment>& GetFragments() const { return m_fragments; }
    int GetActiveCount() const;

    // Render batching — groups fragments by material for draw call reduction.
    struct RenderBatch {
        uint8_t materialType;
        int     startIndex;   // Into sorted index list
        int     count;
    };
    std::vector<RenderBatch> BuildRenderBatches() const;

    // Wake sleeping debris near a point (explosions, impacts).
    void WakeNear(const DirectX::XMFLOAT3& point, float radius, float impulse = 3.0f);

    // Tuning
    int   maxDebris = 512;
    float defaultLifetime = 3.5f;
    float defaultFadeStart = 2.5f;

private:
    // Evict oldest fragments when over capacity.
    void EvictOldest(int needed);

    std::vector<DebrisFragment> m_fragments;
};

} // namespace WT
