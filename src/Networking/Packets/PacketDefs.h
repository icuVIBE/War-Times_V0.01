#pragma once

#include "BitStream.h"
#include <cstdint>
#include <string>
#include <DirectXMath.h>

namespace WT {

// ============================================================
// Packet Types — unique ID for every network message
// ============================================================

enum class PacketType : uint8_t {
    // Connection lifecycle
    ConnectionRequest   = 0x01,
    ConnectionAccept    = 0x02,
    ConnectionDeny      = 0x03,
    Disconnect          = 0x04,
    Heartbeat           = 0x05,

    // Client → Server
    ClientInput         = 0x10,  // Player input command
    ClientReady         = 0x11,  // Client finished loading
    ClientChat          = 0x12,

    // Server → Client
    ServerSnapshot      = 0x20,  // World state snapshot
    ServerDelta         = 0x21,  // Delta-compressed state update
    ServerEvent         = 0x22,  // One-shot event (explosion, sound, etc.)
    ServerChat          = 0x23,
    ServerSpawn         = 0x24,  // Entity spawn notification
    ServerDespawn       = 0x25,  // Entity remove notification

    // Replication
    ReplicationFull     = 0x30,  // Full entity state for new/relevant entity
    ReplicationDelta    = 0x31,  // Delta update for existing entity
    ReplicationAck      = 0x32,  // Client confirms receipt of replication data

    // Level Transfer
    LevelData           = 0x40,  // Chunked level entity data (server → client)
};

// ============================================================
// Packet Header — present at the start of every packet
// ============================================================

struct PacketHeader {
    PacketType type;
    uint16_t   sequence;    // Packet sequence number
    uint16_t   ack;         // Latest received sequence from remote
    uint32_t   ackBits;     // Bitfield of received sequences (relative to ack)

    void Write(BitStream& bs) const {
        bs.WriteUInt8(static_cast<uint8_t>(type));
        bs.WriteUInt16(sequence);
        bs.WriteUInt16(ack);
        bs.WriteUInt32(ackBits);
    }

    void Read(BitStream& bs) {
        type     = static_cast<PacketType>(bs.ReadUInt8());
        sequence = bs.ReadUInt16();
        ack      = bs.ReadUInt16();
        ackBits  = bs.ReadUInt32();
    }
};

// ============================================================
// Input Command — client player input for one tick
// ============================================================

struct InputCommand {
    uint32_t tick = 0;          // Server tick this input is for
    float    moveX = 0.0f;     // -1 to 1 (strafe)
    float    moveZ = 0.0f;     // -1 to 1 (forward/back)
    float    yaw = 0.0f;       // Camera yaw (degrees)
    float    pitch = 0.0f;     // Camera pitch (degrees)
    bool     jump = false;
    bool     crouch = false;
    bool     sprint = false;
    bool     fire = false;
    bool     reload = false;
    bool     interact = false;
    uint8_t  weaponSlot = 0;   // Selected weapon index

    void Write(BitStream& bs) const {
        bs.WriteUInt32(tick);
        // Quantise movement to 8 bits (-1..1 → 0..255)
        bs.WriteUInt8(static_cast<uint8_t>((moveX + 1.0f) * 127.5f));
        bs.WriteUInt8(static_cast<uint8_t>((moveZ + 1.0f) * 127.5f));
        bs.WriteFloat16(yaw);
        bs.WriteFloat16(pitch);
        // Pack booleans into a single byte
        uint8_t flags = 0;
        if (jump)     flags |= (1 << 0);
        if (crouch)   flags |= (1 << 1);
        if (sprint)   flags |= (1 << 2);
        if (fire)     flags |= (1 << 3);
        if (reload)   flags |= (1 << 4);
        if (interact) flags |= (1 << 5);
        bs.WriteUInt8(flags);
        bs.WriteUInt8(weaponSlot);
    }

    void Read(BitStream& bs) {
        tick = bs.ReadUInt32();
        moveX = (bs.ReadUInt8() / 127.5f) - 1.0f;
        moveZ = (bs.ReadUInt8() / 127.5f) - 1.0f;
        yaw   = bs.ReadFloat16();
        pitch = bs.ReadFloat16();
        uint8_t flags = bs.ReadUInt8();
        jump     = (flags & (1 << 0)) != 0;
        crouch   = (flags & (1 << 1)) != 0;
        sprint   = (flags & (1 << 2)) != 0;
        fire     = (flags & (1 << 3)) != 0;
        reload   = (flags & (1 << 4)) != 0;
        interact = (flags & (1 << 5)) != 0;
        weaponSlot = bs.ReadUInt8();
    }
};

// ============================================================
// Entity Snapshot — state of a single replicated entity
// ============================================================

struct EntitySnapshot {
    uint16_t netId = 0;         // Network entity ID
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };
    float    health = 100.0f;
    uint8_t  flags = 0;         // Packed: visible, destructible, etc.

