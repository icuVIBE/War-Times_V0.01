#include "InteractionSystem.h"
#include "Inventory.h"
#include "../Core/Entity.h"
#include "../Core/Character.h"
#include "../Graphics/Camera.h"
#include "../Core/Input.h"
#include "../Physics/PhysicsWorld.h"
#include "../Util/Log.h"

namespace WT {

void InteractionSystem::Update(float dt, const Camera& camera, const Input& input,
                               Scene& scene, const PhysicsWorld& physics, Character& character,
                               Inventory* inventory)
{
    // Always update door animations
    UpdateDoorAnimations(dt, scene);

    // Tick cooldown
    if (m_cooldownTimer > 0.0f)
        m_cooldownTimer -= dt;

    // ---- Scan for best interactable ----
    m_target = {};

    XMFLOAT3 origin = camera.GetPosition();
    XMFLOAT3 forward = camera.GetForward();

    struct Candidate {
        int   index;
        float dist;
        InteractPriority priority;
    };
    std::vector<Candidate> candidates;

    for (int i = 0; i < scene.GetEntityCount(); i++) {
        const Entity& e = scene.GetEntity(i);
        if (e.interactType == InteractType::None && e.pickupType == PickupType::None
            && !e.isLootContainer) continue;
        if (!e.visible || e.IsDestroyed()) continue;

        float dx = e.position[0] - origin.x;
        float dy = e.position[1] - origin.y;
        float dz = e.position[2] - origin.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);

        float range = e.interactRange;
        if (e.pickupType != PickupType::None || e.isLootContainer)
            range = (std::max)(range, 3.0f);  // Minimum pickup/loot range

        if (dist > range) continue;

        float invDist = 1.0f / (dist + 0.001f);
        float dot = (dx * forward.x + dy * forward.y + dz * forward.z) * invDist;
        if (dot < 0.5f) continue;

        Candidate c;
        c.index = i;
        c.dist = dist;
        c.priority = GetEntityPriority(scene.GetEntity(i));
        candidates.push_back(c);
    }

    // ---- Priority resolution: pick best candidate ----
    // Lower priority value = higher importance. Ties broken by distance.
    int bestIdx = -1;
    float bestDist = 1e9f;
    InteractPriority bestPri = InteractPriority::Environmental;

    for (auto& c : candidates) {
        if (bestIdx < 0
            || c.priority < bestPri
            || (c.priority == bestPri && c.dist < bestDist)) {
            bestIdx = c.index;
            bestDist = c.dist;
            bestPri = c.priority;
        }
    }

    if (bestIdx >= 0) {
        Entity& entity = scene.GetEntity(bestIdx);
        m_target.found = true;
        m_target.entityIndex = bestIdx;
        m_target.distance = bestDist;
        m_target.mode = GetEntityMode(entity);
        m_target.holdDuration = GetEntityHoldDuration(entity);
        m_target.priority = bestPri;
        m_target.prompt = entity.interactPrompt.empty()
                          ? GetDefaultPrompt(entity) : entity.interactPrompt;

        // ---- Handle interaction input ----
        if (m_cooldownTimer <= 0.0f) {
            if (m_target.mode == InteractMode::Press) {
                // Instant interaction on F press
                if (input.IsKeyPressed('F')) {
                    PerformInteraction(entity, bestIdx, scene, character, inventory);
                    m_cooldownTimer = kCooldownDuration;
                }
            } else if (m_target.mode == InteractMode::Hold) {
                // Hold interaction: track hold progress
                if (input.IsKeyDown('F')) {
                    if (!m_holding) {
                        m_holding = true;
                        m_holdProgress = 0.0f;
                        m_holdTarget = m_target.holdDuration;
                    }
                    if (m_holdTarget > 0.0f) {
                        m_holdProgress += dt / m_holdTarget;
                        if (m_holdProgress >= 1.0f) {
                            m_holdProgress = 1.0f;
                            PerformInteraction(entity, bestIdx, scene, character, inventory);
                            m_holding = false;
                            m_holdProgress = 0.0f;
                            m_cooldownTimer = kCooldownDuration;
                        }
                    }
                } else {
                    // Released early — cancel
                    m_holding = false;
                    m_holdProgress = 0.0f;
                }
            }
        }
    } else {
        // No target — reset hold state
        m_holding = false;
        m_holdProgress = 0.0f;
    }
}

