#include "InventoryUI.h"
#include "WeaponSystem.h"
#include "Core/Character.h"
#include "Core/Input.h"
#include "../Util/Log.h"

#include "imgui.h"

#include <cstdio>
#include <algorithm>

namespace WT {

void InventoryUI::Init() {
    m_open = false;
    m_dragSlot = -1;
    m_contextMenuSlot = -1;
}

// ============================================================
// Helper: draw a single inventory slot
// Returns true if the slot was clicked
// ============================================================
static bool DrawSlot(const char* id, const ItemInstance& item, float size,
                     bool isEquipSlot = false, const char* label = nullptr) {
    auto& db = ItemDatabase::Get();
    const ItemDef* def = item.IsEmpty() ? nullptr : db.GetDef(item.defID);

    ImGui::PushID(id);

    // Slot background
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Rarity-colored border for filled slots
    ImU32 bgCol = IM_COL32(30, 30, 30, 200);
    ImU32 borderCol = IM_COL32(80, 80, 80, 255);

    if (def) {
        float rc[4];
        GetRarityColor(def->rarity, rc);
        borderCol = IM_COL32((int)(rc[0]*255), (int)(rc[1]*255), (int)(rc[2]*255), 255);
        bgCol = IM_COL32((int)(rc[0]*40), (int)(rc[1]*40), (int)(rc[2]*40), 200);
    }

    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), bgCol, 4.0f);
    dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), borderCol, 4.0f, 0, 2.0f);

    // Slot label (for equipment slots)
    if (label && !def) {
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(pos.x + (size - labelSize.x) * 0.5f,
                           pos.y + (size - labelSize.y) * 0.5f),
                    IM_COL32(100, 100, 100, 180), label);
    }

    // Item name/icon text
    if (def) {
        // Item type icon character
        const char* icon = "?";
        switch (def->type) {
            case ItemType::Weapon:     icon = "W"; break;
            case ItemType::Ammo:       icon = "A"; break;
            case ItemType::Consumable: icon = "+"; break;
            case ItemType::Armor:      icon = "S"; break;
            case ItemType::Material:   icon = "M"; break;
            case ItemType::KeyItem:    icon = "K"; break;
            case ItemType::Tool:       icon = "T"; break;
            default: break;
        }
        // Large center icon
        ImGui::PushFont(nullptr);
        ImVec2 iconSize = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(pos.x + (size - iconSize.x) * 0.5f,
                           pos.y + 4.0f),
                    IM_COL32(255, 255, 255, 220), icon);
        ImGui::PopFont();

        // Item short name (truncated)
        std::string shortName = def->itemName;
        if (shortName.length() > 8) shortName = shortName.substr(0, 7) + "..";
        ImVec2 ts = ImGui::CalcTextSize(shortName.c_str());
        dl->AddText(ImVec2(pos.x + (size - ts.x) * 0.5f,
                           pos.y + size - 28.0f),
                    IM_COL32(200, 200, 200, 255), shortName.c_str());

        // Stack count (bottom-right)
        if (item.stackCount > 1) {
            char buf[16];
            snprintf(buf, sizeof(buf), "x%d", item.stackCount);
            ImVec2 cs = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(pos.x + size - cs.x - 3.0f,
                               pos.y + size - cs.y - 2.0f),
                        IM_COL32(255, 255, 100, 255), buf);
        }

        // Durability bar (bottom)
        if (def->maxDurability > 0 && item.durability > 0) {
            float durFrac = (float)item.durability / (float)def->maxDurability;
            float barW = size - 8.0f;
            ImU32 durCol = durFrac > 0.5f ? IM_COL32(0, 200, 0, 200)
                         : durFrac > 0.25f ? IM_COL32(200, 200, 0, 200)
                         : IM_COL32(200, 0, 0, 200);
            dl->AddRectFilled(ImVec2(pos.x + 4, pos.y + size - 6),
                              ImVec2(pos.x + 4 + barW * durFrac, pos.y + size - 2),
                              durCol, 1.0f);
        }
    }

    // Invisible button for interaction
    bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));

    // Drag & drop source
    if (!item.IsEmpty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("INV_SLOT", id, strlen(id) + 1);
        if (def) ImGui::Text("%s x%d", def->itemName.c_str(), item.stackCount);
        ImGui::EndDragDropSource();
    }

    ImGui::PopID();
    return clicked;
}

