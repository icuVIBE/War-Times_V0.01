#pragma once
// ============================================================
// SceneCulling — Frustum culling + distance-based level streaming
// ============================================================

#include <DirectXMath.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../Scene/Spatial/BVH.h"
#include "ResourceManager.h"

namespace WT {

using namespace DirectX;

// ============================================================
// Frustum — 6 planes extracted from a ViewProjection matrix
// ============================================================
struct Frustum {
    XMFLOAT4 planes[6]; // Left, Right, Bottom, Top, Near, Far  (a,b,c,d => ax+by+cz+d=0)

    // Extract frustum planes from a row-major ViewProjection matrix.
    // Planes are normalized so distance tests give world-space units.
    void ExtractFromViewProj(XMMATRIX vp) {
        // We need the matrix in row-major form for Gribb/Hartmann extraction.
        // DirectXMath stores row-major by default in XMMATRIX.
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, vp);

        // Left:   row3 + row0
        planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
        // Right:  row3 - row0
        planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
        // Bottom: row3 + row1
        planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
        // Top:    row3 - row1
        planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
        // Near:   row2
        planes[4] = { m._13, m._23, m._33, m._43 };
        // Far:    row3 - row2
        planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

        // Normalize each plane
        for (int i = 0; i < 6; i++) {
            float len = sqrtf(planes[i].x * planes[i].x +
                              planes[i].y * planes[i].y +
                              planes[i].z * planes[i].z);
            if (len > 0.0001f) {
                float inv = 1.0f / len;
                planes[i].x *= inv;
                planes[i].y *= inv;
                planes[i].z *= inv;
                planes[i].w *= inv;
            }
        }
    }

    // Test an AABB against the frustum. Returns true if the AABB is
    // at least partially inside (conservative — no false negatives).
    bool TestAABB(const XMFLOAT3& center, const XMFLOAT3& halfExtents) const {
        for (int i = 0; i < 6; i++) {
            // Compute the positive extent vertex (the corner most aligned with the plane normal)
            float d = planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w;
            float r = fabsf(planes[i].x) * halfExtents.x +
                      fabsf(planes[i].y) * halfExtents.y +
                      fabsf(planes[i].z) * halfExtents.z;
            if (d + r < 0.0f) return false; // Entirely outside this plane
        }
        return true;
    }

    // Test a sphere against the frustum
    bool TestSphere(const XMFLOAT3& center, float radius) const {
        for (int i = 0; i < 6; i++) {
            float d = planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w;
            if (d + radius < 0.0f) return false;
        }
        return true;
    }
};

// ============================================================
// EntityBounds — tight AABB computed from oriented bounding box
// ============================================================
struct EntityBounds {
    XMFLOAT3 center;
    XMFLOAT3 halfExtents;
    float    boundingSphereRadius;

