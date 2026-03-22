#include "ItemSystem.h"
#include "../Util/Log.h"
#include <cstdlib>
#include <ctime>
#include <algorithm>

namespace WT {

// ============================================================
// ItemDatabase — register built-in items
// ============================================================
void ItemDatabase::Init() {
    m_defs.clear();

    // ---- Weapons ----
    {
        ItemDef d;
        d.itemID = 1001; d.itemName = "Assault Rifle"; d.description = "Standard-issue assault rifle";
        d.type = ItemType::Weapon; d.rarity = ItemRarity::Common;
        d.equipSlot = EquipSlot::PrimaryWeapon;
        d.weight = 3.5f; d.stackMax = 1; d.value = 500;
        d.damage = 25.0f; d.maxDurability = 300; d.weaponTypeIndex = 0; // Rifle
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 1002; d.itemName = "Pistol"; d.description = "Semi-automatic sidearm";
        d.type = ItemType::Weapon; d.rarity = ItemRarity::Common;
        d.equipSlot = EquipSlot::SecondaryWeapon;
        d.weight = 1.2f; d.stackMax = 1; d.value = 200;
        d.damage = 35.0f; d.maxDurability = 250; d.weaponTypeIndex = 1; // Pistol
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 1003; d.itemName = "SG553"; d.description = "Swiss precision rifle";
        d.type = ItemType::Weapon; d.rarity = ItemRarity::Uncommon;
        d.equipSlot = EquipSlot::PrimaryWeapon;
        d.weight = 3.2f; d.stackMax = 1; d.value = 700;
        d.damage = 30.0f; d.maxDurability = 280; d.weaponTypeIndex = 3; // SG553
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 1004; d.itemName = "M4A1"; d.description = "Versatile carbine with low recoil";
        d.type = ItemType::Weapon; d.rarity = ItemRarity::Uncommon;
        d.equipSlot = EquipSlot::PrimaryWeapon;
        d.weight = 3.0f; d.stackMax = 1; d.value = 650;
        d.damage = 28.0f; d.maxDurability = 320; d.weaponTypeIndex = 4; // M4A1
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 1005; d.itemName = "Heavy Rifle"; d.description = "High-caliber battle rifle";
        d.type = ItemType::Weapon; d.rarity = ItemRarity::Rare;
        d.equipSlot = EquipSlot::PrimaryWeapon;
        d.weight = 4.5f; d.stackMax = 1; d.value = 900;
        d.damage = 40.0f; d.maxDurability = 200; d.weaponTypeIndex = 2; // Rifle3
        RegisterItem(d);
    }

    // ---- Ammo ----
    {
        ItemDef d;
        d.itemID = 2001; d.itemName = "Rifle Ammo"; d.description = "5.56mm ammunition";
        d.type = ItemType::Ammo; d.rarity = ItemRarity::Common;
        d.weight = 0.01f; d.stackMax = 120; d.value = 2;
        d.ammoPerUnit = 1;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 2002; d.itemName = "Pistol Ammo"; d.description = "9mm ammunition";
        d.type = ItemType::Ammo; d.rarity = ItemRarity::Common;
        d.weight = 0.008f; d.stackMax = 90; d.value = 1;
        d.ammoPerUnit = 1;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 2003; d.itemName = "Heavy Ammo"; d.description = "7.62mm ammunition";
        d.type = ItemType::Ammo; d.rarity = ItemRarity::Uncommon;
        d.weight = 0.02f; d.stackMax = 60; d.value = 4;
        d.ammoPerUnit = 1;
        RegisterItem(d);
    }

    // ---- Consumables ----
    {
        ItemDef d;
        d.itemID = 3001; d.itemName = "Medkit"; d.description = "Restores 50 health";
        d.type = ItemType::Consumable; d.rarity = ItemRarity::Common;
        d.weight = 0.5f; d.stackMax = 5; d.value = 50;
        d.healAmount = 50.0f;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 3002; d.itemName = "Bandage"; d.description = "Restores 25 health";
        d.type = ItemType::Consumable; d.rarity = ItemRarity::Common;
        d.weight = 0.2f; d.stackMax = 10; d.value = 20;
        d.healAmount = 25.0f;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 3003; d.itemName = "Adrenaline Shot"; d.description = "Temporary speed boost for 10 seconds";
        d.type = ItemType::Consumable; d.rarity = ItemRarity::Rare;
        d.weight = 0.3f; d.stackMax = 3; d.value = 150;
        d.staminaBoost = 1.5f; d.buffDuration = 10.0f;
        RegisterItem(d);
    }

    // ---- Armor ----
    {
        ItemDef d;
        d.itemID = 4001; d.itemName = "Combat Helmet"; d.description = "Level 2 ballistic helmet";
        d.type = ItemType::Armor; d.rarity = ItemRarity::Uncommon;
        d.equipSlot = EquipSlot::Head;
        d.weight = 1.5f; d.stackMax = 1; d.value = 300;
        d.armor = 15.0f; d.maxDurability = 100;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 4002; d.itemName = "Tactical Vest"; d.description = "Kevlar body armor";
        d.type = ItemType::Armor; d.rarity = ItemRarity::Uncommon;
        d.equipSlot = EquipSlot::Chest;
        d.weight = 4.0f; d.stackMax = 1; d.value = 500;
        d.armor = 30.0f; d.maxDurability = 200;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 4003; d.itemName = "Combat Pants"; d.description = "Reinforced tactical pants";
        d.type = ItemType::Armor; d.rarity = ItemRarity::Common;
        d.equipSlot = EquipSlot::Legs;
        d.weight = 2.0f; d.stackMax = 1; d.value = 200;
        d.armor = 10.0f; d.maxDurability = 150;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 4004; d.itemName = "Tactical Gloves"; d.description = "Cut-resistant gloves";
        d.type = ItemType::Armor; d.rarity = ItemRarity::Common;
        d.equipSlot = EquipSlot::Hands;
        d.weight = 0.3f; d.stackMax = 1; d.value = 80;
        d.armor = 5.0f; d.maxDurability = 80;
        RegisterItem(d);
    }

    // ---- Key Items ----
    {
        ItemDef d;
        d.itemID = 5001; d.itemName = "Red Keycard"; d.description = "Opens red-locked doors";
        d.type = ItemType::KeyItem; d.rarity = ItemRarity::Rare;
        d.weight = 0.05f; d.stackMax = 1; d.value = 0;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 5002; d.itemName = "Blue Keycard"; d.description = "Opens blue-locked doors";
        d.type = ItemType::KeyItem; d.rarity = ItemRarity::Rare;
        d.weight = 0.05f; d.stackMax = 1; d.value = 0;
        RegisterItem(d);
    }

    // ---- Materials ----
    {
        ItemDef d;
        d.itemID = 6001; d.itemName = "Scrap Metal"; d.description = "Can be used for repairs";
        d.type = ItemType::Material; d.rarity = ItemRarity::Common;
        d.weight = 0.5f; d.stackMax = 20; d.value = 10;
        RegisterItem(d);
    }
    {
        ItemDef d;
        d.itemID = 6002; d.itemName = "Cloth"; d.description = "Useful for bandages and repairs";
        d.type = ItemType::Material; d.rarity = ItemRarity::Common;
        d.weight = 0.2f; d.stackMax = 30; d.value = 5;
        RegisterItem(d);
    }

    LOG_INFO("ItemDatabase: Registered %d item definitions", (int)m_defs.size());
}

// ============================================================
// LootTableRegistry
// ============================================================
void LootTableRegistry::Init() {
    m_tables.clear();

    // ---- Basic crate loot ----
    {
        LootTable t;
        t.name = "crate_common";
        t.minDrops = 1; t.maxDrops = 3;
        t.entries.push_back({ 2001, 10, 30, 40.0f });  // Rifle ammo
        t.entries.push_back({ 2002, 10, 20, 30.0f });  // Pistol ammo
        t.entries.push_back({ 3002, 1, 3,   20.0f });  // Bandages
        t.entries.push_back({ 3001, 1, 1,   10.0f });  // Medkit
        RegisterTable(t);
    }

    // ---- Military crate ----
    {
        LootTable t;
        t.name = "crate_military";
        t.minDrops = 2; t.maxDrops = 4;
        t.entries.push_back({ 2001, 20, 60, 30.0f });  // Rifle ammo
        t.entries.push_back({ 2003, 10, 30, 15.0f });  // Heavy ammo
        t.entries.push_back({ 3001, 1, 2,   20.0f });  // Medkit
        t.entries.push_back({ 4001, 1, 1,   10.0f });  // Combat Helmet
        t.entries.push_back({ 4002, 1, 1,   8.0f });   // Tactical Vest
        t.entries.push_back({ 1003, 1, 1,   5.0f });   // SG553
        t.entries.push_back({ 3003, 1, 1,   5.0f });   // Adrenaline
        RegisterTable(t);
    }

    // ---- Rare supply drop ----
    {
        LootTable t;
        t.name = "supply_drop";
        t.minDrops = 3; t.maxDrops = 5;
        t.entries.push_back({ 1004, 1, 1,  15.0f });   // M4A1
        t.entries.push_back({ 1005, 1, 1,  10.0f });   // Heavy Rifle
        t.entries.push_back({ 4002, 1, 1,  12.0f });   // Tactical Vest
        t.entries.push_back({ 4001, 1, 1,  15.0f });   // Combat Helmet
        t.entries.push_back({ 3001, 1, 3,  20.0f });   // Medkit
        t.entries.push_back({ 3003, 1, 2,  10.0f });   // Adrenaline
        t.entries.push_back({ 2001, 30, 90, 25.0f });  // Rifle Ammo
        RegisterTable(t);
    }

    LOG_INFO("LootTableRegistry: Registered %d loot tables", (int)m_tables.size());
}

std::vector<std::pair<int, int>> LootTableRegistry::RollLoot(const std::string& tableName) const {
    std::vector<std::pair<int, int>> result;
    const LootTable* table = GetTable(tableName);
    if (!table || table->entries.empty()) return result;

    // Compute total weight
    float totalWeight = 0.0f;
    for (auto& e : table->entries) totalWeight += e.weight;
    if (totalWeight <= 0.0f) return result;

    // Determine number of drops
    int numDrops = table->minDrops;
    if (table->maxDrops > table->minDrops)
        numDrops += (rand() % (table->maxDrops - table->minDrops + 1));

    for (int d = 0; d < numDrops; d++) {
        float roll = (float)(rand() % 10000) / 10000.0f * totalWeight;
        float cumulative = 0.0f;
        for (auto& e : table->entries) {
            cumulative += e.weight;
            if (roll <= cumulative) {
                int count = e.minCount;
                if (e.maxCount > e.minCount)
                    count += rand() % (e.maxCount - e.minCount + 1);
                result.push_back({ e.itemID, count });
                break;
            }
        }
    }

    return result;
}

} // namespace WT
