#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include "ItemSystem.h"

namespace WT {

using namespace DirectX;

class Scene;
class Camera;
class Character;
class PhysicsWorld;
class Inventory;
class Input;
struct Entity;

// ============================================================
// Interaction Mode — press vs hold
// ============================================================
enum class InteractMode : uint8_t {
    Press = 0,      // Single press (doors, buttons)
    Hold,           // Hold key for holdDuration (looting, planting)
};

// ============================================================
// Interaction Priority (lower value = higher priority)
// ============================================================
enum class InteractPriority : uint8_t {
    Critical = 0,       // Objectives, revive, defuse
    Quest    = 1,       // Quest-related objects
    Lootable = 2,       // Containers, item pickups
    Environmental = 3,  // Doors, switches, generic props
};

// ============================================================
// Result of the interaction scan (run every frame)
// ============================================================
struct InteractTarget {
    bool  found       = false;
    int   entityIndex = -1;
    float distance    = 0.0f;
    std::string prompt;
    InteractMode mode            = InteractMode::Press;
    float        holdDuration    = 0.0f;
    InteractPriority priority    = InteractPriority::Environmental;
};

// ============================================================
// Interaction System — AAA-grade with hold, cooldown, priority
// ============================================================
class InteractionSystem {
public:
    // Call every frame
    void Update(float dt, const Camera& camera, const Input& input,
                Scene& scene, const PhysicsWorld& physics, Character& character,
                Inventory* inventory = nullptr);

    const InteractTarget& GetTarget() const { return m_target; }

    // Hold interaction progress (0..1) for HUD progress bar
    float GetHoldProgress() const { return m_holdProgress; }
    bool  IsHolding() const { return m_holding; }

    // Cooldown remaining
    float GetCooldown() const { return m_cooldownTimer; }

    // Container UI state
    bool  IsContainerOpen() const { return m_containerOpen; }
    int   GetContainerEntityIndex() const { return m_containerEntity; }
    std::vector<ItemInstance>& GetContainerItems() { return m_containerItems; }
    void  CloseContainer();

private:
    void PerformInteraction(Entity& entity, int entityIndex, Scene& scene,
                            Character& character, Inventory* inventory);
    void UpdateDoorAnimations(float dt, Scene& scene);
    std::string GetDefaultPrompt(const Entity& entity) const;
    InteractPriority GetEntityPriority(const Entity& entity) const;
    InteractMode GetEntityMode(const Entity& entity) const;
    float GetEntityHoldDuration(const Entity& entity) const;

    InteractTarget m_target;

    // Hold interaction
    bool  m_holding      = false;
    float m_holdProgress = 0.0f;
    float m_holdTarget   = 0.0f;

    // Cooldown
    float m_cooldownTimer = 0.0f;
    static constexpr float kCooldownDuration = 0.25f;

    // Container state (for loot UI)
    bool m_containerOpen = false;
    int  m_containerEntity = -1;
    std::vector<ItemInstance> m_containerItems;
};

} // namespace WT
