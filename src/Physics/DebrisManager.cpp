#include "DebrisManager.h"
#include "../Core/Entity.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace WT {

using namespace DirectX;

// ============================================================
// Init / Shutdown
// ============================================================

void DebrisManager::Init() {
    m_fragments.clear();
    m_fragments.reserve(maxDebris);
}

void DebrisManager::Shutdown() {
    m_fragments.clear();
}

void DebrisManager::Clear() {
    m_fragments.clear();
}

// ============================================================
// Spawning
// ============================================================

static float RandF() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }
static float RandRange(float lo, float hi) { return lo + RandF() * (hi - lo); }

void DebrisManager::SpawnFromDestruction(const XMFLOAT3& center,
                                          const XMFLOAT3& entityScale,
                                          const float entityColor[4],
                                          int count, float debrisScale,
                                          MaterialType mat) {
    if (count <= 0) return;

    // Ensure we have room
    int available = maxDebris - static_cast<int>(m_fragments.size());
    if (available < count) {
        EvictOldest(count - available);
    }

    float avgScale = (entityScale.x + entityScale.y + entityScale.z) / 3.0f;
    float fragSize = avgScale * debrisScale;

    for (int i = 0; i < count && static_cast<int>(m_fragments.size()) < maxDebris; i++) {
        DebrisFragment frag;

        // Position: randomly within entity bounds
        frag.position = {
            center.x + RandRange(-entityScale.x * 0.3f, entityScale.x * 0.3f),
            center.y + RandRange(-entityScale.y * 0.2f, entityScale.y * 0.4f),
            center.z + RandRange(-entityScale.z * 0.3f, entityScale.z * 0.3f)
        };

        // Velocity: outward explosion
        float dx = frag.position.x - center.x;
        float dy = frag.position.y - center.y;
        float dz = frag.position.z - center.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        float speed = RandRange(3.0f, 8.0f);
        if (dist > 0.01f) {
            float inv = speed / dist;
            frag.velocity = { dx * inv, dy * inv + RandRange(2.0f, 5.0f), dz * inv };
        } else {
            frag.velocity = { RandRange(-3.0f, 3.0f), RandRange(3.0f, 7.0f), RandRange(-3.0f, 3.0f) };
        }

        // Scale variation
        float sVar = RandRange(0.6f, 1.4f);
        frag.scale = { fragSize * sVar, fragSize * sVar, fragSize * sVar };

        // Slight color variation
        frag.color[0] = entityColor[0] * RandRange(0.7f, 1.0f);
        frag.color[1] = entityColor[1] * RandRange(0.7f, 1.0f);
        frag.color[2] = entityColor[2] * RandRange(0.7f, 1.0f);
        frag.color[3] = entityColor[3];

        // Angular velocity — random tumble
        frag.angularVel = {
            RandRange(-300.0f, 300.0f),
            RandRange(-300.0f, 300.0f),
            RandRange(-300.0f, 300.0f)
        };

        // Material-specific tuning
        frag.materialType = static_cast<uint8_t>(mat);
        switch (mat) {
            case MaterialType::Glass:
                frag.maxLifetime = RandRange(1.5f, 2.5f);
                frag.fadeStartTime = frag.maxLifetime * 0.6f;
                frag.bounciness = 0.05f;
                frag.scale.x *= 0.5f; frag.scale.y *= 0.15f; frag.scale.z *= 0.5f; // flat shards
                break;
            case MaterialType::Wood:
                frag.maxLifetime = RandRange(3.0f, 4.5f);
                frag.fadeStartTime = frag.maxLifetime * 0.7f;
                frag.bounciness = 0.1f;
                break;
            case MaterialType::Metal:
                frag.maxLifetime = RandRange(4.0f, 6.0f);
                frag.fadeStartTime = frag.maxLifetime * 0.75f;
                frag.bounciness = 0.3f;
                frag.gravity = 20.0f; // heavier
                break;
            default: // Concrete
                frag.maxLifetime = RandRange(2.5f, 4.0f);
                frag.fadeStartTime = frag.maxLifetime * 0.65f;
                frag.bounciness = 0.08f;
                break;
        }

        frag.lifetime = 0.0f;
        frag.alpha = 1.0f;
        frag.alive = true;
        frag.sleeping = false;
        frag.sleepFrameCount = 0;

        m_fragments.push_back(std::move(frag));
    }
}