// ============================================================
// Perform Interaction
// ============================================================
void InteractionSystem::PerformInteraction(Entity& entity, int entityIndex,
                                           Scene& scene, Character& character,
                                           Inventory* inventory) {
    // ---- Check conditions ----
    if (entity.requiresItemID > 0 && inventory) {
        if (!inventory->HasItem(entity.requiresItemID)) {
            const ItemDef* keyDef = ItemDatabase::Get().GetDef(entity.requiresItemID);
            LOG_INFO("Interaction: Requires '%s'",
                     keyDef ? keyDef->itemName.c_str() : "unknown item");
            return;
        }
        // Consume key item for one-shot doors/containers
        if (entity.interactType == InteractType::Door || entity.isLootContainer) {
            inventory->RemoveItem(entity.requiresItemID, 1);
        }
    }

    // ---- Pickup (goes to inventory) ----
    if (entity.pickupType != PickupType::None && entity.interactType == InteractType::None) {
        if (inventory) {
            int itemID = entity.pickupItemID;  // Map to item database
            if (itemID > 0) {
                int leftover = inventory->AddItem(itemID, (int)entity.pickupAmount);
                if (leftover < (int)entity.pickupAmount) {
                    LOG_INFO("Picked up item (ID %d)", itemID);
                }
            } else {
                // Legacy direct pickup (no item system mapping)
                if (entity.pickupType == PickupType::Health)
                    character.Heal(entity.pickupAmount);
                else if (entity.pickupType == PickupType::Ammo)
                    ; // handled externally
            }
        } else {
            if (entity.pickupType == PickupType::Health)
                character.Heal(entity.pickupAmount);
        }
        entity.pickupCollected = true;
        entity.pickupRespawnTimer = entity.pickupRespawnTime;
        entity.visible = false;
        return;
    }

    // ---- Loot Container ----
    if (entity.isLootContainer) {
        if (inventory && !m_containerOpen) {
            m_containerOpen = true;
            m_containerEntity = entityIndex;
            // Generate loot if first time opening
            if (!entity.lootGenerated) {
                entity.lootGenerated = true;
                auto& reg = LootTableRegistry::Get();
                auto loot = reg.RollLoot(entity.lootTableName);
                m_containerItems.clear();
                for (auto& [itemID, count] : loot) {
                    ItemInstance inst;
                    inst.defID = itemID;
                    inst.stackCount = count;
                    const ItemDef* def = ItemDatabase::Get().GetDef(itemID);
                    if (def && def->maxDurability > 0)
                        inst.durability = def->maxDurability;
                    m_containerItems.push_back(inst);
                }
                LOG_INFO("Loot container '%s': generated %d items",
                         entity.name.c_str(), (int)m_containerItems.size());
            }
        }
        return;
    }

    // ---- Standard interactions ----
    switch (entity.interactType) {
        case InteractType::Door:
            entity.doorOpen = !entity.doorOpen;
            LOG_INFO("Interaction: %s door '%s'",
                     entity.doorOpen ? "Opening" : "Closing", entity.name.c_str());
            break;

        case InteractType::Switch:
            entity.switchState = !entity.switchState;
            LOG_INFO("Interaction: Switch '%s' -> %s",
                     entity.name.c_str(), entity.switchState ? "ON" : "OFF");
            if (!entity.interactTarget.empty()) {
                for (int i = 0; i < scene.GetEntityCount(); i++) {
                    if (scene.GetEntity(i).name == entity.interactTarget) {
                        scene.GetEntity(i).visible = !scene.GetEntity(i).visible;
                        break;
                    }
                }
            }
            break;

        case InteractType::Button:
            entity.switchState = true;
            LOG_INFO("Interaction: Button '%s' pressed", entity.name.c_str());
            break;

        case InteractType::Pickup:
            // Manual pickup (F to pick up, not auto-collect)
            if (inventory && entity.pickupItemID > 0) {
                int leftover = inventory->AddItem(entity.pickupItemID, (int)entity.pickupAmount);
                if (leftover < (int)entity.pickupAmount) {
                    entity.pickupCollected = true;
                    entity.pickupRespawnTimer = entity.pickupRespawnTime;
                    entity.visible = false;
                    LOG_INFO("Picked up '%s'", entity.name.c_str());
                } else {
                    LOG_INFO("Inventory full — can't pick up '%s'", entity.name.c_str());
                }
            }
            break;

        case InteractType::Loot:
            // Loot interaction — opens container UI
            if (inventory && !m_containerOpen) {
                m_containerOpen = true;
                m_containerEntity = entityIndex;
                if (!entity.lootGenerated) {
                    entity.lootGenerated = true;
                    auto loot = LootTableRegistry::Get().RollLoot(entity.lootTableName);
                    m_containerItems.clear();
                    for (auto& [itemID, count] : loot) {
                        ItemInstance inst;
                        inst.defID = itemID;
                        inst.stackCount = count;
                        const ItemDef* def = ItemDatabase::Get().GetDef(itemID);
                        if (def && def->maxDurability > 0)
                            inst.durability = def->maxDurability;
                        m_containerItems.push_back(inst);
                    }
                }
            }
            break;

        default:
            break;
    }
}

