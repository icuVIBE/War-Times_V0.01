#include "FractureSystem.h"
#include "../Core/Entity.h"
#include "../FX/GPUParticleSystem.h"
#include "../FX/ParticleEmitter.h"
#include <algorithm>
#include <cmath>

namespace WT {

using namespace DirectX;

// ============================================================
// ConnectionGraph
// ============================================================

void ConnectionGraph::GetNeighbours(int idx, int res, int neighbours[6], int& count) const {
    count = 0;
    int total = res * res * res;
    int cx = idx % res;
    int cy = (idx / res) % res;
    int cz = idx / (res * res);

    // +X
    if (cx + 1 < res) neighbours[count++] = (cx + 1) + cy * res + cz * res * res;
    // -X
    if (cx - 1 >= 0)  neighbours[count++] = (cx - 1) + cy * res + cz * res * res;
    // +Y
    if (cy + 1 < res) neighbours[count++] = cx + (cy + 1) * res + cz * res * res;
    // -Y
    if (cy - 1 >= 0)  neighbours[count++] = cx + (cy - 1) * res + cz * res * res;
    // +Z
    if (cz + 1 < res) neighbours[count++] = cx + cy * res + (cz + 1) * res * res;
    // -Z
    if (cz - 1 >= 0)  neighbours[count++] = cx + cy * res + (cz - 1) * res * res;
}

void ConnectionGraph::FloodFill(int seed, int res, const Entity& entity,
                                 std::vector<bool>& visited, VoxelCluster& out) const {
    std::vector<int> stack;
    stack.push_back(seed);
    visited[seed] = true;

    while (!stack.empty()) {
        int cur = stack.back();
        stack.pop_back();
        out.cellIndices.push_back(cur);

        int cy = (cur / res) % res;
        if (cy == 0) out.grounded = true;

        int neighbours[6];
        int nCount = 0;
        GetNeighbours(cur, res, neighbours, nCount);

        for (int n = 0; n < nCount; n++) {
            int ni = neighbours[n];
            if (!visited[ni] && entity.IsVoxelCellActive(ni)) {
                visited[ni] = true;
                stack.push_back(ni);
            }
        }
    }
}

std::vector<VoxelCluster> ConnectionGraph::FindClusters(const Entity& entity) const {
    std::vector<VoxelCluster> clusters;
    int res = entity.voxelRes;
    int total = res * res * res;

    std::vector<bool> visited(total, false);

    for (int i = 0; i < total; i++) {
        if (!entity.IsVoxelCellActive(i) || visited[i]) continue;

        VoxelCluster cluster;
        cluster.grounded = false;
        cluster.centerOfMass = { 0, 0, 0 };
        cluster.totalVolume = 0.0f;

        FloodFill(i, res, entity, visited, cluster);

        // Compute center of mass in world space
        float cellVol = (entity.scale[0] / res) * (entity.scale[1] / res) * (entity.scale[2] / res);
        XMFLOAT3 com = { 0, 0, 0 };
        for (int ci : cluster.cellIndices) {
            int cx = ci % res;
            int cy = (ci / res) % res;
            int cz = ci / (res * res);

            float offX = ((cx + 0.5f) / res - 0.5f) * entity.scale[0];
            float offY = ((cy + 0.5f) / res - 0.5f) * entity.scale[1];
            float offZ = ((cz + 0.5f) / res - 0.5f) * entity.scale[2];

            com.x += entity.position[0] + offX;
            com.y += entity.position[1] + offY;
            com.z += entity.position[2] + offZ;
        }
        float n = static_cast<float>(cluster.cellIndices.size());
        if (n > 0) {
            com.x /= n;
            com.y /= n;
            com.z /= n;
        }
        cluster.centerOfMass = com;
        cluster.totalVolume = cellVol * n;

        clusters.push_back(std::move(cluster));
    }

    // Sort by size descending — largest cluster first (likely the main body)
    std::sort(clusters.begin(), clusters.end(),
              [](const VoxelCluster& a, const VoxelCluster& b) {
                  return a.cellIndices.size() > b.cellIndices.size();
              });

    return clusters;
}

// ============================================================
// FractureSystem
// ============================================================

void FractureSystem::Init() {
    m_fragments.clear();
    m_fragments.reserve(maxFragments);
}

void FractureSystem::Shutdown() {
    m_fragments.clear();
}

void FractureSystem::Clear() {
    m_fragments.clear();
}

int FractureSystem::EvaluateFracture(int entityIndex, Scene& scene, GPUParticleSystem& particles) {
    if (entityIndex < 0 || entityIndex >= scene.GetEntityCount()) return 0;
    Entity& entity = scene.GetEntity(entityIndex);
    if (!entity.voxelDestruction) return 0;

    auto clusters = m_graph.FindClusters(entity);
    if (clusters.size() <= 1) return 0; // No disconnection

    int spawned = 0;

    // The largest grounded cluster stays as the entity.
    // All other ungrounded clusters become fragments.
    bool foundMainCluster = false;
    for (auto& cluster : clusters) {
        if (!foundMainCluster && cluster.grounded) {
            // This is the main body — keep it on the entity.
            foundMainCluster = true;
            continue;
        }

        // Convert to fragment
        if (static_cast<int>(m_fragments.size()) >= maxFragments) break;

        // Remove these cells from the entity's voxel mask
        for (int ci : cluster.cellIndices) {
            entity.ClearVoxelCell(ci);
        }

        SpawnFragment(cluster, entity, entityIndex, particles);
        spawned++;
    }

    // If no grounded cluster remains, the whole entity is floating.
    // Keep the largest cluster as fragments have already been spawned.

    return spawned;
}

void FractureSystem::SpawnFragment(const VoxelCluster& cluster, const Entity& sourceEntity,
                                    int entityIndex, GPUParticleSystem& particles) {
    FractureFragment frag;
    frag.sourceEntityIndex = entityIndex;
    frag.cellIndices = cluster.cellIndices;
    frag.position = cluster.centerOfMass;
    frag.color[0] = sourceEntity.color[0];
    frag.color[1] = sourceEntity.color[1];
    frag.color[2] = sourceEntity.color[2];
    frag.color[3] = sourceEntity.color[3];
    frag.materialType = static_cast<uint8_t>(sourceEntity.materialType);

    int res = sourceEntity.voxelRes;
    float maxS = 0.25f; // Hard cap so fragments stay small
    frag.scale = {
        fminf(sourceEntity.scale[0] / static_cast<float>(res), maxS),
        fminf(sourceEntity.scale[1] / static_cast<float>(res), maxS),
        fminf(sourceEntity.scale[2] / static_cast<float>(res), maxS)
    };

    // Initial velocity: slight outward from entity center + downward
    float dx = cluster.centerOfMass.x - sourceEntity.position[0];
    float dy = cluster.centerOfMass.y - sourceEntity.position[1];
    float dz = cluster.centerOfMass.z - sourceEntity.position[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist > 0.01f) {
        float invDist = 1.0f / dist;
        frag.velocity = { dx * invDist * 2.0f, dy * invDist * 1.5f - 1.0f, dz * invDist * 2.0f };
    } else {
        frag.velocity = { 0.0f, -1.0f, 0.0f };
    }

    // Random angular velocity for tumble
    frag.angularVel = {
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 360.0f,
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 360.0f,
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 360.0f
    };

    frag.lifetime = 0.0f;
    frag.maxLifetime = defaultLifetime;
    frag.fadeStartTime = defaultFadeStart;
    frag.alpha = 1.0f;
    frag.alive = true;
    frag.sleeping = false;
    frag.onGround = false;
    frag.sleepFrameCount = 0;

    // Spawn some dust/debris particles at the fracture point
    auto debrisDef = ParticlePresets::Debris(sourceEntity.color);
    int count = static_cast<int>(cluster.cellIndices.size());
    particles.SpawnBurst(cluster.centerOfMass, debrisDef, count);

    m_fragments.push_back(std::move(frag));
}

void FractureSystem::Update(float dt, float groundY) {
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
            frag.alpha = 1.0f - (frag.lifetime - frag.fadeStartTime) / fadeLen;
            if (frag.alpha < 0.0f) frag.alpha = 0.0f;
        }