void DebrisManager::SpawnSingle(const XMFLOAT3& pos,
                                 const XMFLOAT3& vel,
                                 const XMFLOAT3& scale,
                                 const float color[4],
                                 MaterialType mat) {
    if (static_cast<int>(m_fragments.size()) >= maxDebris) {
        EvictOldest(1);
    }

    DebrisFragment frag;
    frag.position = pos;
    frag.velocity = vel;
    frag.scale = scale;
    frag.color[0] = color[0]; frag.color[1] = color[1];
    frag.color[2] = color[2]; frag.color[3] = color[3];
    frag.materialType = static_cast<uint8_t>(mat);
    frag.angularVel = {
        RandRange(-200.0f, 200.0f),
        RandRange(-200.0f, 200.0f),
        RandRange(-200.0f, 200.0f)
    };
    frag.maxLifetime = defaultLifetime;
    frag.fadeStartTime = defaultFadeStart;
    frag.alive = true;

    m_fragments.push_back(std::move(frag));
}

// ============================================================
// Update — physics step for all living debris
// ============================================================

void DebrisManager::Update(float dt, float groundY) {
    if (dt <= 0.0f) return;

    for (auto& frag : m_fragments) {
        if (!frag.alive) continue;

        // Lifetime
        frag.lifetime += dt;
        if (frag.lifetime >= frag.maxLifetime) {
            frag.alive = false;
            continue;
        }

        // Fade
        if (frag.lifetime >= frag.fadeStartTime) {
            float fadeLen = frag.maxLifetime - frag.fadeStartTime;
            frag.alpha = (fadeLen > 0.0f) ? 1.0f - (frag.lifetime - frag.fadeStartTime) / fadeLen : 0.0f;
            if (frag.alpha < 0.0f) frag.alpha = 0.0f;
        }

        if (frag.sleeping) continue;

        // Gravity
        frag.velocity.y -= frag.gravity * dt;

        // Velocity clamp
        float speedSq = frag.velocity.x * frag.velocity.x +
                         frag.velocity.y * frag.velocity.y +
                         frag.velocity.z * frag.velocity.z;
        if (speedSq > 2500.0f) { // 50^2
            float inv = 50.0f / sqrtf(speedSq);
            frag.velocity.x *= inv;
            frag.velocity.y *= inv;
            frag.velocity.z *= inv;
        }

        // Integrate position
        frag.position.x += frag.velocity.x * dt;
        frag.position.y += frag.velocity.y * dt;
        frag.position.z += frag.velocity.z * dt;

        // Integrate rotation
        frag.rotation.x += frag.angularVel.x * dt;
        frag.rotation.y += frag.angularVel.y * dt;
        frag.rotation.z += frag.angularVel.z * dt;

        // Ground collision
        float halfH = frag.scale.y * 0.5f;
        frag.onGround = false;
        if (frag.position.y - halfH < groundY) {
            frag.position.y = groundY + halfH;

            if (frag.velocity.y < -0.5f) {
                frag.velocity.y = -frag.velocity.y * frag.bounciness;
            } else {
                frag.velocity.y = 0.0f;
            }
            frag.onGround = true;

            // Ground friction
            float fricFactor = 1.0f - frag.friction * dt * 10.0f;
            if (fricFactor < 0.0f) fricFactor = 0.0f;
            frag.velocity.x *= fricFactor;
            frag.velocity.z *= fricFactor;

            // Angular damping on ground
            frag.angularVel.x *= 0.9f;
            frag.angularVel.y *= 0.95f;
            frag.angularVel.z *= 0.9f;
        }

        // Air angular damping
        frag.angularVel.x *= (1.0f - 1.5f * dt);
        frag.angularVel.y *= (1.0f - 1.5f * dt);
        frag.angularVel.z *= (1.0f - 1.5f * dt);

        // Sleep policy
        speedSq = frag.velocity.x * frag.velocity.x +
                   frag.velocity.y * frag.velocity.y +
                   frag.velocity.z * frag.velocity.z;
        float angSpeedSq = frag.angularVel.x * frag.angularVel.x +
                            frag.angularVel.y * frag.angularVel.y +
                            frag.angularVel.z * frag.angularVel.z;

        if (frag.onGround && speedSq < frag.sleepThresholdSq && angSpeedSq < 100.0f) {
            frag.sleepFrameCount++;
            if (frag.sleepFrameCount >= frag.sleepFrameLimit) {
                frag.sleeping = true;
                frag.velocity = { 0, 0, 0 };
                frag.angularVel = { 0, 0, 0 };
            }
        } else {
            frag.sleepFrameCount = 0;
        }
    }
}