    void ComputeFromEntity(const float pos[3], const float scl[3], const float rot[3],
                           const XMFLOAT3* meshBoundsCenter = nullptr,
                           const XMFLOAT3* meshBoundsHalfExt = nullptr) {
        // Start with mesh-space bounds or default unit cube (half-extents = 0.5)
        float lhx, lhy, lhz;  // local half-extents
        float lcx = 0, lcy = 0, lcz = 0; // local center offset
        if (meshBoundsCenter && meshBoundsHalfExt) {
            lhx = meshBoundsHalfExt->x * fabsf(scl[0]);
            lhy = meshBoundsHalfExt->y * fabsf(scl[1]);
            lhz = meshBoundsHalfExt->z * fabsf(scl[2]);
            lcx = meshBoundsCenter->x * scl[0];
            lcy = meshBoundsCenter->y * scl[1];
            lcz = meshBoundsCenter->z * scl[2];
        } else {
            lhx = fabsf(scl[0]) * 0.5f;
            lhy = fabsf(scl[1]) * 0.5f;
            lhz = fabsf(scl[2]) * 0.5f;
        }

        bool hasRotation = (fabsf(rot[0]) > 0.01f || fabsf(rot[1]) > 0.01f || fabsf(rot[2]) > 0.01f);

        if (hasRotation) {
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(rot[0]),
                XMConvertToRadians(rot[1]),
                XMConvertToRadians(rot[2]));
            XMFLOAT4X4 rm;
            XMStoreFloat4x4(&rm, R);

            // Rotate the center offset
            XMVECTOR off = XMVectorSet(lcx, lcy, lcz, 0);
            off = XMVector3TransformNormal(off, R);
            XMFLOAT3 rOff;
            XMStoreFloat3(&rOff, off);
            center = { pos[0] + rOff.x, pos[1] + rOff.y, pos[2] + rOff.z };

            // World AABB half-extents = |R| * local half-extents
            halfExtents.x = fabsf(rm._11) * lhx + fabsf(rm._21) * lhy + fabsf(rm._31) * lhz;
            halfExtents.y = fabsf(rm._12) * lhx + fabsf(rm._22) * lhy + fabsf(rm._32) * lhz;
            halfExtents.z = fabsf(rm._13) * lhx + fabsf(rm._23) * lhy + fabsf(rm._33) * lhz;
        } else {
            center = { pos[0] + lcx, pos[1] + lcy, pos[2] + lcz };
            halfExtents = { lhx, lhy, lhz };
        }

        boundingSphereRadius = sqrtf(halfExtents.x * halfExtents.x +
                                     halfExtents.y * halfExtents.y +
                                     halfExtents.z * halfExtents.z);
    }
};

// ============================================================
// SceneCuller — performs frustum culling on a list of entities
// Produces a visibility bitset each frame.
// ============================================================
class SceneCuller {
public:
    // Rebuild bounds cache. Call when entities are added/removed/transformed.
    template <typename GetEntityFn>
    void RebuildBounds(int entityCount, GetEntityFn getEntity) {
        m_bounds.resize(entityCount);
        for (int i = 0; i < entityCount; i++) {
            const auto& e = getEntity(i);
            // For custom meshes, use actual mesh bounds instead of assuming unit cube
            if (e.meshType == MeshType::Custom && !e.meshName.empty()) {
                auto* mesh = ResourceManager::Get().GetMesh(e.meshName);
                if (mesh && mesh->HasBounds()) {
                    XMFLOAT3 bc = mesh->GetBoundsCenter();
                    XMFLOAT3 bh = mesh->GetBoundsHalfExtent();
                    m_bounds[i].ComputeFromEntity(e.position, e.scale, e.rotation, &bc, &bh);
                    continue;
                }
            }
            m_bounds[i].ComputeFromEntity(e.position, e.scale, e.rotation);
        }
        m_boundsEntityCount = entityCount;
        m_boundsDirty = false;
    }

