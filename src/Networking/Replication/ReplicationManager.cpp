#include "ReplicationManager.h"
#include <algorithm>
#include <cmath>

namespace WT {

using namespace DirectX;

// ============================================================
// Init / Shutdown
// ============================================================

void ReplicationManager::Init() {
    m_entities.clear();
    m_netIdToScene.clear();
    m_sceneToNetId.clear();
    m_connections.clear();
    m_nextNetId = 1;
}

void ReplicationManager::Shutdown() {
    m_entities.clear();
    m_netIdToScene.clear();
    m_sceneToNetId.clear();
    m_connections.clear();
}

// ============================================================
// Entity Registration
// ============================================================

NetEntityId ReplicationManager::RegisterEntity(int sceneIndex) {
    // Check if already registered
    auto it = m_sceneToNetId.find(sceneIndex);
    if (it != m_sceneToNetId.end()) return it->second;

    NetEntityId netId = m_nextNetId++;
    m_netIdToScene[netId] = sceneIndex;
    m_sceneToNetId[sceneIndex] = netId;
    m_entities[netId] = ReplicatedEntityState{};
    m_entities[netId].netId = netId;
    return netId;
}

void ReplicationManager::UnregisterEntity(NetEntityId netId) {
    auto sceneIt = m_netIdToScene.find(netId);
    if (sceneIt != m_netIdToScene.end()) {
        m_sceneToNetId.erase(sceneIt->second);
        m_netIdToScene.erase(sceneIt);
    }
    m_entities.erase(netId);

    // Remove from all connection states
    for (auto& [connId, connState] : m_connections) {
        connState.baseline.erase(netId);
        connState.interestSet.erase(netId);
        connState.priority.erase(netId);
        connState.lastSendTime.erase(netId);
    }
}

int ReplicationManager::GetSceneIndex(NetEntityId netId) const {
    auto it = m_netIdToScene.find(netId);
    return (it != m_netIdToScene.end()) ? it->second : -1;
}

NetEntityId ReplicationManager::GetNetId(int sceneIndex) const {
    auto it = m_sceneToNetId.find(sceneIndex);
    return (it != m_sceneToNetId.end()) ? it->second : InvalidNetEntity;
}

// ============================================================
// State Update
// ============================================================

void ReplicationManager::UpdateEntityState(NetEntityId netId, const ReplicatedEntityState& state) {
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        it->second = state;
        it->second.netId = netId; // Ensure netId is correct
    }
}

// ============================================================
// Connection Management
// ============================================================

void ReplicationManager::AddConnection(uint32_t connectionId) {
    m_connections[connectionId] = ConnectionReplicationState{};
}

void ReplicationManager::RemoveConnection(uint32_t connectionId) {
    m_connections.erase(connectionId);
}

// ============================================================
// Interest Management
// ============================================================

void ReplicationManager::UpdateInterestSets(
    const std::unordered_map<uint32_t, XMFLOAT3>& clientPositions,
    float currentTime) {

    float relRadSq = interestPolicy.relevanceRadius * interestPolicy.relevanceRadius;
    float hiRadSq = interestPolicy.highPriorityRadius * interestPolicy.highPriorityRadius;

    for (auto& [connId, connState] : m_connections) {
        auto posIt = clientPositions.find(connId);
        if (posIt == clientPositions.end()) continue;

        const XMFLOAT3& viewerPos = posIt->second;

        connState.interestSet.clear();

        for (const auto& [netId, entState] : m_entities) {
            float dx = entState.position.x - viewerPos.x;
            float dy = entState.position.y - viewerPos.y;
            float dz = entState.position.z - viewerPos.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq <= relRadSq) {
                connState.interestSet.insert(netId);

                // Accumulate priority based on distance and time since last send
                float distFactor = 1.0f - (distSq / relRadSq);
                float timeSinceLastSend = currentTime - connState.lastSendTime[netId];
                float highPriorityBonus = (distSq < hiRadSq) ? 2.0f : 0.0f;

                connState.priority[netId] += distFactor + timeSinceLastSend * 2.0f + highPriorityBonus;
            } else {
                // Out of range — clear priority
                connState.priority.erase(netId);
            }
        }
    }
}

// ============================================================
// Delta Compression
// ============================================================