        // Skip physics if sleeping
        if (frag.sleeping) continue;

        // Gravity
        frag.velocity.y -= 18.0f * dt;

        // Velocity clamp
        float speedSq = frag.velocity.x * frag.velocity.x +
                         frag.velocity.y * frag.velocity.y +
                         frag.velocity.z * frag.velocity.z;
        if (speedSq > 50.0f * 50.0f) {
            float inv = 50.0f / sqrtf(speedSq);
            frag.velocity.x *= inv;
            frag.velocity.y *= inv;
            frag.velocity.z *= inv;
        }

        // Integrate position
        frag.position.x += frag.velocity.x * dt;
        frag.position.y += frag.velocity.y * dt;
        frag.position.z += frag.velocity.z * dt;

        // Ground collision
        float halfH = frag.scale.y * 0.5f;
        frag.onGround = false;
        if (frag.position.y - halfH < groundY) {
            frag.position.y = groundY + halfH;
            frag.velocity.y = -frag.velocity.y * 0.2f; // Low bounce
            if (fabsf(frag.velocity.y) < 0.5f) frag.velocity.y = 0.0f;
            frag.onGround = true;

            // Ground friction
            frag.velocity.x *= (1.0f - 5.0f * dt);
            frag.velocity.z *= (1.0f - 5.0f * dt);
        }

