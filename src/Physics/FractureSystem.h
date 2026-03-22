#pragma once

#include <vector>
#include <cstdint>
#include <DirectXMath.h>

namespace WT {

struct Entity;
class Scene;
class GPUParticleSystem;

// ============================================================
// Connection Graph — adjacency structure for voxel cells
// ============================================================
// Nodes = active voxel cells. Edges = 6-connected face adjacency.
// Used to detect disconnected clusters after cell removal.

struct VoxelCluster {
    std::vector<int> cellIndices;   // Cell indices belonging to this cluster
    bool             grounded;       // At least one cell touches the bottom row (vy==0)
    DirectX::XMFLOAT3 centerOfMass; // Average world position of cluster cells
    float            totalVolume;    // Sum of cell volumes
};

class ConnectionGraph {
public:
    // Rebuild the adjacency graph for an entity's voxel grid.
    // Call after any cell removal to detect disconnected clusters.
    // Returns clusters sorted by size (largest first).
    std::vector<VoxelCluster> FindClusters(const Entity& entity) const;

private:
    // Flood-fill from a seed cell, marking visited cells.
    void FloodFill(int seed, int res, const Entity& entity,
                   std::vector<bool>& visited, VoxelCluster& out) const;

    // Get the 6-connected face neighbours of a cell.
    void GetNeighbours(int idx, int res, int neighbours[6], int& count) const;
};

// ============================================================
// Fracture Fragment — a detached cluster of voxels that becomes
// a dynamic physics object before converting to debris.
// ============================================================

struct FractureFragment {
    int    sourceEntityIndex = -1;   // Original entity this came from
    std::vector<int> cellIndices;    // Which voxel cells belong to this fragment

    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };     // Accumulated euler degrees
    DirectX::XMFLOAT3 angularVel = { 0, 0, 0 };
    DirectX::XMFLOAT3 scale = { 1, 1, 1 };         // Per-cell scale of source entity
    float color[4] = { 0.6f, 0.6f, 0.6f, 1.0f };

    float lifetime = 0.0f;
    float maxLifetime = 4.0f;       // Seconds before despawn
    float fadeStartTime = 3.0f;     // When to begin alpha fade
    float alpha = 1.0f;

    // Sleep policy
    float sleepThresholdSq = 0.04f; // Velocity^2 below this → consider sleeping
    int   sleepFrameCount = 0;      // Consecutive frames below threshold
    int   sleepFrameLimit = 5;      // Frames needed to enter sleep
    bool  sleeping = false;
    bool  onGround = false;

    bool  alive = true;

    // Material from source entity
    uint8_t materialType = 0;       // Cast to MaterialType
};

// ============================================================
// Fracture System — runtime fracture evaluation & management
// ============================================================
// When voxel cells are removed, this system:
//   1. Evaluates the connection graph for disconnected clusters
//   2. Converts unsupported clusters into FractureFragments
//   3. Steps fragment physics (gravity, collision, sleep)
//   4. Manages fragment lifecycle (fade, despawn)
//   5. Batches fragments by material for efficient rendering

class FractureSystem {
public:
    void Init();
    void Shutdown();

    // Evaluate fracture for a specific entity after voxel removal.
    // Creates FractureFragments for any disconnected, ungrounded clusters.
    // Returns number of fragments spawned.
    int EvaluateFracture(int entityIndex, Scene& scene, GPUParticleSystem& particles);

    // Step all active fragments (physics, sleep, lifecycle).
    void Update(float dt, float groundY);

    // Remove dead fragments.
    void Cleanup();

    // Access fragments for rendering.
    const std::vector<FractureFragment>& GetFragments() const { return m_fragments; }
    int GetActiveFragmentCount() const;

    // Batch info for rendering — groups of fragments sharing the same material type.
    struct RenderBatch {
        uint8_t materialType;
        int     startIndex;
        int     count;
    };
    std::vector<RenderBatch> BuildRenderBatches() const;

    // Wake fragments near an explosion point.
    void WakeNear(const DirectX::XMFLOAT3& point, float radius);

    // Clear all fragments (level change, etc.).
    void Clear();

    // Tuning
    int   maxFragments = 256;       // Hard limit on active fragments
    float defaultLifetime = 4.0f;
    float defaultFadeStart = 3.0f;

private:
    void SpawnFragment(const VoxelCluster& cluster, const Entity& sourceEntity,
                       int entityIndex, GPUParticleSystem& particles);

    ConnectionGraph m_graph;
    std::vector<FractureFragment> m_fragments;
};

} // namespace WT