uint16_t ReplicationManager::ComputeDirtyFlags(
    const ReplicatedEntityState& current,
    const ReplicatedEntityState& baseline) const {

    uint16_t flags = ReplicatedEntityState::None;

    // Position: threshold check
    float dx = current.position.x - baseline.position.x;
    float dy = current.position.y - baseline.position.y;
    float dz = current.position.z - baseline.position.z;
    if (dx * dx + dy * dy + dz * dz > 0.0001f)
        flags |= ReplicatedEntityState::Position;

    float rx = current.rotation.x - baseline.rotation.x;
    float ry = current.rotation.y - baseline.rotation.y;
    float rz = current.rotation.z - baseline.rotation.z;
    if (rx * rx + ry * ry + rz * rz > 0.01f)
        flags |= ReplicatedEntityState::Rotation;

    float sx = current.scale.x - baseline.scale.x;
    float sy = current.scale.y - baseline.scale.y;
    float sz = current.scale.z - baseline.scale.z;
    if (sx * sx + sy * sy + sz * sz > 0.0001f)
        flags |= ReplicatedEntityState::Scale;

    if (fabsf(current.health - baseline.health) > 0.1f)
        flags |= ReplicatedEntityState::Health;

    if (current.meshType != baseline.meshType)
        flags |= ReplicatedEntityState::MeshType;

    if (current.materialType != baseline.materialType)
        flags |= ReplicatedEntityState::MatType;

    if (current.visible != baseline.visible)
        flags |= ReplicatedEntityState::Visible;

    if (current.destructible != baseline.destructible)
        flags |= ReplicatedEntityState::Destructible;

    if (current.doorOpen != baseline.doorOpen)
        flags |= ReplicatedEntityState::DoorOpen;

    if (current.switchState != baseline.switchState)
        flags |= ReplicatedEntityState::SwitchState;

    if (current.pickupCollected != baseline.pickupCollected)
        flags |= ReplicatedEntityState::PickupCol;

    if (current.voxelDestruction) {
        bool maskDirty = false;
        for (int i = 0; i < 8; i++) {
            if (current.voxelMask[i] != baseline.voxelMask[i]) { maskDirty = true; break; }
        }
        if (maskDirty || current.voxelRes != baseline.voxelRes
            || current.voxelDestruction != baseline.voxelDestruction)
            flags |= ReplicatedEntityState::VoxelMask;
    } else if (current.voxelDestruction != baseline.voxelDestruction) {
        flags |= ReplicatedEntityState::VoxelMask;
    }

    return flags;
}

void ReplicationManager::WriteDelta(BitStream& bs, const ReplicatedEntityState& state,
                                     uint16_t dirtyFlags) const {
    bs.WriteUInt16(state.netId);
    bs.WriteBits(dirtyFlags, 12); // 12 flags now

    if (dirtyFlags & ReplicatedEntityState::Position)
        bs.WriteCompressedVec3(state.position, 500.0f, 16);

    if (dirtyFlags & ReplicatedEntityState::Rotation)
        bs.WriteCompressedVec3(state.rotation, 180.0f, 10);

    if (dirtyFlags & ReplicatedEntityState::Scale)
        bs.WriteVec3(state.scale);

    if (dirtyFlags & ReplicatedEntityState::Health)
        bs.WriteFloat16(state.health);

    if (dirtyFlags & ReplicatedEntityState::MeshType)
        bs.WriteUInt8(state.meshType);

    if (dirtyFlags & ReplicatedEntityState::MatType)
        bs.WriteUInt8(state.materialType);

    if (dirtyFlags & ReplicatedEntityState::Visible)
        bs.WriteBool(state.visible);

    if (dirtyFlags & ReplicatedEntityState::Destructible)
        bs.WriteBool(state.destructible);

    if (dirtyFlags & ReplicatedEntityState::DoorOpen)
        bs.WriteBool(state.doorOpen);

    if (dirtyFlags & ReplicatedEntityState::SwitchState)
        bs.WriteBool(state.switchState);

    if (dirtyFlags & ReplicatedEntityState::PickupCol)
        bs.WriteBool(state.pickupCollected);

    if (dirtyFlags & ReplicatedEntityState::VoxelMask) {
        bs.WriteBool(state.voxelDestruction);
        if (state.voxelDestruction) {
            bs.WriteUInt8(state.voxelRes);
            // Write only the uint64s needed by voxelRes (res^3 bits, up to 512)
            int cellCount = state.voxelRes * state.voxelRes * state.voxelRes;
            int u64Count = (cellCount + 63) / 64;
            for (int i = 0; i < u64Count; i++) {
                bs.WriteUInt32(static_cast<uint32_t>(state.voxelMask[i] & 0xFFFFFFFF));
                bs.WriteUInt32(static_cast<uint32_t>(state.voxelMask[i] >> 32));
            }
        }
    }
}