// ============================================================
// Main Draw
// ============================================================
void InventoryUI::Draw(Inventory& inventory, InteractionSystem& interaction,
                       Character& character, WeaponSystem& weapon, const Input& input,
                       int screenWidth, int screenHeight) {
    float sw = (float)screenWidth;
    float sh = (float)screenHeight;

    // Draw hold progress bar (always, even when inventory closed)
    if (interaction.IsHolding()) {
        DrawHoldProgressBar(interaction, sw * 0.5f, sh * 0.5f + 40.0f);
    }

    // Toggle with Tab
    if (input.IsKeyPressed(VK_TAB)) {
        Toggle();
        if (!m_open && interaction.IsContainerOpen())
            interaction.CloseContainer();
    }

    // Close container with Escape
    if (input.IsKeyPressed(VK_ESCAPE) && m_open) {
        m_open = false;
        if (interaction.IsContainerOpen())
            interaction.CloseContainer();
    }

    if (!m_open) return;

    // Darken background
    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(sw, sh), IM_COL32(0, 0, 0, 120));

    // ---- Main inventory window ----
    float invW = (kSlotSize + kSlotPad) * kGridCols + kSlotPad + 20.0f;
    float invH = sh * 0.75f;
    float invX = sw * 0.5f - invW * 0.5f;
    float invY = sh * 0.1f;

    // If container is open, shift inventory right
    bool containerOpen = interaction.IsContainerOpen();
    if (containerOpen) {
        invX = sw * 0.55f;
    }

    ImGui::SetNextWindowPos(ImVec2(invX, invY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(invW, invH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                              ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Inventory", &m_open, flags)) {
        // Weight bar at top
        DrawWeightBar(inventory, 10.0f, ImGui::GetCursorPosY(), invW - 30.0f);
        ImGui::Spacing();

        // Equipment section
        ImGui::SeparatorText("Equipment");
        DrawEquipment(inventory, ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);

        ImGui::Spacing();

        // Backpack grid
        ImGui::SeparatorText("Backpack");
        DrawBackpack(inventory, ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);

        ImGui::Spacing();

        // Quick access bar
        ImGui::SeparatorText("Quick Access (1-5)");
        DrawQuickBar(inventory, ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
    }
    ImGui::End();

    // ---- Container window (if looting) ----
    if (containerOpen) {
        float contW = (kSlotSize + kSlotPad) * 4 + kSlotPad + 20.0f;
        float contX = invX - contW - 20.0f;

        ImGui::SetNextWindowPos(ImVec2(contX, invY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(contW, invH * 0.6f), ImGuiCond_Always);

        bool contOpen = true;
        if (ImGui::Begin("Container", &contOpen,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings)) {
            DrawContainer(inventory, interaction,
                          ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
        }
        ImGui::End();

        if (!contOpen) interaction.CloseContainer();
    }

    // ---- Context menu ----
    if (m_contextMenuSlot >= 0 && m_contextMenuSource == 0) {
        if (ImGui::BeginPopup("##SlotContext")) {
            auto& item = inventory.GetSlot(m_contextMenuSlot);
            if (!item.IsEmpty()) {
                const ItemDef* def = ItemDatabase::Get().GetDef(item.defID);
                if (def) {
                    ImGui::Text("%s", def->itemName.c_str());
                    ImGui::Separator();

                    if (def->type == ItemType::Consumable && ImGui::MenuItem("Use")) {
                        UseConsumable(inventory, m_contextMenuSlot, character, weapon);
                    }
                    if (def->equipSlot != EquipSlot::None && ImGui::MenuItem("Equip")) {
                        inventory.EquipFromBackpack(m_contextMenuSlot);
                    }
                    if (def->stackMax > 1 && item.stackCount > 1 && ImGui::MenuItem("Split Stack")) {
                        inventory.SplitStack(m_contextMenuSlot, item.stackCount / 2);
                    }
                    if (ImGui::MenuItem("Drop")) {
                        // TODO: spawn world entity at player's feet
                        inventory.ClearSlot(m_contextMenuSlot);
                    }
                }
            }
            ImGui::EndPopup();
        } else {
            m_contextMenuSlot = -1;
        }
    }
}

// ============================================================
// Backpack Grid
// ============================================================
void InventoryUI::DrawBackpack(Inventory& inventory, float startX, float startY) {
    for (int i = 0; i < Inventory::kBackpackSlots; i++) {
        int col = i % kGridCols;
        if (col > 0) ImGui::SameLine(0, kSlotPad);

        char id[32];
        snprintf(id, sizeof(id), "bp_%d", i);

        auto& item = inventory.GetSlot(i);
        bool clicked = DrawSlot(id, item, kSlotSize);

        // Right-click context menu
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !item.IsEmpty()) {
            m_contextMenuSlot = i;
            m_contextMenuSource = 0;
            ImGui::OpenPopup("##SlotContext");
        }

        // Double-click: quick equip or use
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !item.IsEmpty()) {
            const ItemDef* def = ItemDatabase::Get().GetDef(item.defID);
            if (def && def->equipSlot != EquipSlot::None) {
                inventory.EquipFromBackpack(i);
            }
        }

        // Tooltip on hover
        if (ImGui::IsItemHovered() && !item.IsEmpty()) {
            DrawItemTooltip(item.defID, item.stackCount, item.durability);
        }

        // Drag-drop target (swap slots)
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INV_SLOT")) {
                const char* srcId = (const char*)payload->Data;
                int srcIdx = -1;
                if (sscanf(srcId, "bp_%d", &srcIdx) == 1 && srcIdx != i) {
                    inventory.SwapSlots(srcIdx, i);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONT_SLOT")) {
                // Take from container
                int contSlot = *(int*)payload->Data;
                // Handled by container logic
            }
            ImGui::EndDragDropTarget();
        }
    }
}

// ============================================================
// Equipment Slots
// ============================================================
void InventoryUI::DrawEquipment(Inventory& inventory, float startX, float startY) {
    const EquipSlot slots[] = {
        EquipSlot::Head, EquipSlot::Chest, EquipSlot::Legs,
        EquipSlot::Hands, EquipSlot::PrimaryWeapon, EquipSlot::SecondaryWeapon
    };

    for (int i = 0; i < 6; i++) {
        if (i > 0) ImGui::SameLine(0, kSlotPad);

        char id[32];
        snprintf(id, sizeof(id), "eq_%d", i);

        auto& item = inventory.GetEquipped(slots[i]);
        DrawSlot(id, item, kSlotSize, true, EquipSlotName(slots[i]));

        // Double-click to unequip
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !item.IsEmpty()) {
            inventory.UnequipToBackpack(slots[i]);
        }

        if (ImGui::IsItemHovered() && !item.IsEmpty()) {
            DrawItemTooltip(item.defID, item.stackCount, item.durability);
        }

        // Drop target: equip from backpack drag
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INV_SLOT")) {
                const char* srcId = (const char*)payload->Data;
                int srcIdx = -1;
                if (sscanf(srcId, "bp_%d", &srcIdx) == 1) {
                    inventory.EquipFromBackpack(srcIdx);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
}

// ============================================================
// Quick Access Bar
// ============================================================
void InventoryUI::DrawQuickBar(Inventory& inventory, float startX, float startY) {
    for (int q = 0; q < Inventory::kQuickSlots; q++) {
        if (q > 0) ImGui::SameLine(0, kSlotPad);

        int bpSlot = inventory.GetQuickSlotMapping(q);
        ItemInstance item;
        if (bpSlot >= 0 && bpSlot < Inventory::kBackpackSlots)
            item = inventory.GetSlot(bpSlot);

        char id[32];
        snprintf(id, sizeof(id), "qs_%d", q);
        char label[8];
        snprintf(label, sizeof(label), "%d", q + 1);

        DrawSlot(id, item, kSlotSize, false, label);

        // Drop target: assign backpack item to quick slot
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INV_SLOT")) {
                const char* srcId = (const char*)payload->Data;
                int srcIdx = -1;
                if (sscanf(srcId, "bp_%d", &srcIdx) == 1) {
                    inventory.AssignQuickSlot(q, srcIdx);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::IsItemHovered() && !item.IsEmpty()) {
            DrawItemTooltip(item.defID, item.stackCount, item.durability);
        }
    }
}

// ============================================================
// Container UI
// ============================================================
void InventoryUI::DrawContainer(Inventory& inventory, InteractionSystem& interaction,
                                float startX, float startY) {
    auto& items = interaction.GetContainerItems();
    if (items.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Empty");
        return;
    }

    ImGui::Text("Items: %d", (int)items.size());
    ImGui::Separator();

    // Take All button
    if (ImGui::Button("Take All")) {
        for (int i = (int)items.size() - 1; i >= 0; i--) {
            inventory.TakeFromContainer(items, i);
        }
        // Remove empty entries
        items.erase(std::remove_if(items.begin(), items.end(),
                    [](const ItemInstance& it) { return it.IsEmpty(); }), items.end());
    }
    ImGui::Separator();

    for (int i = 0; i < (int)items.size(); i++) {
        if (items[i].IsEmpty()) continue;

        int col = i % 4;
        if (col > 0) ImGui::SameLine(0, kSlotPad);

        char id[32];
        snprintf(id, sizeof(id), "cont_%d", i);

        DrawSlot(id, items[i], kSlotSize);

        // Click to take
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            inventory.TakeFromContainer(items, i);
        }

        if (ImGui::IsItemHovered() && !items[i].IsEmpty()) {
            DrawItemTooltip(items[i].defID, items[i].stackCount, items[i].durability);
        }
    }

    // Clean up empty slots
    items.erase(std::remove_if(items.begin(), items.end(),
                [](const ItemInstance& it) { return it.IsEmpty(); }), items.end());
}

// ============================================================
// Item Tooltip
// ============================================================
void InventoryUI::DrawItemTooltip(int itemDefID, int stackCount, int durability) {
    const ItemDef* def = ItemDatabase::Get().GetDef(itemDefID);
    if (!def) return;

    ImGui::BeginTooltip();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));

    // Name with rarity color
    float rc[4];
    GetRarityColor(def->rarity, rc);
    ImGui::TextColored(ImVec4(rc[0], rc[1], rc[2], 1), "%s", def->itemName.c_str());
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "%s - %s",
                       ItemTypeName(def->type), ItemRarityName(def->rarity));

    ImGui::Separator();

    if (!def->description.empty())
        ImGui::TextWrapped("%s", def->description.c_str());

    // Stats
    if (def->damage > 0)
        ImGui::Text("Damage: %.0f", def->damage);
    if (def->armor > 0)
        ImGui::Text("Armor: %.0f", def->armor);
    if (def->healAmount > 0)
        ImGui::Text("Heal: %.0f HP", def->healAmount);
    if (def->staminaBoost > 0)
        ImGui::Text("Speed: +%.0f%%", (def->staminaBoost - 1.0f) * 100.0f);
    if (def->buffDuration > 0)
        ImGui::Text("Duration: %.1f sec", def->buffDuration);

    // Durability
    if (def->maxDurability > 0 && durability > 0) {
        ImGui::Text("Durability: %d / %d", durability, def->maxDurability);
    }

    // Weight
    ImGui::Text("Weight: %.1f kg", def->weight * stackCount);
    if (stackCount > 1)
        ImGui::Text("Stack: %d / %d", stackCount, def->stackMax);
    if (def->value > 0)
        ImGui::Text("Value: %d", def->value * stackCount);

    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

