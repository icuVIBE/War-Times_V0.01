#include "ClientState.h"
#include <algorithm>
#include <cmath>

namespace WT {

using namespace DirectX;

// ============================================================
// Init / Shutdown
// ============================================================

void ClientState::Init(float tickRate) {
    m_tickRate = tickRate;
    m_tickInterval = 1.0f / tickRate;
    m_tickAccumulator = 0.0f;
    m_clientTick = 0;
    m_tickReady = false;
    m_predicted = {};
    m_pendingInputs.clear();
    m_remotePlayers.clear();
}

void ClientState::Shutdown() {
    m_pendingInputs.clear();
    m_remotePlayers.clear();
}

// ============================================================
// Tick Management
// ============================================================

bool ClientState::AccumulateTick(float dt) {
    m_tickAccumulator += dt;
    m_tickReady = false;
    if (m_tickAccumulator >= m_tickInterval) {
        m_tickAccumulator -= m_tickInterval;
        // Prevent spiral of death
        if (m_tickAccumulator > m_tickInterval * 3.0f)
            m_tickAccumulator = 0.0f;
        m_clientTick++;
        m_tickReady = true;
    }
    return m_tickReady;
}

// ============================================================
// Input Buffering
// ============================================================

InputCommand ClientState::SampleInput(float moveX, float moveZ, float yaw, float pitch,
                                       bool jump, bool crouch, bool sprint,
                                       bool fire, bool reload, bool interact,
                                       uint8_t weaponSlot) {
    InputCommand cmd;
    cmd.tick = m_clientTick;
    cmd.moveX = moveX;
    cmd.moveZ = moveZ;
    cmd.yaw = yaw;
    cmd.pitch = pitch;
    cmd.jump = jump;
    cmd.crouch = crouch;
    cmd.sprint = sprint;
    cmd.fire = fire;
    cmd.reload = reload;
    cmd.interact = interact;
    cmd.weaponSlot = weaponSlot;

    // Store for reconciliation
    m_pendingInputs.push_back(cmd);
    while (static_cast<int>(m_pendingInputs.size()) > kMaxPendingInputs) {
        m_pendingInputs.pop_front();
    }

    return cmd;
}

// ============================================================
// Client-Side Prediction
// ============================================================

void ClientState::ApplyInput(ClientPredictedState& state, const InputCommand& cmd, float dt) const {
    // Look direction
    state.yaw = cmd.yaw;
    state.pitch = cmd.pitch;

    // Movement — matching Character::UpdateMovement & ServerState::SimulatePlayerMovement
    float yawRad = XMConvertToRadians(cmd.yaw);
    float sinY = sinf(yawRad);
    float cosY = cosf(yawRad);

    float dirX = 0.0f, dirZ = 0.0f;
    if (cmd.moveZ >  0.5f) { dirX += sinY; dirZ += cosY; }
    if (cmd.moveZ < -0.5f) { dirX -= sinY; dirZ -= cosY; }
    if (cmd.moveX < -0.5f) { dirX -= cosY; dirZ += sinY; }
    if (cmd.moveX >  0.5f) { dirX += cosY; dirZ -= sinY; }

    float lenSq = dirX * dirX + dirZ * dirZ;
    bool moving = lenSq > 0.001f;

    float speed = moveSpeed;
    if (cmd.sprint && moving) speed *= sprintMult;
    if (cmd.crouch) speed *= 0.5f;

    if (moving) {
        float invLen = 1.0f / sqrtf(lenSq);
        state.position.x += dirX * invLen * speed * dt;
        state.position.z += dirZ * invLen * speed * dt;
    }

    // Jump
    if (cmd.jump && state.onGround) {
        state.velocity.y = jumpForce;
        state.onGround = false;
    }

    // Gravity
    state.velocity.y -= gravity * dt;
    state.position.y += state.velocity.y * dt;

    // Ground (position is feet-level)
    if (state.position.y <= groundY) {
        state.position.y = groundY;
        state.velocity.y = 0.0f;
        state.onGround = true;
    }
}

void ClientState::PredictInput(const InputCommand& cmd, float dt) {
    ApplyInput(m_predicted, cmd, dt);
}

// ============================================================
// Server Reconciliation
// ============================================================

void ClientState::Reconcile(uint32_t serverTick, const XMFLOAT3& serverPos,
                             const XMFLOAT3& /*serverVel*/, bool /*serverOnGround*/) {
    // Discard inputs already processed by the server
    while (!m_pendingInputs.empty() && m_pendingInputs.front().tick <= serverTick) {
        m_pendingInputs.pop_front();
    }

    // Update persistent correction target (blended toward every frame in SmoothCorrect)
    m_serverTarget = serverPos;
    m_hasServerTarget = true;
}

XMFLOAT3 ClientState::SmoothCorrect(const XMFLOAT3& localPos, float dt) {
    if (!m_hasServerTarget) return localPos;

    float dx = m_serverTarget.x - localPos.x;
    float dz = m_serverTarget.z - localPos.z;
    float distSq = dx * dx + dz * dz;

    if (distSq > 25.0f) {
        // Teleport if > 5 units off
        return m_serverTarget;
    }
    if (distSq < 0.0004f) {
        // Within 0.02 units — no correction needed
        return localPos;
    }

    // Exponential blend — reaches 95% in ~0.2s
    float t = 1.0f - expf(-15.0f * dt);
    return {
        localPos.x + dx * t,
        localPos.y,  // Keep local Y (gravity/ground handled locally)
        localPos.z + dz * t
    };
}

// ============================================================
// Remote Player Interpolation
// ============================================================

void ClientState::AddRemoteSnapshot(uint8_t playerId, float serverTime,
                                     const PlayerSnapshot& snapshot) {
    // Find or create remote player
    RemotePlayer* remote = nullptr;
    for (auto& rp : m_remotePlayers) {
        if (rp.playerId == playerId) {
            remote = &rp;
            break;
        }
    }
    if (!remote) {
        m_remotePlayers.push_back({});
        remote = &m_remotePlayers.back();
        remote->playerId = playerId;
    }

    RemotePlayer::Snapshot snap;
    snap.serverTime = serverTime;
    snap.position = snapshot.position;
    snap.yaw = snapshot.yaw;
    snap.pitch = snapshot.pitch;
    snap.health = snapshot.health;
    snap.weaponSlot = snapshot.weaponSlot;
    snap.animState = snapshot.animState;

    remote->snapshots.push_back(snap);
    while (static_cast<int>(remote->snapshots.size()) > RemotePlayer::kMaxSnapshots) {
        remote->snapshots.pop_front();
    }
}

void ClientState::RemoveRemotePlayer(uint8_t playerId) {
    m_remotePlayers.erase(
        std::remove_if(m_remotePlayers.begin(), m_remotePlayers.end(),
            [playerId](const RemotePlayer& rp) { return rp.playerId == playerId; }),
        m_remotePlayers.end()
    );
}

void ClientState::InterpolateRemotePlayers(float renderTime) {
    float interpTime = renderTime - interpolationDelay;

    for (auto& remote : m_remotePlayers) {
        if (remote.snapshots.size() < 2) {
            // Not enough data to interpolate, use latest
            if (!remote.snapshots.empty()) {
                const auto& latest = remote.snapshots.back();
                remote.displayPosition = latest.position;
                remote.displayYaw = latest.yaw;
                remote.displayPitch = latest.pitch;
                remote.displayHealth = latest.health;
            }
            continue;
        }

        // Find the two snapshots surrounding interpTime
        const RemotePlayer::Snapshot* from = nullptr;
        const RemotePlayer::Snapshot* to = nullptr;

        for (int i = 0; i < static_cast<int>(remote.snapshots.size()) - 1; i++) {
            if (remote.snapshots[i].serverTime <= interpTime &&
                remote.snapshots[i + 1].serverTime >= interpTime) {
                from = &remote.snapshots[i];
                to = &remote.snapshots[i + 1];
                break;
            }
        }

        if (from && to) {
            float duration = to->serverTime - from->serverTime;
            float t = (duration > 0.0001f) ? (interpTime - from->serverTime) / duration : 0.0f;
            t = (std::max)(0.0f, (std::min)(1.0f, t));

            remote.displayPosition.x = from->position.x + (to->position.x - from->position.x) * t;
            remote.displayPosition.y = from->position.y + (to->position.y - from->position.y) * t;
            remote.displayPosition.z = from->position.z + (to->position.z - from->position.z) * t;
            remote.displayYaw = from->yaw + (to->yaw - from->yaw) * t;
            remote.displayPitch = from->pitch + (to->pitch - from->pitch) * t;
            remote.displayHealth = from->health + (to->health - from->health) * t;
        } else {
            // Extrapolate from last known state
            const auto& latest = remote.snapshots.back();
            remote.displayPosition = latest.position;
            remote.displayYaw = latest.yaw;
            remote.displayPitch = latest.pitch;
            remote.displayHealth = latest.health;
        }
    }
}

} // namespace WT
