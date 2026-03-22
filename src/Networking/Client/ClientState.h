#pragma once

#include "../Packets/PacketDefs.h"
#include "../Transport/Transport.h"
#include <vector>
#include <deque>
#include <DirectXMath.h>

namespace WT {

// ============================================================
// Client Predicted State — local player state used for prediction
// ============================================================

struct ClientPredictedState {
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };
    float    yaw = 0.0f;
    float    pitch = 0.0f;
    bool     onGround = false;
};

// ============================================================
// Remote Player — interpolated state of other players
// ============================================================

struct RemotePlayer {
    uint8_t  playerId = 0;

    // Interpolation buffer: we keep several snapshots and
    // render at a time slightly in the past for smooth display.
    struct Snapshot {
        float    serverTime;   // Server timestamp
        DirectX::XMFLOAT3 position;
        float    yaw;
        float    pitch;
        float    health;
        uint8_t  weaponSlot;
        uint8_t  animState;
    };
    std::deque<Snapshot> snapshots;
    static constexpr int kMaxSnapshots = 32;

    // Current interpolated visual state
    DirectX::XMFLOAT3 displayPosition = { 0, 0, 0 };
    float displayYaw = 0.0f;
    float displayPitch = 0.0f;
    float displayHealth = 100.0f;
};

// ============================================================
// Client State — client-side prediction and reconciliation
// ============================================================

class ClientState {
public:
    void Init(float tickRate = 60.0f);
    void Shutdown();

    // ---- Input Buffering ----

    // Sample current input and create a command for the current tick.
    InputCommand SampleInput(float moveX, float moveZ, float yaw, float pitch,
                             bool jump, bool crouch, bool sprint,
                             bool fire, bool reload, bool interact,
                             uint8_t weaponSlot);

    // Get the latest input command (for sending to server).
    const InputCommand& GetLatestInput() const { return m_pendingInputs.empty() ? m_emptyInput : m_pendingInputs.back(); }

    // ---- Client-Side Prediction ----

    // Predict the local player's state by applying the input command immediately.
    void PredictInput(const InputCommand& cmd, float dt);

    // Get the predicted state.
    const ClientPredictedState& GetPredictedState() const { return m_predicted; }

    // ---- Server Reconciliation ----

    // Receive an authoritative position from the server. Replays unacknowledged
    // inputs on top of the server state to reconcile prediction errors.
    void Reconcile(uint32_t serverTick, const DirectX::XMFLOAT3& serverPos,
                   const DirectX::XMFLOAT3& serverVel, bool serverOnGround);

    // ---- Remote Player Interpolation ----

    // Add a snapshot for a remote player (from server replication).
    void AddRemoteSnapshot(uint8_t playerId, float serverTime,
                            const PlayerSnapshot& snapshot);

    // Remove a remote player.
    void RemoveRemotePlayer(uint8_t playerId);

    // Interpolate all remote players to the current render time.
    void InterpolateRemotePlayers(float renderTime);

    // Get remote players for rendering.
    const std::vector<RemotePlayer>& GetRemotePlayers() const { return m_remotePlayers; }

    // ---- Server Correction ----

    // Get the authoritative server position target (blended toward each frame).
    DirectX::XMFLOAT3 GetServerTarget() const { return m_serverTarget; }
    bool HasServerTarget() const { return m_hasServerTarget; }

    // Compute a smoothly corrected position given current local position.
    // Call every frame; blends local toward server target continuously.
    DirectX::XMFLOAT3 SmoothCorrect(const DirectX::XMFLOAT3& localPos, float dt);

    // ---- Tick Management ----

    // Accumulate frame time; returns true each time a tick is due (fixed-rate).
    bool AccumulateTick(float dt);
    bool TickReady() const { return m_tickReady; }
    uint32_t GetTick() const { return m_clientTick; }
    float GetTickInterval() const { return m_tickInterval; }

    // Settings
    float interpolationDelay = 0.1f;  // 100ms interpolation buffer
    float moveSpeed = 6.0f;
    float sprintMult = 1.5f;
    float gravity = 18.0f;
    float jumpForce = 7.0f;
    float groundY = 0.0f;
    float reconcileThresholdSq = 0.01f; // Squared distance to trigger reconciliation

private:
    // Replay an input command for prediction (shared by Predict and Reconcile).
    void ApplyInput(ClientPredictedState& state, const InputCommand& cmd, float dt) const;

    float m_tickRate = 60.0f;
    float m_tickInterval = 1.0f / 60.0f;
    float m_tickAccumulator = 0.0f;
    uint32_t m_clientTick = 0;
    bool m_tickReady = false;  // true when a tick fired this frame

    // Predicted local player state
    ClientPredictedState m_predicted;

    // Input buffer: unacknowledged inputs sent to server, kept for reconciliation
    std::deque<InputCommand> m_pendingInputs;
    static constexpr int kMaxPendingInputs = 128;

    // Remote players
    std::vector<RemotePlayer> m_remotePlayers;

    // Server correction target (persistent — updated by each snapshot, blended every frame)
    DirectX::XMFLOAT3 m_serverTarget = { 0, 0, 0 };
    bool m_hasServerTarget = false;

    // Empty input for safe return
    InputCommand m_emptyInput;
};

} // namespace WT