// ============================================================
// Cleanup / Eviction
// ============================================================

void DebrisManager::Cleanup() {
    m_fragments.erase(
        std::remove_if(m_fragments.begin(), m_fragments.end(),
                        [](const DebrisFragment& f) { return !f.alive; }),
        m_fragments.end()
    );
}

void DebrisManager::EvictOldest(int needed) {
    if (needed <= 0) return;

    // Sort by lifetime descending, kill the oldest
    std::vector<int> indices(m_fragments.size());
    for (int i = 0; i < static_cast<int>(indices.size()); i++) indices[i] = i;

    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_fragments[a].lifetime > m_fragments[b].lifetime;
    });

    int killed = 0;
    for (int i = 0; i < static_cast<int>(indices.size()) && killed < needed; i++) {
        if (m_fragments[indices[i]].alive) {
            m_fragments[indices[i]].alive = false;
            killed++;
        }
    }

    Cleanup();
}

int DebrisManager::GetActiveCount() const {
    int count = 0;
    for (const auto& f : m_fragments) {
        if (f.alive) count++;
    }
    return count;
}

// ============================================================
// Render batching
// ============================================================

std::vector<DebrisManager::RenderBatch> DebrisManager::BuildRenderBatches() const {
    std::vector<int> indices;
    indices.reserve(m_fragments.size());
    for (int i = 0; i < static_cast<int>(m_fragments.size()); i++) {
        if (m_fragments[i].alive) indices.push_back(i);
    }

    if (indices.empty()) return {};

    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_fragments[a].materialType < m_fragments[b].materialType;
    });

    std::vector<RenderBatch> batches;
    RenderBatch current;
    current.materialType = m_fragments[indices[0]].materialType;
    current.startIndex = 0;
    current.count = 0;

    for (int i = 0; i < static_cast<int>(indices.size()); i++) {
        uint8_t mat = m_fragments[indices[i]].materialType;
        if (mat != current.materialType) {
            batches.push_back(current);
            current.materialType = mat;
            current.startIndex = i;
            current.count = 0;
        }
        current.count++;
    }
    batches.push_back(current);

    return batches;
}

// ============================================================
// Wake nearby
// ============================================================

void DebrisManager::WakeNear(const XMFLOAT3& point, float radius, float impulse) {
    float r2 = radius * radius;
    for (auto& frag : m_fragments) {
        if (!frag.alive || !frag.sleeping) continue;

        float dx = frag.position.x - point.x;
        float dy = frag.position.y - point.y;
        float dz = frag.position.z - point.z;
        float d2 = dx * dx + dy * dy + dz * dz;

        if (d2 < r2) {
            frag.sleeping = false;
            frag.sleepFrameCount = 0;

            float dist = sqrtf(d2);
            float falloff = 1.0f - dist / radius;
            frag.velocity.y += impulse * falloff;
            if (dist > 0.01f) {
                float inv = falloff * impulse * 0.5f / dist;
                frag.velocity.x += dx * inv;
                frag.velocity.z += dz * inv;
            }
        }
    }
}

} // namespace WT