void InteractionSystem::CloseContainer() {
    m_containerOpen = false;
    m_containerEntity = -1;
}

// ============================================================
// Door Animation
// ============================================================
void InteractionSystem::UpdateDoorAnimations(float dt, Scene& scene) {
    for (int i = 0; i < scene.GetEntityCount(); i++) {
        Entity& e = scene.GetEntity(i);
        if (e.interactType != InteractType::Door) continue;

        float target = e.doorOpen ? 1.0f : 0.0f;
        if (fabsf(e.doorProgress - target) < 0.001f) {
            e.doorProgress = target;
            continue;
        }

        float step = e.doorSpeed * dt;
        if (e.doorProgress < target)
            e.doorProgress = (e.doorProgress + step > target) ? target : e.doorProgress + step;
        else
            e.doorProgress = (e.doorProgress - step < target) ? target : e.doorProgress - step;

        if (e.doorSlideDist > 0.0f) {
            // Sliding door (placeholder — level designer sets doorSlideDist)
        } else {
            e.rotation[1] = e.doorOpenAngle * e.doorProgress;
        }
    }
}

// ============================================================
// Prompt Generation
// ============================================================
std::string InteractionSystem::GetDefaultPrompt(const Entity& entity) const {
    // Loot container
    if (entity.isLootContainer) {
        return entity.holdInteract ? "Hold F to search" : "Press F to loot";
    }

    // Item pickup
    if (entity.pickupType != PickupType::None && entity.interactType == InteractType::None) {
        return "Press F to pick up";
    }

    switch (entity.interactType) {
        case InteractType::Door:
            return entity.doorOpen ? "Press F to close" : "Press F to open";
        case InteractType::Switch:
            return entity.switchState ? "Press F to turn off" : "Press F to turn on";
        case InteractType::Button:
            return "Press F to activate";
        case InteractType::Pickup:
            return "Press F to pick up";
        case InteractType::Loot:
            return "Hold F to search";
        default:
            return "Press F";
    }
}

// ============================================================
// Priority / Mode / Hold Duration helpers
// ============================================================
InteractPriority InteractionSystem::GetEntityPriority(const Entity& entity) const {
    if (entity.interactType == InteractType::Button)
        return InteractPriority::Critical;
    if (entity.pickupType != PickupType::None)
        return InteractPriority::Lootable;
    if (entity.isLootContainer || entity.interactType == InteractType::Loot)
        return InteractPriority::Lootable;
    return InteractPriority::Environmental;
}

InteractMode InteractionSystem::GetEntityMode(const Entity& entity) const {
    if (entity.holdInteract) return InteractMode::Hold;
    if (entity.isLootContainer) return InteractMode::Hold;
    if (entity.interactType == InteractType::Loot) return InteractMode::Hold;
    return InteractMode::Press;
}

float InteractionSystem::GetEntityHoldDuration(const Entity& entity) const {
    if (entity.holdInteractDuration > 0.0f) return entity.holdInteractDuration;
    if (entity.isLootContainer || entity.interactType == InteractType::Loot) return 1.5f;
    return 0.0f;
}

} // namespace WT