        // Angular velocity damping
        frag.angularVel.x *= (1.0f - 2.0f * dt);
        frag.angularVel.y *= (1.0f - 2.0f * dt);
        frag.angularVel.z *= (1.0f - 2.0f * dt);

        // Integrate rotation
        frag.rotation.x += frag.angularVel.x * dt;
        frag.rotation.y += frag.angularVel.y * dt;
        frag.rotation.z += frag.angularVel.z * dt;

        // Sleep policy
        speedSq = frag.velocity.x * frag.velocity.x +
                   frag.velocity.y * frag.velocity.y +
                   frag.velocity.z * frag.velocity.z;
        if (frag.onGround && speedSq < frag.sleepThresholdSq) {
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

void FractureSystem::Cleanup() {
    m_fragments.erase(
        std::remove_if(m_fragments.begin(), m_fragments.end(),
                        [](const FractureFragment& f) { return !f.alive; }),
        m_fragments.end()
    );
}

int FractureSystem::GetActiveFragmentCount() const {
    int count = 0;
    for (const auto& f : m_fragments) {
        if (f.alive) count++;
    }
    return count;
}

std::vector<FractureSystem::RenderBatch> FractureSystem::BuildRenderBatches() const {
    // Sort indices by material type, then build contiguous batches.
    std::vector<int> indices;
    indices.reserve(m_fragments.size());
    for (int i = 0; i < static_cast<int>(m_fragments.size()); i++) {
        if (m_fragments[i].alive) indices.push_back(i);
    }

    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_fragments[a].materialType < m_fragments[b].materialType;
    });

    std::vector<RenderBatch> batches;
    if (indices.empty()) return batches;

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

void FractureSystem::WakeNear(const XMFLOAT3& point, float radius) {
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
            // Give a small upward impulse
            float dist = sqrtf(d2);
            float falloff = 1.0f - dist / radius;
            frag.velocity.y += 3.0f * falloff;
            if (dist > 0.01f) {
                float inv = falloff * 2.0f / dist;
                frag.velocity.x += dx * inv;
                frag.velocity.z += dz * inv;
            }
        }
    }
}

} // namespace WT