    void Write(BitStream& bs) const {
        bs.WriteUInt16(netId);
        bs.WriteCompressedVec3(position, 500.0f, 16);  // ±500 range, 16 bits/axis
        bs.WriteCompressedVec3(rotation, 180.0f, 10);   // ±180°, 10 bits/axis
        bs.WriteFloat16(health);
        bs.WriteUInt8(flags);
    }

    void Read(BitStream& bs) {
        netId    = bs.ReadUInt16();
        position = bs.ReadCompressedVec3(500.0f, 16);
        rotation = bs.ReadCompressedVec3(180.0f, 10);
        health   = bs.ReadFloat16();
        flags    = bs.ReadUInt8();
    }
};

// ============================================================
// Player Snapshot — state of a player for server snapshot
// ============================================================

struct PlayerSnapshot {
    uint8_t  playerId = 0;
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    float    yaw = 0.0f;
    float    pitch = 0.0f;
    float    health = 100.0f;
    uint8_t  weaponSlot = 0;
    uint8_t  animState = 0;     // Animation state index

    void Write(BitStream& bs) const {
        bs.WriteUInt8(playerId);
        bs.WriteCompressedVec3(position, 500.0f, 16);
        bs.WriteFloat16(yaw);
        bs.WriteFloat16(pitch);
        bs.WriteFloat16(health);
        bs.WriteUInt8(weaponSlot);
        bs.WriteUInt8(animState);
    }

    void Read(BitStream& bs) {
        playerId   = bs.ReadUInt8();
        position   = bs.ReadCompressedVec3(500.0f, 16);
        yaw        = bs.ReadFloat16();
        pitch      = bs.ReadFloat16();
        health     = bs.ReadFloat16();
        weaponSlot = bs.ReadUInt8();
        animState  = bs.ReadUInt8();
    }
};

// ============================================================
// Server Event — one-shot events broadcast to clients
// ============================================================

enum class ServerEventType : uint8_t {
    WeaponFired = 0,
    Explosion,
    EntityDestroyed,
    DamageDealt,
    PickupCollected,
    DoorToggled,
    SwitchToggled,
    VoxelDestroyed,     // A voxel cell was removed (includes cell index)
    EntityRemoved,      // Entity fully removed from scene
    LootContainerOpened,
};

struct ServerEvent {
    ServerEventType eventType = ServerEventType::WeaponFired;
    uint8_t  sourcePlayer = 0;
    uint16_t targetNetId = 0;
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    float    value = 0.0f;      // Damage amount, etc.
    int16_t  extra = 0;         // Voxel cell index, etc.

    void Write(BitStream& bs) const {
        bs.WriteUInt8(static_cast<uint8_t>(eventType));
        bs.WriteUInt8(sourcePlayer);
        bs.WriteUInt16(targetNetId);
        bs.WriteVec3(position);
        bs.WriteFloat16(value);
        bs.WriteUInt16(static_cast<uint16_t>(extra));
    }

    void Read(BitStream& bs) {
        eventType    = static_cast<ServerEventType>(bs.ReadUInt8());
        sourcePlayer = bs.ReadUInt8();
        targetNetId  = bs.ReadUInt16();
        position     = bs.ReadVec3();
        value        = bs.ReadFloat16();
        extra        = static_cast<int16_t>(bs.ReadUInt16());
    }
};

// ============================================================
// Net Entity Data — full entity serialization for level transfer
// ============================================================
// Mirrors the persistent fields saved by LevelFile.
// Sent once per entity when a client joins.

struct NetEntityData {
    // Identity
    std::string name;
    uint8_t meshType = 0;           // MeshType enum
    std::string meshName;            // Custom mesh key
    std::string textureName;         // Texture override key

    // Transform
    DirectX::XMFLOAT3 position = {0,0,0};
    DirectX::XMFLOAT3 rotation = {0,0,0};
    DirectX::XMFLOAT3 scale    = {1,1,1};
    float color[4] = {0.6f, 0.6f, 0.6f, 1.0f};

    // Flags (packed into one uint16)
    bool visible       = true;
    bool castShadow    = true;
    bool isLadder      = false;
    bool hasGravity    = false;
    bool destructible  = true;
    bool voxelDestruction = false;
    bool smokeOnDamage = true;
    bool noCollision   = false;

    // Physics / destruction
    uint8_t materialType = 0;
    float maxHealth      = 100.0f;
    uint8_t debrisCount  = 6;
    float debrisScale    = 0.3f;
    std::string supportedBy;
    uint8_t breakPieceCount = 3;
    uint8_t voxelRes     = 2;
    float despawnTimer   = -1.0f;

    // Pickup
    uint8_t pickupType   = 0;
    float pickupAmount   = 25.0f;
    float pickupRadius   = 1.5f;
    float pickupBobSpeed = 2.0f;
    float pickupBobHeight = 0.15f;
    float pickupSpinSpeed = 90.0f;
    float pickupRespawnTime = 15.0f;

    // Damage visuals
    float damageFlashDuration = 0.15f;
    float damageFlashColor[3] = {1.0f, 0.3f, 0.1f};

    // Interaction
    uint8_t interactType = 0;
    float interactRange  = 3.0f;
    std::string interactPrompt;
    std::string interactTarget;

