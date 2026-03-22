#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace WT {

// ============================================================
// Item Type Categories
// ============================================================
enum class ItemType : uint8_t {
    None = 0,
    Weapon,       // Equippable weapon
    Ammo,         // Ammunition (stackable)
    Consumable,   // Health packs, boosts (stackable, usable)
    Armor,        // Equippable armor piece
    Material,     // Crafting material (stackable)
    KeyItem,      // Quest/key items (non-stackable, non-droppable)
    Tool,         // Utility tools
    Count
};

inline const char* ItemTypeName(ItemType t) {
    switch (t) {
        case ItemType::Weapon:     return "Weapon";
        case ItemType::Ammo:       return "Ammo";
        case ItemType::Consumable: return "Consumable";
        case ItemType::Armor:      return "Armor";
        case ItemType::Material:   return "Material";
        case ItemType::KeyItem:    return "Key Item";
        case ItemType::Tool:       return "Tool";
        default:                   return "None";
    }
}

// ============================================================
// Item Rarity
// ============================================================
enum class ItemRarity : uint8_t {
    Common = 0,
    Uncommon,
    Rare,
    Epic,
    Legendary,
    Count
};

inline const char* ItemRarityName(ItemRarity r) {
    switch (r) {
        case ItemRarity::Common:    return "Common";
        case ItemRarity::Uncommon:  return "Uncommon";
        case ItemRarity::Rare:      return "Rare";
        case ItemRarity::Epic:      return "Epic";
        case ItemRarity::Legendary: return "Legendary";
        default:                    return "Unknown";
    }
}

// Rarity colors (RGBA floats) for UI
inline void GetRarityColor(ItemRarity r, float out[4]) {
    switch (r) {
        case ItemRarity::Common:    out[0]=0.7f; out[1]=0.7f; out[2]=0.7f; out[3]=1.0f; break;
        case ItemRarity::Uncommon:  out[0]=0.2f; out[1]=0.8f; out[2]=0.2f; out[3]=1.0f; break;
        case ItemRarity::Rare:      out[0]=0.3f; out[1]=0.5f; out[2]=1.0f; out[3]=1.0f; break;
        case ItemRarity::Epic:      out[0]=0.7f; out[1]=0.3f; out[2]=0.9f; out[3]=1.0f; break;
        case ItemRarity::Legendary: out[0]=1.0f; out[1]=0.8f; out[2]=0.0f; out[3]=1.0f; break;
        default:                    out[0]=0.5f; out[1]=0.5f; out[2]=0.5f; out[3]=1.0f; break;
    }
}

// ============================================================
// Equipment Slot
// ============================================================
enum class EquipSlot : uint8_t {
    None = 0,       // Not equippable
    Head,
    Chest,
    Legs,
    Hands,
    PrimaryWeapon,
    SecondaryWeapon,
    Count
};

inline const char* EquipSlotName(EquipSlot s) {
    switch (s) {
        case EquipSlot::Head:            return "Head";
        case EquipSlot::Chest:           return "Chest";
        case EquipSlot::Legs:            return "Legs";
        case EquipSlot::Hands:           return "Hands";
        case EquipSlot::PrimaryWeapon:   return "Primary";
        case EquipSlot::SecondaryWeapon: return "Secondary";
        default:                         return "None";
    }
}

// ============================================================
// Item Definition (static data — loaded once)
// ============================================================
struct ItemDef {
    int         itemID      = 0;        // Unique item ID
    std::string itemName    = "Unknown";
    std::string description;
    ItemType    type        = ItemType::None;
    ItemRarity  rarity      = ItemRarity::Common;
    EquipSlot   equipSlot   = EquipSlot::None;
    float       weight      = 0.1f;     // kg per unit
    int         stackMax    = 1;        // Max stack size (1 = non-stackable)
    int         value       = 0;        // Base monetary value

    // Icon texture name (for UI display)
    std::string iconName;

    // World model (for dropped items in scene)
    std::string worldModelName;
    std::string worldTextureName;

    // Type-specific stats
    float       damage      = 0.0f;     // Weapon damage / heal amount for consumables
    float       armor       = 0.0f;     // Armor defense value
    int         maxDurability = 0;      // 0 = indestructible
    int         ammoPerUnit = 1;        // Ammo: how many rounds per stack unit

    // Weapon link: if this is a weapon item, which WeaponType does it map to?
    int         weaponTypeIndex = -1;   // Maps to WeaponType enum (-1 = N/A)

    // Consumable: effect applied on use
    float       healAmount   = 0.0f;
    float       staminaBoost = 0.0f;
    float       buffDuration = 0.0f;

    // Conditions: required item ID to use/pick up (0 = none)
    int         requiresItemID = 0;
};

// ============================================================
// Item Instance (runtime — lives in inventory slots)
// ============================================================
struct ItemInstance {
    int   defID       = 0;    // References ItemDef::itemID
    int   stackCount  = 1;    // Current stack
    int   durability  = -1;   // -1 = uses maxDurability from def, or current durability

    bool IsEmpty() const { return defID == 0; }
    void Clear() { defID = 0; stackCount = 0; durability = -1; }
};

// ============================================================
// ItemDatabase — global registry of all item definitions
// ============================================================
class ItemDatabase {
public:
    static ItemDatabase& Get() {
        static ItemDatabase db;
        return db;
    }

    void Init();

    const ItemDef* GetDef(int itemID) const {
        auto it = m_defs.find(itemID);
        return (it != m_defs.end()) ? &it->second : nullptr;
    }

    const std::unordered_map<int, ItemDef>& GetAllDefs() const { return m_defs; }

    void RegisterItem(const ItemDef& def) { m_defs[def.itemID] = def; }

private:
    ItemDatabase() = default;
    std::unordered_map<int, ItemDef> m_defs;
};

// ============================================================
// Loot Table Entry
// ============================================================
struct LootEntry {
    int   itemID     = 0;
    int   minCount   = 1;
    int   maxCount   = 1;
    float weight     = 1.0f;   // Relative probability weight
};

struct LootTable {
    std::string        name;
    std::vector<LootEntry> entries;
    int                minDrops = 1;   // Min items from this table
    int                maxDrops = 3;   // Max items from this table
};

// ============================================================
// Loot Table Registry
// ============================================================
class LootTableRegistry {
public:
    static LootTableRegistry& Get() {
        static LootTableRegistry reg;
        return reg;
    }

    void Init();

    const LootTable* GetTable(const std::string& name) const {
        auto it = m_tables.find(name);
        return (it != m_tables.end()) ? &it->second : nullptr;
    }

    // Roll loot from a named table, returns list of (itemID, count)
    std::vector<std::pair<int, int>> RollLoot(const std::string& tableName) const;

    void RegisterTable(const LootTable& table) { m_tables[table.name] = table; }

private:
    LootTableRegistry() = default;
    std::unordered_map<std::string, LootTable> m_tables;
};

} // namespace WT