    // Rebuild visibility list. Call once per frame before rendering.
    // If useBVH is true, uses O(log N) tree traversal; otherwise O(N) linear scan.
    // categoryDists: per-CullCategory distances [Building, Prop, Detail, Foliage, Always]
    template <typename GetEntityFn>
    void Update(int entityCount, GetEntityFn getEntity,
                XMMATRIX viewProj, const XMFLOAT3& cameraPos,
                float streamDist, bool useBVH = false,
                const float* categoryDists = nullptr) {
        // Only rebuild bounds when dirty or entity count changed
        if (m_boundsDirty || entityCount != m_boundsEntityCount) {
            RebuildBounds(entityCount, getEntity);
            m_bvhDirty = true;
        }

        // Extract frustum
        m_frustum.ExtractFromViewProj(viewProj);
        m_streamDistance = streamDist;
        m_cameraPos = cameraPos;

        // Resize visibility array (clear all to 0)
        m_visible.assign(entityCount, 0);
        m_stats = {};

        bool useDistanceCull = streamDist > 0.0f;

        if (useBVH && entityCount > 0) {
            UpdateBVH(entityCount, getEntity, cameraPos, streamDist, useDistanceCull, categoryDists);
        } else {
            UpdateLinear(entityCount, getEntity, cameraPos, streamDist, useDistanceCull, categoryDists);
        }
    }

private:
    // --- Linear O(N) path (original) ---
    template <typename GetEntityFn>
    void UpdateLinear(int entityCount, GetEntityFn getEntity,
                      const XMFLOAT3& cameraPos, float streamDist, bool useDistanceCull,
                      const float* categoryDists) {
        for (int i = 0; i < entityCount; i++) {
            const auto& e = getEntity(i);

            if (!e.visible) continue;

            m_stats.totalEntities++;

            const EntityBounds& bounds = m_bounds[i];

            // Distance culling (level streaming) — cheap reject first
            if (useDistanceCull) {
                // CullCategory::Always entities skip distance culling
                uint8_t cat = static_cast<uint8_t>(e.cullCategory);
                if (cat != 4) { // 4 = Always
                    float catDist = (categoryDists && cat < 5) ? categoryDists[cat] : streamDist;
                    float dx = bounds.center.x - cameraPos.x;
                    float dy = bounds.center.y - cameraPos.y;
                    float dz = bounds.center.z - cameraPos.z;
                    float dist2 = dx * dx + dy * dy + dz * dz;
                    float effectiveRange = catDist + bounds.boundingSphereRadius;

                    if (dist2 > effectiveRange * effectiveRange) {
                        m_stats.distanceCulled++;
                        continue;
                    }
                }
            }

            // Frustum culling — tight AABB test
            if (m_frustum.TestAABB(bounds.center, bounds.halfExtents)) {
                m_visible[i] = 1;
                m_stats.rendered++;
            } else {
                m_stats.frustumCulled++;
            }
        }
    }

    // --- BVH O(log N + K) path ---
    template <typename GetEntityFn>
    void UpdateBVH(int entityCount, GetEntityFn getEntity,
                   const XMFLOAT3& cameraPos, float streamDist, bool useDistanceCull,
                   const float* categoryDists) {
        // Rebuild BVH if needed
        if (m_bvhDirty) {
            // Extract centers and half-extents for BVH build
            m_bvhCenters.resize(entityCount);
            m_bvhHalfExtents.resize(entityCount);
            for (int i = 0; i < entityCount; i++) {
                m_bvhCenters[i] = m_bounds[i].center;
                m_bvhHalfExtents[i] = m_bounds[i].halfExtents;
            }
            m_bvh.Build(m_bvhCenters.data(), m_bvhHalfExtents.data(), entityCount);
            m_bvhDirty = false;
        }

        // Count total visible entities (not hidden via Entity::visible flag)
        for (int i = 0; i < entityCount; i++) {
            if (getEntity(i).visible) m_stats.totalEntities++;
        }

        // Step 1: If distance culling is active, use sphere query to get candidates
        // Step 2: Use frustum query to get visible set
        // Combined approach: frustum query on BVH, then distance-cull survivors
        m_queryResults.clear();
        m_bvh.QueryFrustum(m_frustum, m_queryResults);

        for (int idx : m_queryResults) {
            if (idx < 0 || idx >= entityCount) continue;
            const auto& e = getEntity(idx);
            if (!e.visible) continue;

            const EntityBounds& bounds = m_bounds[idx];

            // Distance culling post-filter
            if (useDistanceCull) {
                uint8_t cat = static_cast<uint8_t>(e.cullCategory);
                if (cat != 4) { // 4 = Always
                    float catDist = (categoryDists && cat < 5) ? categoryDists[cat] : streamDist;
                    float dx = bounds.center.x - cameraPos.x;
                    float dy = bounds.center.y - cameraPos.y;
                    float dz = bounds.center.z - cameraPos.z;
                    float dist2 = dx * dx + dy * dy + dz * dz;
                    float effectiveRange = catDist + bounds.boundingSphereRadius;
                    if (dist2 > effectiveRange * effectiveRange) {
                        m_stats.distanceCulled++;
                        continue;
                    }
                }
            }

            m_visible[idx] = 1;
            m_stats.rendered++;
        }

        // Frustum-culled = total visible entities - rendered - distance culled
        m_stats.frustumCulled = m_stats.totalEntities - m_stats.rendered - m_stats.distanceCulled;
    }

public:

    // Check if entity at index should be rendered
    bool IsVisible(int index) const {
        if (index < 0 || index >= static_cast<int>(m_visible.size())) return false;
        return m_visible[index] != 0;
    }

    // Shadow pass frustum test for a specific entity (uses light frustum)
    bool IsVisibleShadow(int index) const {
        if (index < 0 || index >= static_cast<int>(m_shadowVisible.size())) return false;
        return m_shadowVisible[index] != 0;
    }

    // Update shadow visibility using the light's view-projection
    // Reuses cached bounds from Update() — no recomputation.
    template <typename GetEntityFn>
    void UpdateShadowFrustum(int entityCount, GetEntityFn getEntity,
                             XMMATRIX lightVP, const XMFLOAT3& cameraPos,
                             float shadowDist) {
        m_shadowFrustum.ExtractFromViewProj(lightVP);
        m_shadowVisible.resize(entityCount);

        for (int i = 0; i < entityCount; i++) {
            const auto& e = getEntity(i);
            if (!e.visible || !e.castShadow) {
                m_shadowVisible[i] = 0;
                continue;
            }

            const EntityBounds& bounds = (i < m_boundsEntityCount) ? m_bounds[i]
                                         : m_fallbackBounds;

            // Distance cull for shadows (typically shorter range)
            float dx = bounds.center.x - cameraPos.x;
            float dz = bounds.center.z - cameraPos.z;
            float dist2 = dx * dx + dz * dz;
            float effectiveRange = shadowDist + bounds.boundingSphereRadius;
            if (dist2 > effectiveRange * effectiveRange) {
                m_shadowVisible[i] = 0;
                continue;
            }

            m_shadowVisible[i] = m_shadowFrustum.TestAABB(bounds.center, bounds.halfExtents) ? 1 : 0;
        }
    }

    // Force bounds rebuild (call after scene changes like PCG, load, entity edit)
    void InvalidateBounds() { m_boundsDirty = true; m_bvhDirty = true; }

    // Statistics
    struct CullStats {
        int totalEntities  = 0;
        int frustumCulled  = 0;
        int distanceCulled = 0;
        int rendered       = 0;
    };

    const CullStats& GetStats() const { return m_stats; }
    const Frustum& GetFrustum() const { return m_frustum; }
    int GetBVHNodeCount() const { return m_bvh.GetNodeCount(); }
    int GetBVHDepth() const { return m_bvh.GetDepth(); }
    const EntityBounds& GetBounds(int index) const {
        return (index >= 0 && index < m_boundsEntityCount) ? m_bounds[index] : m_fallbackBounds;
    }
    int GetBoundsCount() const { return m_boundsEntityCount; }

private:
    Frustum m_frustum;
    Frustum m_shadowFrustum;
    std::vector<uint8_t> m_visible;
    std::vector<uint8_t> m_shadowVisible;
    std::vector<EntityBounds> m_bounds;
    int m_boundsEntityCount = -1;
    bool m_boundsDirty = true;
    EntityBounds m_fallbackBounds = {};
    float m_streamDistance = 0.0f;
    XMFLOAT3 m_cameraPos = {};
    CullStats m_stats;

    // BVH acceleration
    BVH m_bvh;
    bool m_bvhDirty = true;
    std::vector<XMFLOAT3> m_bvhCenters;
    std::vector<XMFLOAT3> m_bvhHalfExtents;
    std::vector<int> m_queryResults;
};

} // namespace WT
