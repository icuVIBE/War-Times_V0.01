#pragma once
// ============================================================
// BVH — Bounding Volume Hierarchy for fast spatial queries
// ============================================================
// Top-down AABB tree. Replaces linear O(N) frustum scans with
// O(log N + K) traversal where K = visible entity count.
// Header-only for template convenience.

#include <DirectXMath.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace WT {

using namespace DirectX;

// ============================================================
// BVH Node — stored in a flat array for cache efficiency
// ============================================================
struct BVHNode {
    XMFLOAT3 aabbMin;
    XMFLOAT3 aabbMax;
    int      left       = -1;   // Left child index  (-1 = leaf)
    int      right      = -1;   // Right child index
    int      entityIndex = -1;  // Entity index (leaf only)

    bool IsLeaf() const { return left == -1; }
};

// ============================================================
// BVH — build from entity bounds, query with frustum
// ============================================================
class BVH {
public:
    // Build the tree from entity bounds (center + halfExtents).
    // Call when entities are added/removed/moved.
    void Build(const XMFLOAT3* centers, const XMFLOAT3* halfExtents, int count) {
        m_nodes.clear();
        if (count == 0) return;

        // Pre-allocate (max 2N-1 nodes for N leaves)
        m_nodes.reserve(count * 2);

        // Build index list
        std::vector<int> indices(count);
        for (int i = 0; i < count; i++) indices[i] = i;

        BuildRecursive(indices.data(), count, centers, halfExtents);
    }

    // Query: find all entity indices whose AABB overlaps the frustum.
    // Appends to `results`. This is the main performance win — skips
    // entire subtrees when their bounding box is outside the frustum.
    template <typename FrustumT>
    void QueryFrustum(const FrustumT& frustum, std::vector<int>& results) const {
        if (m_nodes.empty()) return;
        QueryFrustumRecursive(0, frustum, results);
    }

    // Query: find all entity indices within a sphere (distance culling).
    void QuerySphere(const XMFLOAT3& center, float radius, std::vector<int>& results) const {
        if (m_nodes.empty()) return;
        QuerySphereRecursive(0, center, radius * radius, results);
    }

    int GetNodeCount() const { return static_cast<int>(m_nodes.size()); }
    int GetDepth() const { return m_depth; }

private:
    // Recursive top-down build with median split along longest axis
    int BuildRecursive(int* indices, int count,
                       const XMFLOAT3* centers, const XMFLOAT3* halfExtents) {
        int nodeIndex = static_cast<int>(m_nodes.size());
        m_nodes.push_back(BVHNode{});
        BVHNode& node = m_nodes[nodeIndex];

        // Compute AABB of all entities in this group
        XMFLOAT3 groupMin = { 1e18f, 1e18f, 1e18f };
        XMFLOAT3 groupMax = { -1e18f, -1e18f, -1e18f };
        for (int i = 0; i < count; i++) {
            int idx = indices[i];
            float minX = centers[idx].x - halfExtents[idx].x;
            float minY = centers[idx].y - halfExtents[idx].y;
            float minZ = centers[idx].z - halfExtents[idx].z;
            float maxX = centers[idx].x + halfExtents[idx].x;
            float maxY = centers[idx].y + halfExtents[idx].y;
            float maxZ = centers[idx].z + halfExtents[idx].z;
            if (minX < groupMin.x) groupMin.x = minX;
            if (minY < groupMin.y) groupMin.y = minY;
            if (minZ < groupMin.z) groupMin.z = minZ;
            if (maxX > groupMax.x) groupMax.x = maxX;
            if (maxY > groupMax.y) groupMax.y = maxY;
            if (maxZ > groupMax.z) groupMax.z = maxZ;
        }
        node.aabbMin = groupMin;
        node.aabbMax = groupMax;

        // Leaf node
        if (count == 1) {
            node.entityIndex = indices[0];
            return nodeIndex;
        }

        // Find longest axis to split along
        float extX = groupMax.x - groupMin.x;
        float extY = groupMax.y - groupMin.y;
        float extZ = groupMax.z - groupMin.z;
        int axis = 0;
        if (extY > extX && extY > extZ) axis = 1;
        else if (extZ > extX && extZ > extY) axis = 2;

        // Sort indices by center along split axis
        const XMFLOAT3* c = centers;
        std::sort(indices, indices + count, [c, axis](int a, int b) {
            if (axis == 0) return c[a].x < c[b].x;
            if (axis == 1) return c[a].y < c[b].y;
            return c[a].z < c[b].z;
        });

        // Split at median
        int mid = count / 2;

        // Recurse (note: m_nodes may reallocate, so don't hold references across calls)
        int leftIdx  = BuildRecursive(indices, mid, centers, halfExtents);
        int rightIdx = BuildRecursive(indices + mid, count - mid, centers, halfExtents);

        // Re-fetch node after potential reallocation
        m_nodes[nodeIndex].left  = leftIdx;
        m_nodes[nodeIndex].right = rightIdx;

        // Track depth
        int leftDepth  = DepthOf(leftIdx);
        int rightDepth = DepthOf(rightIdx);
        int d = 1 + (leftDepth > rightDepth ? leftDepth : rightDepth);
        if (d > m_depth) m_depth = d;

        return nodeIndex;
    }

