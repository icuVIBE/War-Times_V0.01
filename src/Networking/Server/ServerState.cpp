#include "ServerState.h"
#include <algorithm>
#include <cmath>

namespace WT {

using namespace DirectX;

// ============================================================
// Init / Shutdown
// ============================================================

void ServerState::Init(float tickRate) {
    m_tickRate = tickRate;
    m_tickInterval = 1.0f / tickRate;
    m_accumulator = 0.0f;
    m_currentTick = 0;
    m_players.clear();
    m_inputQueue.clear();
    m_nextPlayerId = 1;
}

void ServerState::Shutdown() {
    m_players.clear();
    m_inputQueue.clear();
}

// ============================================================
// Tick Management
// ============================================================

int ServerState::Update(float dt) {
    m_accumulator += dt;
    int ticksRun = 0;

    while (m_accumulator >= m_tickInterval) {
        m_accumulator -= m_tickInterval;
        Tick();
        ticksRun++;

        // Safety: prevent spiral of death
        if (ticksRun >= 5) {
            m_accumulator = 0.0f;
            break;
        }
    }

    return ticksRun;
}

void ServerState::Tick() {
    m_currentTick++;
    ProcessInputs();
    UpdateRespawns(m_tickInterval);
    RecordPositionHistory();
}

// ============================================================
// Player Management
// ============================================================

uint8_t ServerState::AddPlayer(ConnectionId connectionId) {
    ServerPlayer player;
    player.connectionId = connectionId;
    player.playerId = m_nextPlayerId++;
    player.position = { 0, 0, 0 }; // Default spawn (feet on ground)
    player.health = 100.0f;
    player.maxHealth = 100.0f;
    player.alive = true;
    m_players.push_back(player);
    return player.playerId;
}

void ServerState::RemovePlayer(ConnectionId connectionId) {
    m_players.erase(
        std::remove_if(m_players.begin(), m_players.end(),
            [connectionId](const ServerPlayer& p) { return p.connectionId == connectionId; }),
        m_players.end()
    );
}

ServerPlayer* ServerState::GetPlayerByConnection(ConnectionId id) {
    for (auto& p : m_players) {
        if (p.connectionId == id) return &p;
    }
    return nullptr;
}

const ServerPlayer* ServerState::GetPlayerByConnection(ConnectionId id) const {
    for (const auto& p : m_players) {
        if (p.connectionId == id) return &p;
    }
    return nullptr;
}

ServerPlayer* ServerState::GetPlayer(uint8_t playerId) {
    for (auto& p : m_players) {
        if (p.playerId == playerId) return &p;
    }
    return nullptr;
}

// ============================================================
// Input Processing
// ============================================================

void ServerState::QueueInput(ConnectionId connectionId, const InputCommand& cmd) {
    m_inputQueue.push_back({ connectionId, cmd });
}

void ServerState::ProcessInputs() {
    for (auto& qi : m_inputQueue) {
        ServerPlayer* player = GetPlayerByConnection(qi.connectionId);
        if (!player || !player->alive) continue;

        // Only process if this input is newer than what we've already processed
        if (qi.command.tick <= player->lastProcessedTick) continue;

        player->lastInput = qi.command;
        player->lastProcessedTick = qi.command.tick;

        SimulatePlayerMovement(*player, qi.command, m_tickInterval);
    }
    m_inputQueue.clear();
}

void ServerState::SimulatePlayerMovement(ServerPlayer& player, const InputCommand& input, float dt) {
    // Apply look direction
    player.yaw = input.yaw;
    player.pitch = input.pitch;
    player.weaponSlot = input.weaponSlot;

    // Calculate movement direction relative to yaw (matching Character::UpdateMovement)
    float yawRad = XMConvertToRadians(input.yaw);
    float sinY = sinf(yawRad);
    float cosY = cosf(yawRad);

    // Decompose into world-space direction (same as Character)
    float dirX = 0.0f, dirZ = 0.0f;
    if (input.moveZ >  0.5f) { dirX += sinY; dirZ += cosY; }  // W
    if (input.moveZ < -0.5f) { dirX -= sinY; dirZ -= cosY; }  // S
    if (input.moveX < -0.5f) { dirX -= cosY; dirZ += sinY; }  // A
    if (input.moveX >  0.5f) { dirX += cosY; dirZ -= sinY; }  // D

    // Normalize diagonal movement (same as Character)
    float lenSq = dirX * dirX + dirZ * dirZ;
    bool moving = lenSq > 0.001f;

    float speed = moveSpeed;
    if (input.sprint && moving) speed *= sprintMult;
    if (input.crouch) speed *= 0.5f;

    if (moving) {
        float invLen = 1.0f / sqrtf(lenSq);
        player.position.x += dirX * invLen * speed * dt;
        player.position.z += dirZ * invLen * speed * dt;
    }

    // Vertical: gravity + jump
    if (input.jump && player.onGround) {
        player.velocity.y = jumpForce;
        player.onGround = false;
    }

    player.velocity.y -= gravity * dt;
    player.position.y += player.velocity.y * dt;

    // Ground check (position is feet-level)
    if (player.position.y <= groundY) {
        player.position.y = groundY;
        player.velocity.y = 0.0f;
        player.onGround = true;
    }
}

// ============================================================
// Respawn
// ============================================================

void ServerState::UpdateRespawns(float dt) {
    for (auto& player : m_players) {
        if (!player.alive) {
            player.respawnTimer -= dt;
            if (player.respawnTimer <= 0.0f) {
                // Auto-respawn at origin (game mode should override spawn point)
                RespawnPlayer(player.playerId, { 0, 1, 0 });
            }
        }
    }
}

void ServerState::RespawnPlayer(uint8_t playerId, const XMFLOAT3& spawnPos) {
    ServerPlayer* player = GetPlayer(playerId);
    if (!player) return;

    player->position = spawnPos;
    player->velocity = { 0, 0, 0 };
    player->health = player->maxHealth;
    player->alive = true;
    player->respawnTimer = 0.0f;
}

// ============================================================
// Lag Compensation
// ============================================================

void ServerState::RecordPositionHistory() {
    for (auto& player : m_players) {
        ServerPlayer::PositionSample sample;
        sample.tick = m_currentTick;
        sample.position = player.position;
        sample.halfExtents = { 0.3f, 0.85f, 0.3f }; // Approximate player collision box

        player.positionHistory.push_back(sample);
        while (static_cast<int>(player.positionHistory.size()) > ServerPlayer::kMaxHistorySamples) {
            player.positionHistory.pop_front();
        }
    }
}

void ServerState::RewindToTick(uint32_t tick) {
    m_savedPositions.clear();

    for (auto& player : m_players) {
        // Save current position
        m_savedPositions.push_back({ player.playerId, player.position });

        // Find closest history sample at or before the target tick
        XMFLOAT3 rewoundPos = player.position;
        for (auto it = player.positionHistory.rbegin(); it != player.positionHistory.rend(); ++it) {
            if (it->tick <= tick) {
                rewoundPos = it->position;

                // Interpolate if we have the next sample
                auto next = it;
                if (next != player.positionHistory.rbegin()) {
                    --next;
                    if (next->tick > tick) {
                        float t = static_cast<float>(tick - it->tick) /
                                  static_cast<float>(next->tick - it->tick);
                        rewoundPos.x = it->position.x + (next->position.x - it->position.x) * t;
                        rewoundPos.y = it->position.y + (next->position.y - it->position.y) * t;
                        rewoundPos.z = it->position.z + (next->position.z - it->position.z) * t;
                    }
                }
                break;
            }
        }

        player.position = rewoundPos;
    }
}

void ServerState::RestorePositions() {
    for (auto& saved : m_savedPositions) {
        ServerPlayer* player = GetPlayer(saved.playerId);
        if (player) player->position = saved.position;
    }
    m_savedPositions.clear();
}

// ============================================================
// Snapshot Generation
// ============================================================

std::vector<PlayerSnapshot> ServerState::BuildPlayerSnapshots() const {
    std::vector<PlayerSnapshot> snapshots;
    snapshots.reserve(m_players.size());

    for (const auto& player : m_players) {
        PlayerSnapshot snap;
        snap.playerId = player.playerId;
        snap.position = player.position;
        snap.yaw = player.yaw;
        snap.pitch = player.pitch;
        snap.health = player.health;
        snap.weaponSlot = player.weaponSlot;
        snap.animState = 0; // TODO: animation state mapping
        snapshots.push_back(snap);
    }

    return snapshots;
}

// ============================================================
// Game Logic
// ============================================================

bool ServerState::DamagePlayer(uint8_t targetId, float damage, uint8_t sourceId) {
    ServerPlayer* target = GetPlayer(targetId);
    if (!target || !target->alive) return false;

    target->health -= damage;
    if (target->health <= 0.0f) {
        target->health = 0.0f;
        target->alive = false;
        target->respawnTimer = respawnTime;
        target->deaths++;

        ServerPlayer* source = GetPlayer(sourceId);
        if (source && sourceId != targetId) {
            source->kills++;
        }
        return true; // Killed
    }
    return false;
}

} // namespace WT
