#include "Inventory.h"
#include "../Util/Log.h"
#include <algorithm>
#include <sstream>
#include <string>

namespace WT {

void Inventory::Init() {
    Clear();
}

void Inventory::Clear() {
    for (auto& s : m_backpack) s.Clear();
    for (auto& s : m_equipment) s.Clear();
    m_quickSlotMap.fill(-1);
}

// ============================================================
// AddItem — auto-stack into existing stacks, then empty slots
// ============================================================
int Inventory::AddItem(int itemID, int count) {
    if (count <= 0) return 0;
    const ItemDef* def = ItemDatabase::Get().GetDef(itemID);
    if (!def) return count;  // Unknown item

    int remaining = count;

    // Phase 1: Stack into existing partial stacks
    if (def->stackMax > 1) {
        for (int i = 0; i < kBackpackSlots && remaining > 0; i++) {
            auto& s = m_backpack[i];
            if (s.defID == itemID && s.stackCount < def->stackMax) {
                int space = def->stackMax - s.stackCount;
                int add = (std::min)(remaining, space);
                s.stackCount += add;
                remaining -= add;
            }
        }
    }

    // Phase 2: Fill empty slots
    while (remaining > 0) {
        int idx = FindEmptySlot();
        if (idx < 0) break;  // Inventory full

        int perSlot = (std::min)(remaining, def->stackMax);
        m_backpack[idx].defID = itemID;
        m_backpack[idx].stackCount = perSlot;
        m_backpack[idx].durability = def->maxDurability > 0 ? def->maxDurability : -1;
        remaining -= perSlot;
    }

    if (remaining > 0) {
        LOG_WARN("Inventory: Couldn't fit %d of item '%s' (ID %d)", remaining, def->itemName.c_str(), itemID);
    }

    return remaining;
}

int Inventory::RemoveItem(int itemID, int count) {
    int toRemove = count;

    // Remove from last slots first (LIFO)
    for (int i = kBackpackSlots - 1; i >= 0 && toRemove > 0; i--) {
        auto& s = m_backpack[i];
        if (s.defID != itemID) continue;

        int take = (std::min)(toRemove, s.stackCount);
        s.stackCount -= take;
        toRemove -= take;
        if (s.stackCount <= 0) s.Clear();
    }

    return count - toRemove;
}

bool Inventory::HasItem(int itemID, int count) const {
    return CountItem(itemID) >= count;
}

int Inventory::CountItem(int itemID) const {
    int total = 0;
    for (auto& s : m_backpack)
        if (s.defID == itemID) total += s.stackCount;
    for (auto& s : m_equipment)
        if (s.defID == itemID) total += s.stackCount;
    return total;
}

void Inventory::SwapSlots(int a, int b) {
    if (a < 0 || a >= kBackpackSlots || b < 0 || b >= kBackpackSlots) return;
    std::swap(m_backpack[a], m_backpack[b]);
}

bool Inventory::SplitStack(int fromSlot, int count) {
    if (fromSlot < 0 || fromSlot >= kBackpackSlots) return false;
    auto& src = m_backpack[fromSlot];
    if (src.IsEmpty() || count <= 0 || count >= src.stackCount) return false;

    int emptyIdx = FindEmptySlot();
    if (emptyIdx < 0) return false;

    m_backpack[emptyIdx].defID = src.defID;
    m_backpack[emptyIdx].stackCount = count;
    m_backpack[emptyIdx].durability = src.durability;
    src.stackCount -= count;

    return true;
}

// ============================================================
// Equipment
// ============================================================
bool Inventory::EquipFromBackpack(int backpackSlot) {
    if (backpackSlot < 0 || backpackSlot >= kBackpackSlots) return false;
    auto& src = m_backpack[backpackSlot];
    if (src.IsEmpty()) return false;

    const ItemDef* def = ItemDatabase::Get().GetDef(src.defID);
    if (!def || def->equipSlot == EquipSlot::None) return false;

    int eqIdx = EquipSlotIndex(def->equipSlot);
    if (eqIdx < 0 || eqIdx >= kEquipSlotCount) return false;

    // Swap with currently equipped item (if any)
    std::swap(m_equipment[eqIdx], m_backpack[backpackSlot]);

    return true;
}

bool Inventory::UnequipToBackpack(EquipSlot slot) {
    int eqIdx = EquipSlotIndex(slot);
    if (eqIdx < 0 || eqIdx >= kEquipSlotCount) return false;
    if (m_equipment[eqIdx].IsEmpty()) return false;

    int emptyIdx = FindEmptySlot();
    if (emptyIdx < 0) return false;  // No room

    m_backpack[emptyIdx] = m_equipment[eqIdx];
    m_equipment[eqIdx].Clear();
    return true;
}

const ItemInstance& Inventory::GetEquipped(EquipSlot slot) const {
    static const ItemInstance empty;
    int idx = EquipSlotIndex(slot);
    if (idx < 0 || idx >= kEquipSlotCount) return empty;
    return m_equipment[idx];
}

ItemInstance& Inventory::GetEquipped(EquipSlot slot) {
    static ItemInstance empty;
    int idx = EquipSlotIndex(slot);
    if (idx < 0 || idx >= kEquipSlotCount) { empty.Clear(); return empty; }
    return m_equipment[idx];
}

// ============================================================
// Quick Access
// ============================================================
void Inventory::AssignQuickSlot(int quickIdx, int backpackSlot) {
    if (quickIdx < 0 || quickIdx >= kQuickSlots) return;
    m_quickSlotMap[quickIdx] = backpackSlot;
}

bool Inventory::UseQuickSlot(int quickIdx) {
    if (quickIdx < 0 || quickIdx >= kQuickSlots) return false;
    int bpSlot = m_quickSlotMap[quickIdx];
    if (bpSlot < 0 || bpSlot >= kBackpackSlots) return false;
    // Will be consumed via UseItem
    return true;
}

// ============================================================
// Weight
// ============================================================
float Inventory::GetCurrentWeight() const {
    float total = 0.0f;
    auto& db = ItemDatabase::Get();
    for (auto& s : m_backpack) {
        if (s.IsEmpty()) continue;
        const ItemDef* def = db.GetDef(s.defID);
        if (def) total += def->weight * s.stackCount;
    }
    for (auto& s : m_equipment) {
        if (s.IsEmpty()) continue;
        const ItemDef* def = db.GetDef(s.defID);
        if (def) total += def->weight * s.stackCount;
    }
    return total;
}

float Inventory::GetWeightFraction() const {
    if (m_maxCarryWeight <= 0.0f) return 0.0f;
    return GetCurrentWeight() / m_maxCarryWeight;
}

// ============================================================
// Container Operations
// ============================================================
int Inventory::TakeFromContainer(std::vector<ItemInstance>& container, int containerSlot) {
    if (containerSlot < 0 || containerSlot >= (int)container.size()) return 0;
    auto& src = container[containerSlot];
    if (src.IsEmpty()) return 0;

    int leftover = AddItem(src.defID, src.stackCount);
    int taken = src.stackCount - leftover;

    if (leftover == 0) {
        src.Clear();
    } else {
        src.stackCount = leftover;
    }

    return taken;
}

bool Inventory::PutInContainer(std::vector<ItemInstance>& container, int backpackSlot) {
    if (backpackSlot < 0 || backpackSlot >= kBackpackSlots) return false;
    auto& src = m_backpack[backpackSlot];
    if (src.IsEmpty()) return false;

    // Find empty slot in container or a matching stack
    const ItemDef* def = ItemDatabase::Get().GetDef(src.defID);
    if (!def) return false;

    // Try stacking first
    if (def->stackMax > 1) {
        for (auto& cs : container) {
            if (cs.defID == src.defID && cs.stackCount < def->stackMax) {
                int space = def->stackMax - cs.stackCount;
                int add = (std::min)(src.stackCount, space);
                cs.stackCount += add;
                src.stackCount -= add;
                if (src.stackCount <= 0) { src.Clear(); return true; }
            }
        }
    }

    // Put in empty container slot
    for (auto& cs : container) {
        if (cs.IsEmpty()) {
            cs = src;
            src.Clear();
            return true;
        }
    }

    return false;  // Container full
}

// ============================================================
// Use Consumable
// ============================================================
bool Inventory::UseItem(int backpackSlot, UseCallback callback) {
    if (backpackSlot < 0 || backpackSlot >= kBackpackSlots) return false;
    auto& item = m_backpack[backpackSlot];
    if (item.IsEmpty()) return false;

    const ItemDef* def = ItemDatabase::Get().GetDef(item.defID);
    if (!def || def->type != ItemType::Consumable) return false;

    callback(*def, item);

    item.stackCount--;
    if (item.stackCount <= 0) item.Clear();

    return true;
}

// ============================================================
// Persistence
// ============================================================
void Inventory::Serialize(std::ostream& out) const {
    out << "[inventory]\n";
    out << "max_weight = " << m_maxCarryWeight << "\n";

    // Backpack
    for (int i = 0; i < kBackpackSlots; i++) {
        if (m_backpack[i].IsEmpty()) continue;
        out << "bp " << i << " " << m_backpack[i].defID << " "
            << m_backpack[i].stackCount << " " << m_backpack[i].durability << "\n";
    }

    // Equipment
    for (int i = 0; i < kEquipSlotCount; i++) {
        if (m_equipment[i].IsEmpty()) continue;
        out << "eq " << i << " " << m_equipment[i].defID << " "
            << m_equipment[i].stackCount << " " << m_equipment[i].durability << "\n";
    }

    // Quick slots
    for (int i = 0; i < kQuickSlots; i++) {
        if (m_quickSlotMap[i] >= 0) {
            out << "qs " << i << " " << m_quickSlotMap[i] << "\n";
        }
    }
    out << "[/inventory]\n";
}

bool Inventory::Deserialize(std::istream& in) {
    Clear();
    std::string line;
    bool inSection = false;
    while (std::getline(in, line)) {
        if (line.find("[inventory]") != std::string::npos) { inSection = true; continue; }
        if (line.find("[/inventory]") != std::string::npos) break;
        if (!inSection) continue;

        if (line.substr(0, 12) == "max_weight =") {
            m_maxCarryWeight = std::stof(line.substr(13));
        } else if (line.substr(0, 3) == "bp ") {
            int idx, defID, stack, dur;
            if (sscanf(line.c_str(), "bp %d %d %d %d", &idx, &defID, &stack, &dur) == 4) {
                if (idx >= 0 && idx < kBackpackSlots) {
                    m_backpack[idx].defID = defID;
                    m_backpack[idx].stackCount = stack;
                    m_backpack[idx].durability = dur;
                }
            }
        } else if (line.substr(0, 3) == "eq ") {
            int idx, defID, stack, dur;
            if (sscanf(line.c_str(), "eq %d %d %d %d", &idx, &defID, &stack, &dur) == 4) {
                if (idx >= 0 && idx < kEquipSlotCount) {
                    m_equipment[idx].defID = defID;
                    m_equipment[idx].stackCount = stack;
                    m_equipment[idx].durability = dur;
                }
            }
        } else if (line.substr(0, 3) == "qs ") {
            int qi, bpIdx;
            if (sscanf(line.c_str(), "qs %d %d", &qi, &bpIdx) == 2) {
                if (qi >= 0 && qi < kQuickSlots)
                    m_quickSlotMap[qi] = bpIdx;
            }
        }
    }
    return true;
}

int Inventory::FindSlot(int itemID) const {
    for (int i = 0; i < kBackpackSlots; i++)
        if (m_backpack[i].defID == itemID) return i;
    return -1;
}

int Inventory::FindEmptySlot() const {
    for (int i = 0; i < kBackpackSlots; i++)
        if (m_backpack[i].IsEmpty()) return i;
    return -1;
}

} // namespace WT