    int DepthOf(int nodeIdx) const {
        const auto& n = m_nodes[nodeIdx];
        if (n.IsLeaf()) return 1;
        int ld = DepthOf(n.left);
        int rd = DepthOf(n.right);
        return 1 + (ld > rd ? ld : rd);
    }

    // Frustum traversal — prune entire subtrees that are outside
    template <typename FrustumT>
    void QueryFrustumRecursive(int nodeIndex, const FrustumT& frustum,
                               std::vector<int>& results) const {
        const BVHNode& node = m_nodes[nodeIndex];

        // Convert node AABB to center + halfExtents for frustum test
        XMFLOAT3 center = {
            (node.aabbMin.x + node.aabbMax.x) * 0.5f,
            (node.aabbMin.y + node.aabbMax.y) * 0.5f,
            (node.aabbMin.z + node.aabbMax.z) * 0.5f
        };
        XMFLOAT3 half = {
            (node.aabbMax.x - node.aabbMin.x) * 0.5f,
            (node.aabbMax.y - node.aabbMin.y) * 0.5f,
            (node.aabbMax.z - node.aabbMin.z) * 0.5f
        };

        // If this node's AABB is entirely outside the frustum, skip subtree
        if (!frustum.TestAABB(center, half)) return;

        if (node.IsLeaf()) {
            results.push_back(node.entityIndex);
            return;
        }

        QueryFrustumRecursive(node.left, frustum, results);
        QueryFrustumRecursive(node.right, frustum, results);
    }

    // Sphere query (distance culling)
    void QuerySphereRecursive(int nodeIndex, const XMFLOAT3& center,
                              float radiusSq, std::vector<int>& results) const {
        const BVHNode& node = m_nodes[nodeIndex];

        // Closest point on AABB to sphere center
        float cx = (center.x < node.aabbMin.x) ? node.aabbMin.x :
                   (center.x > node.aabbMax.x) ? node.aabbMax.x : center.x;
        float cy = (center.y < node.aabbMin.y) ? node.aabbMin.y :
                   (center.y > node.aabbMax.y) ? node.aabbMax.y : center.y;
        float cz = (center.z < node.aabbMin.z) ? node.aabbMin.z :
                   (center.z > node.aabbMax.z) ? node.aabbMax.z : center.z;
        float dx = center.x - cx;
        float dy = center.y - cy;
        float dz = center.z - cz;
        if (dx * dx + dy * dy + dz * dz > radiusSq) return;

        if (node.IsLeaf()) {
            results.push_back(node.entityIndex);
            return;
        }

        QuerySphereRecursive(node.left, center, radiusSq, results);
        QuerySphereRecursive(node.right, center, radiusSq, results);
    }

    std::vector<BVHNode> m_nodes;
    int m_depth = 0;
};

} // namespace WT
