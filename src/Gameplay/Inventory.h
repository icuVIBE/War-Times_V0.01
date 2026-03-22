#pragma once

#include "ItemSystem.h"
#include <array>
#include <vector>
#include <string>
#include <functional>

namespace WT {

// ============================================================
// Inventory — manages player item slots, equipment, hotbar
// ============================================================
class Inventory {
public:
    // Slot counts
    static constexpr int kBackpackSlots = 24;       // 6 columns x 4 rows
    static constexpr int kQuickSlots    = 5;        // 1-5 hotbar
    static constexpr int kEquipSlotCount = static_cast<int>(EquipSlot::Count) - 1; // Exclude None

    void Init();
    void Clear();

    // ---- Backpack Operations ----
    // Returns how many items couldn't be added (0 = all added)
    int  AddItem(int itemID, int count = 1);
    // Remove items by ID, returns how many actually removed
    int  RemoveItem(int itemID, int count = 1);
    // Check if player has at least `count` of itemID
    bool HasItem(int itemID, int count = 1) const;
    // Count total of itemID across all slots
    int  CountItem(int itemID) const;

    // Direct slot operations
    const ItemInstance& GetSlot(int index) const { return m_backpack[index]; }
    ItemInstance&       GetSlot(int index)       { return m_backpack[index]; }
    void ClearSlot(int index) { m_backpack[index].Clear(); }
    int  GetBackpackSize() const { return kBackpackSlots; }

    // Swap two backpack slots
    void SwapSlots(int a, int b);
    // Split stack: move `count` items from slot `from` into first empty slot
    bool SplitStack(int fromSlot, int count);

    // ---- Equipment Slots ----
    // Equip item from backpack slot to the correct equipment slot
    bool EquipFromBackpack(int backpackSlot);
    // Unequip from equipment slot back to backpack
    bool UnequipToBackpack(EquipSlot slot);
    // Get equipped item in a slot
    const ItemInstance& GetEquipped(EquipSlot slot) const;
    ItemInstance& GetEquipped(EquipSlot slot);

    // ---- Quick Access / Hotbar ----
    // Assign a backpack slot to a quick slot (0-4)
    void AssignQuickSlot(int quickIdx, int backpackSlot);
    int  GetQuickSlotMapping(int quickIdx) const { return m_quickSlotMap[quickIdx]; }
    // Use the item in quick slot (consumables)
    bool UseQuickSlot(int quickIdx);

    // ---- Weight System ----
    float GetCurrentWeight() const;
    float GetMaxWeight() const { return m_maxCarryWeight; }
    void  SetMaxWeight(float w) { m_maxCarryWeight = w; }
    bool  IsOverweight() const { return GetCurrentWeight() > m_maxCarryWeight; }
    float GetWeightFraction() const;

    // ---- Container Interaction ----
    // Transfer item from a container's item list into inventory
    int  TakeFromContainer(std::vector<ItemInstance>& container, int containerSlot);
    // Put item from backpack into container
    bool PutInContainer(std::vector<ItemInstance>& container, int backpackSlot);

    // ---- Use Consumable ----
    // Returns true if item was used (applies effect via callback)
    using UseCallback = std::function<void(const ItemDef& def, const ItemInstance& item)>;
    bool UseItem(int backpackSlot, UseCallback callback);

    // ---- Persistence ----
    void Serialize(std::ostream& out) const;
    bool Deserialize(std::istream& in);

    // Find first backpack slot containing itemID, or -1
    int FindSlot(int itemID) const;

private:
    int FindEmptySlot() const;
    int EquipSlotIndex(EquipSlot slot) const { return static_cast<int>(slot) - 1; }

    std::array<ItemInstance, kBackpackSlots> m_backpack = {};
    std::array<ItemInstance, kEquipSlotCount> m_equipment = {};
    std::array<int, kQuickSlots> m_quickSlotMap = { -1, -1, -1, -1, -1 }; // Backpack index or -1
    float m_maxCarryWeight = 40.0f;  // kg
};

} // namespace WT
