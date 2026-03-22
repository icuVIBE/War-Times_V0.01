#pragma once

#include "../Packets/PacketDefs.h"
#include "../Packets/BitStream.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <DirectXMath.h>

namespace WT {

// ============================================================
// Net Entity ID — network-synced entity identifier
// ============================================================

using NetEntityId = uint16_t;
static constexpr NetEntityId InvalidNetEntity = 0;

// ============================================================
// Replicated Entity State — full snapshot for delta comparison
// ============================================================

struct ReplicatedEntityState {
    NetEntityId     netId = InvalidNetEntity;
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };
    DirectX::XMFLOAT3 scale    = { 1, 1, 1 };
    float           health = 100.0f;
    uint8_t         meshType = 0;
    uint8_t         materialType = 0;
    bool            visible = true;
    bool            destructible = true;

    // Interaction state
    bool            doorOpen = false;
    bool            switchState = false;
    bool            pickupCollected = false;

    // Voxel destruction mask (up to 8x8x8 = 512 bits)
    uint64_t        voxelMask[8] = { 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                      0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                      0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                      0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF };
    uint8_t         voxelRes = 2;
    bool            voxelDestruction = false;

    // Delta flags — which fields changed since last ack'd baseline
    enum DirtyFlags : uint16_t {
        None        = 0,
        Position    = 1 << 0,
        Rotation    = 1 << 1,
        Scale       = 1 << 2,
        Health      = 1 << 3,
        MeshType    = 1 << 4,
        MatType     = 1 << 5,
        Visible     = 1 << 6,
        Destructible= 1 << 7,
        DoorOpen    = 1 << 8,
        SwitchState = 1 << 9,
        PickupCol   = 1 << 10,
        VoxelMask   = 1 << 11,
        All         = 0xFFF
    };
};

// ============================================================
// Interest Management — determines which entities a client
// should receive updates for, based on relevance.
// ============================================================

struct InterestPolicy {
    float relevanceRadius = 150.0f;     // Max distance for entity updates
    float highPriorityRadius = 30.0f;   // Distance for high-frequency updates
    int   maxEntitiesPerPacket = 32;    // Limit per snapshot packet
    float baseUpdateRate = 0.05f;       // 20 Hz for nearby entities
    float farUpdateRate = 0.2f;         // 5 Hz for distant entities
};

// ============================================================
// Per-Connection Replication State
// ============================================================

struct ConnectionReplicationState {
    // Last acknowledged baseline per entity
    std::unordered_map<NetEntityId, ReplicatedEntityState> baseline;

    // Entities currently in the client's interest set
    std::unordered_set<NetEntityId> interestSet;

    // Priority accumulator: entities accumulate priority over time,
    // and the highest-priority entities get sent first.
    std::unordered_map<NetEntityId, float> priority;

    // Last send time per entity (for rate limiting)
    std::unordered_map<NetEntityId, float> lastSendTime;
};

// ============================================================
// Replication Manager — entity replication with delta compression
// and interest management
// ============================================================

class ReplicationManager {
public:
    void Init();
    void Shutdown();

    // ---- Entity Registration ----

    // Register a scene entity for network replication. Returns its NetEntityId.
    NetEntityId RegisterEntity(int sceneIndex);

    // Unregister an entity (destroyed/removed).
    void UnregisterEntity(NetEntityId netId);

    // Get the scene index for a net entity.
    int GetSceneIndex(NetEntityId netId) const;

    // Get the net ID for a scene index (-1 if not registered).
    NetEntityId GetNetId(int sceneIndex) const;

    // ---- State Update ----

    // Update the authoritative state for a replicated entity.
    void UpdateEntityState(NetEntityId netId, const ReplicatedEntityState& state);

    // ---- Connection Management ----

    // Track a new client connection for replication.
    void AddConnection(uint32_t connectionId);

    // Remove a client connection.
    void RemoveConnection(uint32_t connectionId);

    // ---- Interest Management ----

    // Recalculate which entities are relevant to each client.
    // clientPositions maps connectionId → viewer position.
    void UpdateInterestSets(const std::unordered_map<uint32_t, DirectX::XMFLOAT3>& clientPositions,
                            float currentTime);

    // ---- Delta Compression & Packet Building ----

    // Build a replication packet for a specific connection.
    // Returns serialised data ready to send. Priority-sorted, delta-compressed.
    BitStream BuildReplicationPacket(uint32_t connectionId, float currentTime);

    // Process an incoming replication ack from a client.
    // Updates the baseline to the acknowledged state.
    void ProcessAck(uint32_t connectionId, NetEntityId netId);

    // ---- Client-Side: Apply Incoming Replication ----

    // Parse a replication packet and return the entity states it contains.
    static std::vector<ReplicatedEntityState> ParseReplicationPacket(const BitStream& packet);

    // Settings
    InterestPolicy interestPolicy;

private:
    // Compute dirty flags by comparing current state to baseline.
    uint16_t ComputeDirtyFlags(const ReplicatedEntityState& current,
                                const ReplicatedEntityState& baseline) const;

    // Write delta-compressed entity state to a bitstream.
    void WriteDelta(BitStream& bs, const ReplicatedEntityState& state, uint16_t dirtyFlags) const;

    // Read delta-compressed entity state from a bitstream.
    static ReplicatedEntityState ReadDelta(BitStream& bs);

    // All authoritative entity states
    std::unordered_map<NetEntityId, ReplicatedEntityState> m_entities;

    // Mapping between scene indices and net IDs
    std::unordered_map<NetEntityId, int> m_netIdToScene;
    std::unordered_map<int, NetEntityId> m_sceneToNetId;

    // Per-connection replication tracking
    std::unordered_map<uint32_t, ConnectionReplicationState> m_connections;

    NetEntityId m_nextNetId = 1;
};

} // namespace WT
