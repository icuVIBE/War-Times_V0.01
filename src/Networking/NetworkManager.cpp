#include "NetworkManager.h"
#include "../Core/Entity.h"
#include "../Util/Log.h"
#include <cstring>

namespace WT {

using namespace DirectX;

// ============================================================
// Init / Shutdown
// ============================================================

void NetworkManager::Init() {
    m_role = NetworkRole::Offline;
    m_localPlayerId = 0;
    m_snapshotTimer = 0.0f;
    m_snapshotInterval = 1.0f / snapshotRate;
    m_serverTime = 0.0f;
    m_lastBytesSent = 0;
    m_lastBytesRecv = 0;
    m_bandwidthTimer = 0.0f;
    m_bandwidthInKBps = 0.0f;
    m_bandwidthOutKBps = 0.0f;
    m_scene = nullptr;
    m_pendingLevelSends.clear();
    m_levelTotalChunks = 0;
    m_levelTotalEntities = 0;
    m_levelEntities.clear();
    m_levelChunksReceived.clear();
    m_clientLevelReceived = false;
}

void NetworkManager::Shutdown() {
    LeaveGame();
}

// ============================================================
// Session Management
// ============================================================

bool NetworkManager::HostGame(uint16_t port, bool listenServer) {
    if (m_role != NetworkRole::Offline) LeaveGame();

    m_transport = std::make_unique<WinsockTransport>();
    if (!m_transport->Init()) return false;

    m_transport->SetCallbacks({
        [this](ConnectionId id) { OnConnected(id); },
        [this](ConnectionId id) { OnDisconnected(id); },
        [this](ConnectionId id, const uint8_t* data, int size) { OnDataReceived(id, data, size); }
    });

    if (!m_transport->Listen(port)) {
        m_transport->Shutdown();
        m_transport.reset();
        return false;
    }

    m_role = listenServer ? NetworkRole::ListenServer : NetworkRole::Server;
    m_serverState.Init(tickRate);
    m_replication.Init();
    m_snapshotInterval = 1.0f / snapshotRate;

    if (listenServer) {
        // Create a local player for the host
        m_clientState.Init(tickRate);
        m_localPlayerId = m_serverState.AddPlayer(InvalidConnection);
    }

    return true;
}

bool NetworkManager::JoinGame(const std::string& address, uint16_t port) {
    if (m_role != NetworkRole::Offline) LeaveGame();

    m_transport = std::make_unique<WinsockTransport>();
    if (!m_transport->Init()) return false;

    m_transport->SetCallbacks({
        [this](ConnectionId id) { OnConnected(id); },
        [this](ConnectionId id) { OnDisconnected(id); },
        [this](ConnectionId id, const uint8_t* data, int size) { OnDataReceived(id, data, size); }
    });

    if (!m_transport->Connect(address, port)) {
        m_transport->Shutdown();
        m_transport.reset();
        return false;
    }

    m_role = NetworkRole::Client;
    m_clientState.Init(tickRate);

    return true;
}

void NetworkManager::LeaveGame() {
    if (m_role == NetworkRole::Offline) return;

    if (m_transport) {
        m_transport->DisconnectAll();
        m_transport->Shutdown();
        m_transport.reset();
    }

    m_serverState.Shutdown();
    m_clientState.Shutdown();
    m_replication.Shutdown();
    m_role = NetworkRole::Offline;
    m_localPlayerId = 0;
    m_pendingLevelSends.clear();
    m_levelTotalChunks = 0;
    m_levelTotalEntities = 0;
    m_levelEntities.clear();
    m_levelChunksReceived.clear();
    m_clientLevelReceived = false;
}

// ============================================================
// Per-Frame Update
// ============================================================

void NetworkManager::Update(float dt, Scene& scene) {
    if (m_role == NetworkRole::Offline) return;

    m_serverTime += dt;
    m_scene = &scene;

    // Poll transport for incoming messages
    if (m_transport) m_transport->Poll();

    // Server: send level data to newly connected clients
    if (IsServer() && !m_pendingLevelSends.empty()) {
        for (ConnectionId cid : m_pendingLevelSends) {
            ServerSendLevelData(cid, scene);
        }
        m_pendingLevelSends.clear();
    }

    // Server: run authoritative simulation
    if (IsServer()) {
        m_serverState.Update(dt);

        // Sync scene entities to replication
        SyncReplicationFromScene(scene);

        // Send snapshots at snapshot rate
        ServerSendSnapshots(dt);

        // Broadcast queued gameplay events to all clients
        ServerBroadcastEvents();
    }

    // Client: accumulate tick timer and interpolate
    if (IsClient()) {
        m_clientState.AccumulateTick(dt);
        m_clientState.InterpolateRemotePlayers(m_serverTime);
    }

    // Bandwidth tracking
    m_bandwidthTimer += dt;
    if (m_bandwidthTimer >= 1.0f) {
        uint64_t totalSent = 0, totalRecv = 0;
        if (m_transport) {
            for (auto cid : m_transport->GetConnections()) {
                auto info = m_transport->GetConnectionInfo(cid);
                totalSent += info.bytesSent;
                totalRecv += info.bytesRecv;
            }
        }
        m_bandwidthOutKBps = static_cast<float>(totalSent - m_lastBytesSent) / 1024.0f / m_bandwidthTimer;
        m_bandwidthInKBps = static_cast<float>(totalRecv - m_lastBytesRecv) / 1024.0f / m_bandwidthTimer;
        m_lastBytesSent = totalSent;
        m_lastBytesRecv = totalRecv;
        m_bandwidthTimer = 0.0f;
    }
}

// ============================================================
// Input Sending
// ============================================================

void NetworkManager::SendInput(const InputCommand& cmd) {
    if (m_role == NetworkRole::ListenServer) {
        // Local player on listen server — queue directly
        m_serverState.QueueInput(InvalidConnection, cmd);
        return;
    }

    if (m_role != NetworkRole::Client || !m_transport) return;

    // Serialise input command
    BitStream bs;
    PacketHeader header;
    header.type = PacketType::ClientInput;
    header.sequence = 0;
    header.ack = 0;
    header.ackBits = 0;
    header.Write(bs);
    cmd.Write(bs);

    // Send to server (first connection)
    auto connections = m_transport->GetConnections();
    if (!connections.empty()) {
        m_transport->Send(connections[0], bs.GetData(), bs.GetSizeBytes(), SendFlags::Unreliable);
    }
}

// ============================================================
// Connection Callbacks
// ============================================================

void NetworkManager::OnConnected(ConnectionId id) {
    if (IsServer()) {
        // New player joined
        uint8_t playerId = m_serverState.AddPlayer(id);
        m_replication.AddConnection(id);

        // Send player ID assignment
        BitStream bs;
        PacketHeader header;
        header.type = PacketType::ConnectionAccept;
        header.sequence = 0;
        header.ack = 0;
        header.ackBits = 0;
        header.Write(bs);
        bs.WriteUInt8(playerId);
        m_transport->Send(id, bs.GetData(), bs.GetSizeBytes(),
                          SendFlags::Reliable | SendFlags::NoDelay);

        // Queue level data send (processed next Update when scene is available)
        m_pendingLevelSends.push_back(id);
    }

    if (m_role == NetworkRole::Client) {
        // Connected to server — wait for player ID assignment
    }
}

void NetworkManager::OnDisconnected(ConnectionId id) {
    if (IsServer()) {
        m_serverState.RemovePlayer(id);
        m_replication.RemoveConnection(id);
    }

    if (m_role == NetworkRole::Client) {
        // Lost connection to server
        m_localPlayerId = 0;
    }
}

void NetworkManager::OnDataReceived(ConnectionId id, const uint8_t* data, int size) {
    if (size < 1) return;

    BitStream bs(data, size);
    PacketHeader header;
    header.Read(bs);

    switch (header.type) {
        case PacketType::ClientInput:
            if (IsServer()) ServerHandleInput(id, bs);
            break;

        case PacketType::ConnectionAccept:
            if (m_role == NetworkRole::Client) {
                m_localPlayerId = bs.ReadUInt8();
            }
            break;

        case PacketType::ServerSnapshot:
            if (IsClient()) ClientHandleSnapshot(bs);
            break;

        case PacketType::ServerEvent:
            if (IsClient()) ClientHandleEvent(bs);
            break;

        case PacketType::ReplicationFull:
        case PacketType::ReplicationDelta: {
            if (IsClient()) {
                auto states = ReplicationManager::ParseReplicationPacket(bs);
                for (const auto& s : states)
                    m_pendingReplicationStates.push_back(s);
            }
            break;
        }

        case PacketType::LevelData:
            if (m_role == NetworkRole::Client) ClientHandleLevelData(bs);
            break;

        default:
            break;
    }
}

// ============================================================
// Server-Side Handlers
// ============================================================

void NetworkManager::ServerHandleInput(ConnectionId id, BitStream& bs) {
    InputCommand cmd;
    cmd.Read(bs);
    m_serverState.QueueInput(id, cmd);
}

void NetworkManager::ServerSendSnapshots(float dt) {
    m_snapshotTimer += dt;
    if (m_snapshotTimer < m_snapshotInterval) return;
    m_snapshotTimer -= m_snapshotInterval;

    if (!m_transport) return;

    // Build player snapshots
    auto playerSnapshots = m_serverState.BuildPlayerSnapshots();

    // Serialise
    BitStream bs;
    PacketHeader header;
    header.type = PacketType::ServerSnapshot;
    header.sequence = 0;
    header.ack = 0;
    header.ackBits = 0;
    header.Write(bs);

    bs.WriteUInt32(m_serverState.GetTick());
    bs.WriteUInt8(static_cast<uint8_t>(playerSnapshots.size()));
    for (const auto& snap : playerSnapshots) {
        snap.Write(bs);
    }

    // Broadcast to all clients
    m_transport->Broadcast(bs.GetData(), bs.GetSizeBytes(), SendFlags::Unreliable);

    // Also send replication packets per-connection
    auto connections = m_transport->GetConnections();

    // Build client positions for interest management
    std::unordered_map<uint32_t, XMFLOAT3> clientPositions;
    for (auto cid : connections) {
        auto* player = m_serverState.GetPlayerByConnection(cid);
        if (player) {
            clientPositions[cid] = player->position;
        }
    }
    m_replication.UpdateInterestSets(clientPositions, m_serverTime);

    for (auto cid : connections) {
        BitStream repPacket = m_replication.BuildReplicationPacket(cid, m_serverTime);
        if (repPacket.GetSizeBytes() > 0) {
            // Wrap in ReplicationDelta packet
            BitStream wrapped;
            PacketHeader repHeader;
            repHeader.type = PacketType::ReplicationDelta;
            repHeader.sequence = 0;
            repHeader.ack = 0;
            repHeader.ackBits = 0;
            repHeader.Write(wrapped);
            wrapped.WriteBytes(repPacket.GetData(), repPacket.GetSizeBytes());

            m_transport->Send(cid, wrapped.GetData(), wrapped.GetSizeBytes(), SendFlags::Unreliable);
        }
    }
}

// ============================================================
// Client-Side Handlers
// ============================================================

void NetworkManager::ClientHandleSnapshot(BitStream& bs) {
    uint32_t serverTick = bs.ReadUInt32();
    uint8_t playerCount = bs.ReadUInt8();

    for (int i = 0; i < playerCount; i++) {
        PlayerSnapshot snap;
        snap.Read(bs);

        if (snap.playerId == m_localPlayerId) {
            // Reconcile local player
            XMFLOAT3 serverPos = snap.position;
            XMFLOAT3 serverVel = { 0, 0, 0 }; // Velocity not in snapshot, use zero
            m_clientState.Reconcile(serverTick, serverPos, serverVel, true);
        } else {
            // Remote player — add to interpolation buffer
            m_clientState.AddRemoteSnapshot(snap.playerId, m_serverTime, snap);
        }

        if (bs.IsOverflow()) break;
    }
}

void NetworkManager::ClientHandleEvent(BitStream& bs) {
    ServerEvent evt;
    evt.Read(bs);
    m_clientEventQueue.push_back(evt);
}

// ============================================================
// Level Data Transfer
// ============================================================

void NetworkManager::ServerSendLevelData(ConnectionId id, Scene& scene) {
    if (!m_transport) return;

    int entityCount = scene.GetEntityCount();
    static constexpr int ENTITIES_PER_CHUNK = 16;
    int totalChunks = (entityCount + ENTITIES_PER_CHUNK - 1) / ENTITIES_PER_CHUNK;
    if (totalChunks == 0) totalChunks = 1; // Send at least one chunk (empty level)

    LOG_INFO("Net: Sending level data to client %u (%d entities, %d chunks)",
             id, entityCount, totalChunks);

    for (int chunk = 0; chunk < totalChunks; chunk++) {
        BitStream bs;
        PacketHeader header;
        header.type = PacketType::LevelData;
        header.sequence = 0;
        header.ack = 0;
        header.ackBits = 0;
        header.Write(bs);

        bs.WriteUInt16(static_cast<uint16_t>(chunk));
        bs.WriteUInt16(static_cast<uint16_t>(totalChunks));
        bs.WriteUInt16(static_cast<uint16_t>(entityCount));

        int startIdx = chunk * ENTITIES_PER_CHUNK;
        int endIdx = startIdx + ENTITIES_PER_CHUNK;
        if (endIdx > entityCount) endIdx = entityCount;
        bs.WriteUInt16(static_cast<uint16_t>(endIdx - startIdx));

        for (int i = startIdx; i < endIdx; i++) {
            const Entity& e = scene.GetEntity(i);
            NetEntityData ned;
            ned.name        = e.name;
            ned.meshType    = static_cast<uint8_t>(e.meshType);
            ned.meshName    = e.meshName;
            ned.textureName = e.textureName;
            ned.position    = { e.position[0], e.position[1], e.position[2] };
            ned.rotation    = { e.rotation[0], e.rotation[1], e.rotation[2] };
            ned.scale       = { e.scale[0], e.scale[1], e.scale[2] };
            ned.color[0] = e.color[0]; ned.color[1] = e.color[1];
            ned.color[2] = e.color[2]; ned.color[3] = e.color[3];
            ned.visible          = e.visible;
            ned.castShadow       = e.castShadow;
            ned.isLadder         = e.isLadder;
            ned.hasGravity       = e.hasGravity;
            ned.destructible     = e.destructible;
            ned.voxelDestruction = e.voxelDestruction;
            ned.smokeOnDamage    = e.smokeOnDamage;
            ned.noCollision      = e.noCollision;
            ned.materialType     = static_cast<uint8_t>(e.materialType);
            ned.maxHealth        = e.maxHealth;
            ned.debrisCount      = static_cast<uint8_t>(e.debrisCount);
            ned.debrisScale      = e.debrisScale;
            ned.supportedBy      = e.supportedBy;
            ned.breakPieceCount  = static_cast<uint8_t>(e.breakPieceCount);
            ned.voxelRes         = static_cast<uint8_t>(e.voxelRes);
            ned.despawnTimer     = e.despawnTimer;
            ned.pickupType       = static_cast<uint8_t>(e.pickupType);
            ned.pickupAmount     = e.pickupAmount;
            ned.pickupRadius     = e.pickupRadius;
            ned.pickupBobSpeed   = e.pickupBobSpeed;
            ned.pickupBobHeight  = e.pickupBobHeight;
            ned.pickupSpinSpeed  = e.pickupSpinSpeed;
            ned.pickupRespawnTime = e.pickupRespawnTime;
            ned.damageFlashDuration = e.damageFlashDuration;
            ned.damageFlashColor[0] = e.damageFlashColor[0];
            ned.damageFlashColor[1] = e.damageFlashColor[1];
            ned.damageFlashColor[2] = e.damageFlashColor[2];
            ned.interactType     = static_cast<uint8_t>(e.interactType);
            ned.interactRange    = e.interactRange;
            ned.interactPrompt   = e.interactPrompt;
            ned.interactTarget   = e.interactTarget;
            ned.doorOpenAngle    = e.doorOpenAngle;
            ned.doorSlideDist    = e.doorSlideDist;
            ned.doorSpeed        = e.doorSpeed;
            ned.cullCategory     = static_cast<uint8_t>(e.cullCategory);
            ned.Write(bs);
        }

        m_transport->Send(id, bs.GetData(), bs.GetSizeBytes(),
                          SendFlags::Reliable | SendFlags::NoDelay);
    }
}

void NetworkManager::ClientHandleLevelData(BitStream& bs) {
    uint16_t chunkIndex   = bs.ReadUInt16();
    uint16_t totalChunks  = bs.ReadUInt16();
    uint16_t totalEntities = bs.ReadUInt16();
    uint16_t entitiesInChunk = bs.ReadUInt16();

    // Detect new transfer: first time, different chunk count, or previous transfer already complete
    bool previousComplete = true;
    for (bool r : m_levelChunksReceived) {
        if (!r) { previousComplete = false; break; }
    }
    if (m_levelTotalChunks == 0 || totalChunks != m_levelTotalChunks || previousComplete) {
        m_levelTotalChunks   = totalChunks;
        m_levelTotalEntities = totalEntities;
        m_levelEntities.clear();
        m_levelEntities.resize(totalEntities);
        m_levelChunksReceived.clear();
        m_levelChunksReceived.resize(totalChunks, false);
    }

    if (chunkIndex >= totalChunks) return; // Invalid
    if (m_levelChunksReceived[chunkIndex]) return; // Duplicate within same transfer

    // Read entities for this chunk
    static constexpr int ENTITIES_PER_CHUNK = 16;
    int startIdx = chunkIndex * ENTITIES_PER_CHUNK;
    for (uint16_t i = 0; i < entitiesInChunk; i++) {
        int entityIdx = startIdx + i;
        if (entityIdx < static_cast<int>(m_levelEntities.size())) {
            m_levelEntities[entityIdx].Read(bs);
        }
        if (bs.IsOverflow()) break;
    }

    m_levelChunksReceived[chunkIndex] = true;

    // Check if all chunks received
    bool allReceived = true;
    for (bool r : m_levelChunksReceived) {
        if (!r) { allReceived = false; break; }
    }

    if (allReceived) {
        LOG_INFO("Net: Received full level data (%d entities, %d chunks)",
                 m_levelTotalEntities, m_levelTotalChunks);
        m_clientLevelReceived = true;
    }
}

bool NetworkManager::ConsumeClientLevelReceived() {
    if (!m_clientLevelReceived) return false;
    m_clientLevelReceived = false;
    return true;
}

void NetworkManager::BroadcastLevelData(Scene& scene) {
    if (!IsServer() || !m_transport) return;
    auto connections = m_transport->GetConnections();
    if (connections.empty()) {
        LOG_INFO("Net: No clients connected — nothing to push");
        return;
    }
    for (auto cid : connections) {
        ServerSendLevelData(cid, scene);
    }
    LOG_INFO("Net: Pushed level to %d client(s)", static_cast<int>(connections.size()));
}

// ============================================================
// Event System
// ============================================================

void NetworkManager::QueueServerEvent(const ServerEvent& evt) {
    m_serverEventQueue.push_back(evt);
}

std::vector<ServerEvent> NetworkManager::ConsumeClientEvents() {
    std::vector<ServerEvent> out;
    out.swap(m_clientEventQueue);
    return out;
}

std::vector<ReplicatedEntityState> NetworkManager::ConsumeReplicationStates() {
    std::vector<ReplicatedEntityState> out;
    out.swap(m_pendingReplicationStates);
    return out;
}

void NetworkManager::ServerBroadcastEvents() {
    if (m_serverEventQueue.empty() || !m_transport) return;

    for (const auto& evt : m_serverEventQueue) {
        BitStream bs;
        PacketHeader header;
        header.type = PacketType::ServerEvent;
        header.sequence = 0;
        header.ack = 0;
        header.ackBits = 0;
        header.Write(bs);
        evt.Write(bs);

        // Reliable for state-changing events, unreliable for cosmetic
        SendFlags flags = SendFlags::Unreliable;
        switch (evt.eventType) {
            case ServerEventType::DoorToggled:
            case ServerEventType::SwitchToggled:
            case ServerEventType::PickupCollected:
            case ServerEventType::EntityDestroyed:
            case ServerEventType::EntityRemoved:
            case ServerEventType::VoxelDestroyed:
            case ServerEventType::LootContainerOpened:
                flags = SendFlags::Reliable;
                break;
            default:
                break;
        }

        m_transport->Broadcast(bs.GetData(), bs.GetSizeBytes(), flags);
    }
    m_serverEventQueue.clear();
}

// ============================================================
// Scene Sync
// ============================================================

void NetworkManager::SyncReplicationFromScene(Scene& scene) {
    for (int i = 0; i < scene.GetEntityCount(); i++) {
        const Entity& e = scene.GetEntity(i);
        NetEntityId netId = m_replication.GetNetId(i);
        if (netId == InvalidNetEntity) {
            netId = m_replication.RegisterEntity(i);
        }

        ReplicatedEntityState state;
        state.netId = netId;
        state.position = { e.position[0], e.position[1], e.position[2] };
        state.rotation = { e.rotation[0], e.rotation[1], e.rotation[2] };
        state.scale = { e.scale[0], e.scale[1], e.scale[2] };
        state.health = e.health;
        state.meshType = static_cast<uint8_t>(e.meshType);
        state.materialType = static_cast<uint8_t>(e.materialType);
        state.visible = e.visible;
        state.destructible = e.destructible;

        // Interaction / destruction state
        state.doorOpen = e.doorOpen;
        state.switchState = e.switchState;
        state.pickupCollected = e.pickupCollected;
        state.voxelDestruction = e.voxelDestruction;
        state.voxelRes = static_cast<uint8_t>(e.voxelRes);
        if (e.voxelDestruction) {
            for (int m = 0; m < 8; m++)
                state.voxelMask[m] = e.voxelMask[m];
        }

        m_replication.UpdateEntityState(netId, state);
    }
}

// ============================================================
// Stats
// ============================================================

NetworkStats NetworkManager::GetStats() const {
    NetworkStats stats;
    if (m_transport) {
        stats.connectedPlayers = m_transport->GetConnectionCount();
        auto conns = m_transport->GetConnections();
        float totalRtt = 0.0f;
        for (auto cid : conns) {
            auto info = m_transport->GetConnectionInfo(cid);
            totalRtt += info.rttMs;
        }
        if (!conns.empty()) stats.avgRttMs = totalRtt / static_cast<float>(conns.size());
    }
    stats.bandwidthInKBps = m_bandwidthInKBps;
    stats.bandwidthOutKBps = m_bandwidthOutKBps;
    if (IsServer()) {
        stats.serverTick = m_serverState.GetTick();
        stats.tickRate = m_serverState.GetTickRate();
    }
    return stats;
}

} // namespace WT
