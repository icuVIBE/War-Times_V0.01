#pragma once

#include "Inventory.h"
#include "InteractionSystem.h"

namespace WT {

class Character;
class WeaponSystem;
class Input;

// ============================================================
// InventoryUI — Full ImGui inventory screen
// Tab to toggle, drag-and-drop, equipment, container loot
// ============================================================
class InventoryUI {
public:
    void Init();

    // Main draw call (inside ImGui frame)
    void Draw(Inventory& inventory, InteractionSystem& interaction,
              Character& character, WeaponSystem& weapon, const Input& input,
              int screenWidth, int screenHeight);

    // Toggle and state
    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }
    void Toggle() { m_open = !m_open; }

private:
    void DrawBackpack(Inventory& inventory, float startX, float startY);
    void DrawEquipment(Inventory& inventory, float startX, float startY);
    void DrawQuickBar(Inventory& inventory, float startX, float startY);
    void DrawContainer(Inventory& inventory, InteractionSystem& interaction,
                       float startX, float startY);
    void DrawItemTooltip(int itemDefID, int stackCount, int durability);
    void DrawWeightBar(const Inventory& inventory, float x, float y, float width);
    void DrawHoldProgressBar(const InteractionSystem& interaction,
                             float cx, float cy);

    // Apply consumable effects
    void UseConsumable(Inventory& inventory, int backpackSlot,
                       Character& character, WeaponSystem& weapon);

    bool m_open = false;
    int  m_dragSlot = -1;              // Backpack slot being dragged (-1 = none)
    int  m_dragSource = 0;             // 0=backpack, 1=container, 2=equipment
    int  m_contextMenuSlot = -1;       // Right-click context menu
    int  m_contextMenuSource = 0;

    // Slot sizing
    static constexpr float kSlotSize = 56.0f;
    static constexpr float kSlotPad  = 4.0f;
    static constexpr int   kGridCols = 6;
};

} // namespace WT