// ============================================================
// Weight Bar
// ============================================================
void InventoryUI::DrawWeightBar(const Inventory& inventory, float x, float y, float width) {
    float frac = inventory.GetWeightFraction();
    float curW = inventory.GetCurrentWeight();
    float maxW = inventory.GetMaxWeight();

    char buf[64];
    snprintf(buf, sizeof(buf), "Weight: %.1f / %.1f kg", curW, maxW);

    ImU32 barCol = frac < 0.7f ? IM_COL32(80, 180, 80, 200)
                 : frac < 0.9f ? IM_COL32(200, 200, 50, 200)
                 : IM_COL32(220, 50, 50, 200);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wPos = ImGui::GetWindowPos();
    float bx = wPos.x + x;
    float by = wPos.y + y;

    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + width, by + 18), IM_COL32(20, 20, 20, 180), 3.0f);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + width * (std::min)(frac, 1.0f), by + 18), barCol, 3.0f);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + width, by + 18), IM_COL32(100, 100, 100, 200), 3.0f);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(bx + (width - ts.x) * 0.5f, by + 1.0f),
                IM_COL32(255, 255, 255, 220), buf);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 22.0f);
}

// ============================================================
// Hold Progress Bar (world-space, shown near crosshair)
// ============================================================
void InventoryUI::DrawHoldProgressBar(const InteractionSystem& interaction,
                                      float cx, float cy) {
    float progress = interaction.GetHoldProgress();
    if (progress <= 0.0f) return;

    float barW = 120.0f;
    float barH = 8.0f;
    float bx = cx - barW * 0.5f;
    float by = cy;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH),
                      IM_COL32(0, 0, 0, 160), 4.0f);
    dl->AddRectFilled(ImVec2(bx + 1, by + 1),
                      ImVec2(bx + 1 + (barW - 2) * progress, by + barH - 1),
                      IM_COL32(100, 200, 255, 220), 3.0f);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + barW, by + barH),
                IM_COL32(150, 150, 150, 200), 4.0f);
}

// ============================================================
// Use Consumable
// ============================================================
void InventoryUI::UseConsumable(Inventory& inventory, int backpackSlot,
                                Character& character, WeaponSystem& weapon) {
    inventory.UseItem(backpackSlot, [&](const ItemDef& def, const ItemInstance& item) {
        if (def.healAmount > 0.0f) {
            character.Heal(def.healAmount);
            LOG_INFO("Used %s: healed %.0f HP", def.itemName.c_str(), def.healAmount);
        }
        // Speed boost would be handled by a buff system (future)
    });
}

} // namespace WT