    // Door
    float doorOpenAngle  = 90.0f;
    float doorSlideDist  = 0.0f;
    float doorSpeed      = 3.0f;

    // Cull category
    uint8_t cullCategory = 1; // CullCategory::Prop

    void Write(BitStream& bs) const {
        bs.WriteString(name);
        bs.WriteUInt8(meshType);
        bs.WriteString(meshName);
        bs.WriteString(textureName);

        bs.WriteVec3(position);
        bs.WriteVec3(rotation);
        bs.WriteVec3(scale);
        bs.WriteFloat(color[0]); bs.WriteFloat(color[1]);
        bs.WriteFloat(color[2]); bs.WriteFloat(color[3]);

        // Pack 8 bools into one byte
        uint8_t flags = 0;
        if (visible)          flags |= (1 << 0);
        if (castShadow)       flags |= (1 << 1);
        if (isLadder)         flags |= (1 << 2);
        if (hasGravity)       flags |= (1 << 3);
        if (destructible)     flags |= (1 << 4);
        if (voxelDestruction) flags |= (1 << 5);
        if (smokeOnDamage)    flags |= (1 << 6);
        if (noCollision)      flags |= (1 << 7);
        bs.WriteUInt8(flags);

        bs.WriteUInt8(materialType);
        bs.WriteFloat(maxHealth);
        bs.WriteUInt8(debrisCount);
        bs.WriteFloat(debrisScale);
        bs.WriteString(supportedBy);
        bs.WriteUInt8(breakPieceCount);
        bs.WriteUInt8(voxelRes);
        bs.WriteFloat(despawnTimer);

        bs.WriteUInt8(pickupType);
        bs.WriteFloat(pickupAmount);
        bs.WriteFloat(pickupRadius);
        bs.WriteFloat(pickupBobSpeed);
        bs.WriteFloat(pickupBobHeight);
        bs.WriteFloat(pickupSpinSpeed);
        bs.WriteFloat(pickupRespawnTime);

        bs.WriteFloat(damageFlashDuration);
        bs.WriteFloat(damageFlashColor[0]);
        bs.WriteFloat(damageFlashColor[1]);
        bs.WriteFloat(damageFlashColor[2]);

        bs.WriteUInt8(interactType);
        bs.WriteFloat(interactRange);
        bs.WriteString(interactPrompt);
        bs.WriteString(interactTarget);

        bs.WriteFloat(doorOpenAngle);
        bs.WriteFloat(doorSlideDist);
        bs.WriteFloat(doorSpeed);
        bs.WriteUInt8(cullCategory);
    }

    void Read(BitStream& bs) {
        name     = bs.ReadString();
        meshType = bs.ReadUInt8();
        meshName = bs.ReadString();
        textureName = bs.ReadString();

        position = bs.ReadVec3();
        rotation = bs.ReadVec3();
        scale    = bs.ReadVec3();
        color[0] = bs.ReadFloat(); color[1] = bs.ReadFloat();
        color[2] = bs.ReadFloat(); color[3] = bs.ReadFloat();

        uint8_t flags = bs.ReadUInt8();
        visible          = (flags & (1 << 0)) != 0;
        castShadow       = (flags & (1 << 1)) != 0;
        isLadder         = (flags & (1 << 2)) != 0;
        hasGravity       = (flags & (1 << 3)) != 0;
        destructible     = (flags & (1 << 4)) != 0;
        voxelDestruction = (flags & (1 << 5)) != 0;
        smokeOnDamage    = (flags & (1 << 6)) != 0;
        noCollision      = (flags & (1 << 7)) != 0;

        materialType = bs.ReadUInt8();
        maxHealth    = bs.ReadFloat();
        debrisCount  = bs.ReadUInt8();
        debrisScale  = bs.ReadFloat();
        supportedBy  = bs.ReadString();
        breakPieceCount = bs.ReadUInt8();
        voxelRes     = bs.ReadUInt8();
        despawnTimer = bs.ReadFloat();

        pickupType      = bs.ReadUInt8();
        pickupAmount    = bs.ReadFloat();
        pickupRadius    = bs.ReadFloat();
        pickupBobSpeed  = bs.ReadFloat();
        pickupBobHeight = bs.ReadFloat();
        pickupSpinSpeed = bs.ReadFloat();
        pickupRespawnTime = bs.ReadFloat();

        damageFlashDuration = bs.ReadFloat();
        damageFlashColor[0] = bs.ReadFloat();
        damageFlashColor[1] = bs.ReadFloat();
        damageFlashColor[2] = bs.ReadFloat();

        interactType   = bs.ReadUInt8();
        interactRange  = bs.ReadFloat();
        interactPrompt = bs.ReadString();
        interactTarget = bs.ReadString();

        doorOpenAngle = bs.ReadFloat();
        doorSlideDist = bs.ReadFloat();
        doorSpeed     = bs.ReadFloat();
        cullCategory  = bs.ReadUInt8();
    }
};

} // namespace WT