ReplicatedEntityState ReplicationManager::ReadDelta(BitStream& bs) {
    ReplicatedEntityState state;
    state.netId = bs.ReadUInt16();
    uint16_t dirtyFlags = static_cast<uint16_t>(bs.ReadBits(12));

    if (dirtyFlags & ReplicatedEntityState::Position)
        state.position = bs.ReadCompressedVec3(500.0f, 16);

    if (dirtyFlags & ReplicatedEntityState::Rotation)
        state.rotation = bs.ReadCompressedVec3(180.0f, 10);

    if (dirtyFlags & ReplicatedEntityState::Scale)
        state.scale = bs.ReadVec3();

    if (dirtyFlags & ReplicatedEntityState::Health)
        state.health = bs.ReadFloat16();

    if (dirtyFlags & ReplicatedEntityState::MeshType)
        state.meshType = bs.ReadUInt8();

    if (dirtyFlags & ReplicatedEntityState::MatType)
        state.materialType = bs.ReadUInt8();

    if (dirtyFlags & ReplicatedEntityState::Visible)
        state.visible = bs.ReadBool();

    if (dirtyFlags & ReplicatedEntityState::Destructible)
        state.destructible = bs.ReadBool();

    if (dirtyFlags & ReplicatedEntityState::DoorOpen)
        state.doorOpen = bs.ReadBool();

    if (dirtyFlags & ReplicatedEntityState::SwitchState)
        state.switchState = bs.ReadBool();

    if (dirtyFlags & ReplicatedEntityState::PickupCol)
        state.pickupCollected = bs.ReadBool();

    if (dirtyFlags & ReplicatedEntityState::VoxelMask) {
        state.voxelDestruction = bs.ReadBool();
        if (state.voxelDestruction) {
            state.voxelRes = bs.ReadUInt8();
            int cellCount = state.voxelRes * state.voxelRes * state.voxelRes;
            int u64Count = (cellCount + 63) / 64;
            for (int i = 0; i < u64Count; i++) {
                uint32_t lo = bs.ReadUInt32();
                uint32_t hi = bs.ReadUInt32();
                state.voxelMask[i] = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
            }
        }
    }

    return state;
}

// ============================================================
// Packet Building
// ============================================================

BitStream ReplicationManager::BuildReplicationPacket(uint32_t connectionId, float currentTime) {
    BitStream bs;

    auto connIt = m_connections.find(connectionId);
    if (connIt == m_connections.end()) return bs;

    auto& connState = connIt->second;

    // Collect entities in the interest set, sorted by priority
    struct PrioritisedEntity {
        NetEntityId netId;
        float priority;
    };
    std::vector<PrioritisedEntity> candidates;
    candidates.reserve(connState.interestSet.size());

    for (NetEntityId netId : connState.interestSet) {
        float prio = 0.0f;
        auto prioIt = connState.priority.find(netId);
        if (prioIt != connState.priority.end()) prio = prioIt->second;
        candidates.push_back({ netId, prio });
    }

    // Sort by priority descending
    std::sort(candidates.begin(), candidates.end(),
              [](const PrioritisedEntity& a, const PrioritisedEntity& b) {
                  return a.priority > b.priority;
              });

    // Limit entities per packet
    int maxEnts = interestPolicy.maxEntitiesPerPacket;
    if (static_cast<int>(candidates.size()) > maxEnts) {
        candidates.resize(maxEnts);
    }

    // Write entity count
    bs.WriteUInt16(static_cast<uint16_t>(candidates.size()));

    for (const auto& cand : candidates) {
        auto entIt = m_entities.find(cand.netId);
        if (entIt == m_entities.end()) continue;

        const auto& currentState = entIt->second;

        // Check if we have a baseline for this entity on this connection
        auto baselineIt = connState.baseline.find(cand.netId);
        if (baselineIt != connState.baseline.end()) {
            // Delta compression
            uint16_t dirtyFlags = ComputeDirtyFlags(currentState, baselineIt->second);
            if (dirtyFlags == ReplicatedEntityState::None) {
                // No changes — skip
                bs.WriteBool(false); // "no update" marker
                continue;
            }
            bs.WriteBool(true);  // "has update" marker
            bs.WriteBool(false); // "is delta" (not full)
            WriteDelta(bs, currentState, dirtyFlags);
        } else {
            // Full state (new entity for this connection)
            bs.WriteBool(true);  // "has update"
            bs.WriteBool(true);  // "is full"
            WriteDelta(bs, currentState, ReplicatedEntityState::All);
        }

        // Update send tracking
        connState.lastSendTime[cand.netId] = currentTime;
        connState.priority[cand.netId] = 0.0f; // Reset priority after sending
    }

    return bs;
}

void ReplicationManager::ProcessAck(uint32_t connectionId, NetEntityId netId) {
    auto connIt = m_connections.find(connectionId);
    if (connIt == m_connections.end()) return;

    auto entIt = m_entities.find(netId);
    if (entIt == m_entities.end()) return;

    // Advance the baseline to current state
    connIt->second.baseline[netId] = entIt->second;
}

// ============================================================
// Client-Side Parsing
// ============================================================

std::vector<ReplicatedEntityState> ReplicationManager::ParseReplicationPacket(const BitStream& packet) {
    std::vector<ReplicatedEntityState> results;

    // Need a mutable copy for reading
    BitStream reader(packet.GetData(), packet.GetSizeBytes());

    uint16_t entityCount = reader.ReadUInt16();
    results.reserve(entityCount);

    for (int i = 0; i < entityCount; i++) {
        bool hasUpdate = reader.ReadBool();
        if (!hasUpdate) continue;

        bool isFull = reader.ReadBool();
        (void)isFull; // Both full and delta use same format, just different dirty flags

        ReplicatedEntityState state = ReadDelta(reader);
        results.push_back(state);

        if (reader.IsOverflow()) break;
    }

    return results;
}

} // namespace WT
