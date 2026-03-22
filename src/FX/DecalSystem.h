#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdint>

namespace WT {

using namespace DirectX;

// ============================================================
// Decal — a persistent surface mark spawned by particle collision
// ============================================================
struct Decal {
    XMFLOAT3 position   = { 0, 0, 0 };
    XMFLOAT3 normal     = { 0, 1, 0 };
    XMFLOAT4 color      = { 0.1f, 0.1f, 0.1f, 0.8f };
    float    size        = 0.2f;
    float    lifetime    = 0.0f;
    float    maxLifetime = 10.0f;
    bool     alive       = true;
};

// ============================================================
// DecalSystem — lightweight manager for collision decals
// Renders as screen-space projected quads (deferred) or
// simple billboards facing the surface normal.
// ============================================================
class DecalSystem {
public:
    void Init(int maxDecals = 256) {
        m_decals.reserve(maxDecals);
        m_maxDecals = maxDecals;
    }

    void SpawnDecal(const XMFLOAT3& pos, const XMFLOAT3& normal,
                    const XMFLOAT4& color, float size, float lifetime = 8.0f) {
        // If at capacity, recycle oldest
        if (static_cast<int>(m_decals.size()) >= m_maxDecals) {
            // Find oldest or first dead
            for (auto& d : m_decals) {
                if (!d.alive) {
                    d = { pos, normal, color, size, 0.0f, lifetime, true };
                    return;
                }
            }
            // All alive — overwrite oldest
            m_decals[m_oldestIdx % m_decals.size()] = { pos, normal, color, size, 0.0f, lifetime, true };
            m_oldestIdx++;
            return;
        }
        m_decals.push_back({ pos, normal, color, size, 0.0f, lifetime, true });
    }

    void Update(float dt) {
        for (auto& d : m_decals) {
            if (!d.alive) continue;
            d.lifetime += dt;
            if (d.lifetime >= d.maxLifetime) {
                d.alive = false;
            }
        }
        // Compact dead decals periodically
        m_compactTimer += dt;
        if (m_compactTimer > 5.0f) {
            m_compactTimer = 0;
            m_decals.erase(
                std::remove_if(m_decals.begin(), m_decals.end(),
                    [](const Decal& d) { return !d.alive; }),
                m_decals.end());
        }
    }

    const std::vector<Decal>& GetDecals() const { return m_decals; }
    int GetActiveCount() const {
        int count = 0;
        for (auto& d : m_decals) if (d.alive) count++;
        return count;
    }
    void Clear() { m_decals.clear(); }

private:
    std::vector<Decal> m_decals;
    int m_maxDecals = 256;
    size_t m_oldestIdx = 0;
    float m_compactTimer = 0;
};

} // namespace WT
