#pragma once

#include "../Packets/PacketDefs.h"
#include "../Transport/Transport.h"
#include <vector>
#include <unordered_map>
#include <deque>
#include <DirectXMath.h>

namespace WT {

// ============================================================
// Server Player — authoritative state for a connected player
// ============================================================

struct ServerPlayer {
    ConnectionId connectionId = InvalidConnection;
    uint8_t      playerId = 0;

    // Authoritative state
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    float    yaw = 0.0f;
    float    pitch = 0.0f;
    float    health = 100.0f;
    float    maxHealth = 100.0f;
    uint8_t  weaponSlot = 0;
    bool     alive = true;
    bool     onGround = false;

    // Input processing
    InputCommand lastInput;
    uint32_t     lastProcessedTick = 0;

    // Lag compensation — history of positions for rewinding
    struct PositionSample {
        uint32_t tick;
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 halfExtents; // Collision box size
    };
    std::deque<PositionSample> positionHistory;
    static constexpr int kMaxHistorySamples = 64; // ~1 second at 60 tick

    // Stats
    int kills = 0;
    int deaths = 0;
    float respawnTimer = 0.0f;
};

// ============================================================
// Server State — authoritative game simulation
// ============================================================

class ServerState {
public:
    void Init(float tickRate = 60.0f);
    void Shutdown();

    // ---- Tick Management ----

    // Advance simulation by dt. Internally accumulates time and runs
    // fixed-rate ticks. Returns number of ticks that ran.
    int Update(float dt);

    // Current server tick.
    uint32_t GetTick() const { return m_currentTick; }

    // Server tick rate (ticks per second).
    float GetTickRate() const { return m_tickRate; }

    // Time per tick.
    float GetTickInterval() const { return m_tickInterval; }

    // ---- Player Management ----

    // Register a new player from a connection.
    uint8_t AddPlayer(ConnectionId connectionId);

    // Remove a player (disconnected).
    void RemovePlayer(ConnectionId connectionId);

    // Get player by connection ID.
    ServerPlayer* GetPlayerByConnection(ConnectionId id);
    const ServerPlayer* GetPlayerByConnection(ConnectionId id) const;

    // Get player by player ID.
    ServerPlayer* GetPlayer(uint8_t playerId);

    // Get all players.
    const std::vector<ServerPlayer>& GetPlayers() const { return m_players; }

    // ---- Input Processing ----

    // Queue an input command from a client for processing on next tick.
    void QueueInput(ConnectionId connectionId, const InputCommand& cmd);

    // ---- Lag Compensation ----

    // Rewind all player positions to the state at a given tick.
    // Call before doing hit detection, then call RestorePositions() after.
    void RewindToTick(uint32_t tick);

    // Restore current positions after rewound hit detection.
    void RestorePositions();

    // ---- Snapshot Generation ----

    // Build a snapshot of all player states for replication.
    std::vector<PlayerSnapshot> BuildPlayerSnapshots() const;

    // ---- Game Logic ----

    // Apply damage to a player. Returns true if killed.
    bool DamagePlayer(uint8_t targetId, float damage, uint8_t sourceId);

    // Respawn a dead player at a spawn point.
    void RespawnPlayer(uint8_t playerId, const DirectX::XMFLOAT3& spawnPos);

    // Settings
    float respawnTime = 5.0f;
    float moveSpeed = 6.0f;
    float sprintMult = 1.5f;
    float jumpForce = 7.0f;
    float gravity = 18.0f;
    float groundY = 0.0f;

private:
    // Run a single simulation tick.
    void Tick();

    // Process queued inputs for all players.
    void ProcessInputs();

    // Simulate a single player's movement from their input.
    void SimulatePlayerMovement(ServerPlayer& player, const InputCommand& input, float dt);

    // Update respawn timers.
    void UpdateRespawns(float dt);

    // Record position history for lag compensation.
    void RecordPositionHistory();

    float m_tickRate = 60.0f;
    float m_tickInterval = 1.0f / 60.0f;
    float m_accumulator = 0.0f;
    uint32_t m_currentTick = 0;

    std::vector<ServerPlayer> m_players;
    uint8_t m_nextPlayerId = 1;

    // Input queue: inputs received from clients, to be consumed on next tick
    struct QueuedInput {
        ConnectionId connectionId;
        InputCommand command;
    };
    std::vector<QueuedInput> m_inputQueue;

    // Saved positions for lag compensation restore
    struct SavedPosition {
        uint8_t playerId;
        DirectX::XMFLOAT3 position;
    };
    std::vector<SavedPosition> m_savedPositions;
};

} // namespace WT
