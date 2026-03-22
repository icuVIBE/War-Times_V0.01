#pragma once

#include "Transport/Transport.h"
#include "Server/ServerState.h"
#include "Client/ClientState.h"
#include "Packets/PacketDefs.h"
#include "Packets/BitStream.h"
#include "Replication/ReplicationManager.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace WT {

class Scene;

// ============================================================
// Network Role
// ============================================================

enum class NetworkRole : uint8_t {
    Offline = 0,    // Singleplayer (no networking)
    Server,         // Hosting — authoritative simulation
    Client,         // Connected to a server
    ListenServer    // Hosting + playing (server + local client)
};

inline const char* NetworkRoleName(NetworkRole role) {
    switch (role) {
        case NetworkRole::Offline:      return "Offline";
        case NetworkRole::Server:       return "Server";
        case NetworkRole::Client:       return "Client";
        case NetworkRole::ListenServer: return "Listen Server";
        default: return "Unknown";
    }
}

// ============================================================
// Network Stats — aggregate statistics
// ============================================================

struct NetworkStats {
    int   connectedPlayers = 0;
    float avgRttMs = 0.0f;
    float bandwidthInKBps = 0.0f;
    float bandwidthOutKBps = 0.0f;
    int   packetsIn = 0;
    int   packetsOut = 0;
    float packetLoss = 0.0f;
    uint32_t serverTick = 0;
    float tickRate = 0.0f;
};

// ============================================================
// Network Manager — top-level orchestrator
// ============================================================
// Wires together Transport, ServerState, ClientState, and
// ReplicationManager into a coherent multiplayer system.
// Provides a simple high-level API used by the game Application.

class NetworkManager {
public:
    void Init();
    void Shutdown();

    // ---- Session Management ----

    // Host a game (creates server, optionally with local player).
    bool HostGame(uint16_t port, bool listenServer = true);

    // Join a remote game as a client.
    bool JoinGame(const std::string& address, uint16_t port);

    // Leave the current session (disconnect or stop hosting).
    void LeaveGame();

    // Current role.
    NetworkRole GetRole() const { return m_role; }
    bool IsServer() const { return m_role == NetworkRole::Server || m_role == NetworkRole::ListenServer; }
    bool IsClient() const { return m_role == NetworkRole::Client || m_role == NetworkRole::ListenServer; }
    bool IsOnline() const { return m_role != NetworkRole::Offline; }

    // ---- Per-Frame Update ----

    // Call once per frame. Polls transport, runs server ticks,
    // handles replication, processes incoming packets.
    void Update(float dt, Scene& scene);

    // ---- Client Input ----

    // Send local player input to the server (client or listen-server).
    void SendInput(const InputCommand& cmd);

    // ---- Accessors ----

    ServerState& GetServerState() { return m_serverState; }
    ClientState& GetClientState() { return m_clientState; }
    ReplicationManager& GetReplication() { return m_replication; }
    Transport& GetTransport() { return *m_transport; }
    NetworkStats GetStats() const;

    // Local player ID (assigned by server).
    uint8_t GetLocalPlayerId() const { return m_localPlayerId; }

    // Check if client received a new level this frame (consumed on read).
    bool ConsumeClientLevelReceived();

    // Server: push current level to all connected clients (hot-swap).
    void BroadcastLevelData(Scene& scene);

    // Access received level entity data (valid after ConsumeClientLevelReceived).
    const std::vector<NetEntityData>& GetReceivedLevelEntities() const { return m_levelEntities; }

    // ---- Event System ----

    // Server: queue a gameplay event to broadcast to all clients.
    void QueueServerEvent(const ServerEvent& evt);

    // Client: consume all pending events received from the server.
    std::vector<ServerEvent> ConsumeClientEvents();

    // Client: consume pending entity replication states.
    std::vector<ReplicatedEntityState> ConsumeReplicationStates();

    // Settings
    float tickRate = 60.0f;
    float snapshotRate = 60.0f;   // How often server sends snapshots (Hz)
    uint16_t defaultPort = 27015;
    std::string serverName = "War Times Server";

private:
    // ---- Internal Handlers ----
    void OnConnected(ConnectionId id);
    void OnDisconnected(ConnectionId id);
    void OnDataReceived(ConnectionId id, const uint8_t* data, int size);

    // Server: process a client input packet.
    void ServerHandleInput(ConnectionId id, BitStream& bs);

    // Server: broadcast world snapshots at snapshot rate.
    void ServerSendSnapshots(float dt);

    // Client: process server snapshot/delta.
    void ClientHandleSnapshot(BitStream& bs);

    // Client: process server event.
    void ClientHandleEvent(BitStream& bs);

    // Client: process level data chunk.
    void ClientHandleLevelData(BitStream& bs);

    // Server: send full level to a specific client (chunked reliable).
    void ServerSendLevelData(ConnectionId id, Scene& scene);

    // Sync replicated entity states from scene.
    void SyncReplicationFromScene(Scene& scene);

    // Server: flush all queued events to clients.
    void ServerBroadcastEvents();

    NetworkRole m_role = NetworkRole::Offline;

    std::unique_ptr<Transport> m_transport;
    ServerState   m_serverState;
    ClientState   m_clientState;
    ReplicationManager m_replication;

    uint8_t m_localPlayerId = 0;
    float   m_snapshotTimer = 0.0f;
    float   m_snapshotInterval = 1.0f / 20.0f;
    float   m_serverTime = 0.0f;

    // Scene pointer — set during Update(), used by deferred sends
    Scene*  m_scene = nullptr;

    // Pending level sends (connection IDs queued during OnConnected)
    std::vector<ConnectionId> m_pendingLevelSends;

    // Client-side level transfer state
    uint16_t m_levelTotalChunks = 0;        // Expected total chunks
    uint16_t m_levelTotalEntities = 0;      // Expected total entities
    std::vector<NetEntityData> m_levelEntities; // Accumulated entity data
    std::vector<bool> m_levelChunksReceived;    // Which chunks arrived
    bool m_clientLevelReceived = false;     // Flag: full level available

    // Bandwidth tracking
    uint64_t m_lastBytesSent = 0;
    uint64_t m_lastBytesRecv = 0;
    float    m_bandwidthTimer = 0.0f;
    float    m_bandwidthInKBps = 0.0f;
    float    m_bandwidthOutKBps = 0.0f;

    // Event queues
    std::vector<ServerEvent> m_serverEventQueue;  // Server: pending events to broadcast
    std::vector<ServerEvent> m_clientEventQueue;   // Client: received events for Application

    // Client-side replication receive buffer
    std::vector<ReplicatedEntityState> m_pendingReplicationStates;
};

} // namespace WT
