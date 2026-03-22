// ============================================================
// LevelEditorSections.cpp
// Complete AAA-quality editor panel UI for the Level Editor.
// Tabbed layout: File | Scene | World | Render | Settings
// ============================================================

#include "LevelEditorWindow.h"
#include "EditorPanels.h"
#include "LevelFile.h"
#include "PCG/LevelGenerator.h"
#include "Core/ResourceManager.h"
#include "Util/Log.h"

#include "imgui.h"

// ═══════════════════════════════════════════════════════════════
//  Design System — shared style tokens
// ═══════════════════════════════════════════════════════════════
namespace {

// --- Palette ---
constexpr ImVec4 kBgHeader      = {0.118f, 0.125f, 0.155f, 1.0f};
constexpr ImVec4 kBgHeaderHov   = {0.148f, 0.158f, 0.195f, 1.0f};
constexpr ImVec4 kBgChild       = {0.058f, 0.062f, 0.078f, 1.0f};
constexpr ImVec4 kBgInput       = {0.10f, 0.11f, 0.14f, 1.0f};

constexpr ImVec4 kAccent        = {0.35f, 0.58f, 1.00f, 1.0f};
constexpr ImVec4 kAccentDim     = {0.26f, 0.42f, 0.72f, 1.0f};
constexpr ImVec4 kAccentBg      = {0.14f, 0.20f, 0.34f, 1.0f};

constexpr ImVec4 kTextSecondary = {0.52f, 0.55f, 0.62f, 1.0f};
constexpr ImVec4 kTextMuted     = {0.38f, 0.40f, 0.46f, 1.0f};

constexpr ImVec4 kGreen         = {0.28f, 0.78f, 0.42f, 1.0f};
constexpr ImVec4 kGreenBg       = {0.10f, 0.25f, 0.12f, 1.0f};
constexpr ImVec4 kGreenHov      = {0.14f, 0.32f, 0.16f, 1.0f};
constexpr ImVec4 kYellow        = {0.95f, 0.78f, 0.22f, 1.0f};
constexpr ImVec4 kOrange        = {0.92f, 0.55f, 0.18f, 1.0f};
constexpr ImVec4 kOrangeBg      = {0.30f, 0.18f, 0.06f, 1.0f};
constexpr ImVec4 kOrangeHov     = {0.38f, 0.24f, 0.08f, 1.0f};
constexpr ImVec4 kRed           = {0.92f, 0.28f, 0.28f, 1.0f};
constexpr ImVec4 kRedBg         = {0.30f, 0.08f, 0.08f, 1.0f};
constexpr ImVec4 kRedHov        = {0.42f, 0.12f, 0.12f, 1.0f};

constexpr ImVec4 kAxisX         = {0.90f, 0.22f, 0.22f, 1.0f};
constexpr ImVec4 kAxisY         = {0.22f, 0.82f, 0.22f, 1.0f};
constexpr ImVec4 kAxisZ         = {0.22f, 0.42f, 0.90f, 1.0f};

constexpr float kLabelW = 105.0f;

// --- Helpers ---
void Label(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(-1);
}

void Muted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void ThinSeparator() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.18f, 0.20f, 0.26f, 0.40f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

bool GroupHeader(const char* label, bool defaultOpen = false) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleColor(ImGuiCol_Header,        kBgHeader);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  kBgHeaderHov);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   kBgHeaderHov);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return open;
}

void BeginPropGroup() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::Indent(4.0f);
    ImGui::Spacing();
}

void EndPropGroup() {
    ImGui::Spacing();
    ImGui::Unindent(4.0f);
    ImGui::PopStyleVar();
}

bool AccentButton(const char* label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button,        kAccentBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.18f, 0.26f, 0.44f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.22f, 0.32f, 0.52f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool GreenButton(const char* label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button,        kGreenBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  kGreenHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.08f, 0.20f, 0.10f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool OrangeButton(const char* label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button,        kOrangeBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  kOrangeHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.24f, 0.14f, 0.04f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool DangerButton(const char* label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button,        kRedBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  kRedHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.22f, 0.06f, 0.06f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// Compact toggle button
bool ToolToggle(const char* label, bool active, ImVec2 size = {0, 22}) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        kAccentBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.18f, 0.26f, 0.44f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,           kAccent);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.11f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.14f, 0.15f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,           kTextSecondary);
    }
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════
namespace WT {

// ─────────────────────────────────────────────────────────────
//  TOOL BAR (always visible at top, outside tabs)
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawToolSection(EditorState& state) {
    (void)state;

    // Tool row
    float rowW = ImGui::GetContentRegionAvail().x;
    float btnW = (rowW - 12) / 5.0f;
    const char* names[] = {"Select\nQ", "Move\nG", "Rotate\nR", "Scale\nT", "Place\nP"};

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    for (int i = 0; i < 5; i++) {
        bool active = (m_currentTool == static_cast<LevelEditTool>(i));
        if (ToolToggle(names[i], active, ImVec2(btnW, 32))) {
            m_currentTool = static_cast<LevelEditTool>(i);
            if (i == 1) m_axisConstraint = AxisConstraint::XZ;
            else if (i == 2) m_axisConstraint = AxisConstraint::Y;
            else m_axisConstraint = AxisConstraint::None;
        }
        if (i < 4) ImGui::SameLine(0, 3);
    }
    ImGui::PopStyleVar();

    // Axis constraint (only when relevant)
    if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Rotate ||
        m_currentTool == LevelEditTool::Scale) {
        ImGui::Spacing();
        float axisBtnW = (rowW - 12) / 5.0f;
        struct AB { const char* l; AxisConstraint ac; ImVec4 col; };
        AB btns[] = {
            {"Free",AxisConstraint::None,{0.45f,0.45f,0.5f,1}},
            {"X",   AxisConstraint::X,   kAxisX},
            {"Y",   AxisConstraint::Y,   kAxisY},
            {"Z",   AxisConstraint::Z,   kAxisZ},
            {"XZ",  AxisConstraint::XZ,  {0.55f,0.30f,0.55f,1}}
        };
        for (int i = 0; i < 5; i++) {
            bool act = (m_axisConstraint == btns[i].ac);
            if (act) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(btns[i].col.x*0.35f, btns[i].col.y*0.35f, btns[i].col.z*0.35f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(btns[i].col.x*0.5f, btns[i].col.y*0.5f, btns[i].col.z*0.5f, 1));
                ImGui::PushStyleColor(ImGuiCol_Text, btns[i].col);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f,0.11f,0.14f,1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f,0.15f,0.19f,1));
                ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
            }
            if (ImGui::Button(btns[i].l, ImVec2(axisBtnW, 20)))
                m_axisConstraint = btns[i].ac;
            ImGui::PopStyleColor(3);
            if (i < 4) ImGui::SameLine(0, 3);
        }

        // Tool-specific extras
        if (m_currentTool == LevelEditTool::Rotate) {
            Label("Snap");
            ImGui::Checkbox("##rotsnap", &m_rotationSnapOn);
            if (m_rotationSnapOn) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(60);
                ImGui::DragFloat("##rotsnapval", &m_rotationSnap, 0.5f, 1.0f, 90.0f, "%.0f\xc2\xb0");
            }
        }
        if (m_currentTool == LevelEditTool::Scale) {
            Label("Uniform");
            ImGui::Checkbox("##uniformscl", &m_uniformScale);
            Label("Snap");
            ImGui::Checkbox("##sclsnap", &m_scaleSnapOn);
            if (m_scaleSnapOn) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(60);
                ImGui::DragFloat("##sclsnapval", &m_scaleSnap, 0.025f, 0.05f, 5.0f, "%.2f");
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  TAB: FILE — Level operations + PCG generators
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawLevelSection(EditorState& state) {
    // ── Level File ──
    if (GroupHeader("Level File", true)) {
        BeginPropGroup();

        float rowW = ImGui::GetContentRegionAvail().x;
        float btnW = (rowW - 6.0f) / 3.0f;

        // Primary actions row
        if (AccentButton("New", ImVec2(btnW, 24)))
            NewLevel(state);
        ImGui::SameLine(0, 3);
        if (GreenButton("Save", ImVec2(btnW, 24)))
            SaveCurrentLevel(state);
        ImGui::SameLine(0, 3);
        if (AccentButton("Load", ImVec2(btnW, 24)))
            ImGui::OpenPopup("##LELoadPopup");

        // Load popup
        if (ImGui::BeginPopup("##LELoadPopup")) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            ImGui::Text("Load Level");
            ImGui::PopStyleColor();
            ImGui::Separator();
            auto files = LevelFile::ListLevels(m_levelsDirectory);
            if (files.empty()) {
                ImGui::TextDisabled("No .wtlevel files found");
            }
            for (auto& f : files) {
                std::string name = LevelFile::GetLevelName(f);
                if (ImGui::MenuItem(name.c_str()))
                    LoadLevel(f, state);
            }
            ImGui::EndPopup();
        }

        // Save As
        ImGui::Spacing();
        Label("Save As");
        float goW = 36.0f;
        ImGui::SetNextItemWidth(m_levelNameBuf[0] ? -(goW + 6) : -1);
        ImGui::InputText("##levname", m_levelNameBuf, sizeof(m_levelNameBuf));
        if (m_levelNameBuf[0] != '\0') {
            ImGui::SameLine(0, 3);
            if (GreenButton("Go", ImVec2(goW, 0))) {
                m_currentLevelPath = m_levelsDirectory + std::string(m_levelNameBuf) + ".wtlevel";
                SaveCurrentLevel(state);
                m_levelNameBuf[0] = '\0';
            }
        }

        // Hot Swap
        ImGui::Spacing();
        float hwW = rowW * 0.5f;
        if (OrangeButton("Hot Swap", ImVec2(hwW, 22))) {
            m_hotSwapPending = true;
            state.physicsRebuildRequested = true;
            state.navRebuildRequested = true;
            state.entityDirty = true;
        }
        ImGui::SameLine();
        Muted("Push to game");

        // File info card
        ThinSeparator();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kBgChild);
        ImGui::BeginChild("##lvlinfo", ImVec2(0, m_unsavedChanges ? 48.0f : 34.0f), ImGuiChildFlags_Borders);
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
            if (!m_currentLevelPath.empty())
                ImGui::Text("  %s", LevelFile::GetLevelName(m_currentLevelPath).c_str());
            else
                ImGui::Text("  (unsaved)");
            ImGui::Text("  %d entities", state.scene.GetEntityCount());
            ImGui::PopStyleColor();
            if (m_unsavedChanges) {
                ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
                ImGui::Text("  * Unsaved changes");
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Status feedback
        if (m_statusTimer > 0.0f) {
            bool isError = m_statusMessage.find("FAILED") != std::string::npos;
            ImGui::PushStyleColor(ImGuiCol_Text, isError ? kRed : kGreen);
            ImGui::Text("  %s", m_statusMessage.c_str());
            ImGui::PopStyleColor();
            m_statusTimer -= ImGui::GetIO().DeltaTime;
        }

        // Delete
        if (!m_currentLevelPath.empty()) {
            ImGui::Spacing();
            if (DangerButton("Delete Level", ImVec2(-1, 20)))
                ImGui::OpenPopup("##LEDeleteConfirm");

            if (ImGui::BeginPopup("##LEDeleteConfirm")) {
                ImGui::Text("Delete '%s'?", LevelFile::GetLevelName(m_currentLevelPath).c_str());
                ImGui::Spacing();
                if (DangerButton("Yes, Delete", ImVec2(90, 0))) {
                    DeleteCurrentLevel(state);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(60, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        EndPropGroup();
    }
}

void LevelEditorWindow::DrawPCGSection(EditorState& state) {
    // ── Procedural Generation ──
    if (!GroupHeader("Level Generator")) return;
    BeginPropGroup();

    const char* modes[] = {"Urban Arena", "Warfield (Massive)"};
    Label("Mode");
    ImGui::Combo("##pcgmode", &m_pcgMode, modes, 2);
    ImGui::Spacing();

    if (m_pcgMode == 0) {
        // --- Urban Arena ---
        Label("Seed");
        int seedInt = static_cast<int>(m_pcgSettings.seed);
        if (ImGui::DragInt("##pcgseed", &seedInt, 0.5f, 0, 999999))
            m_pcgSettings.seed = static_cast<uint32_t>(seedInt);
        Muted("  0 = random");

        if (GroupHeader("Layout##pcg", true)) {
            BeginPropGroup();
            Label("Arena Size");
            ImGui::DragFloat("##pcgarena", &m_pcgSettings.arenaSize, 0.25f, 20.0f, 100.0f, "%.0f");
            Label("Grid");
            ImGui::PushItemWidth(50);
            ImGui::DragInt("##pcgcols", &m_pcgSettings.gridCols, 0.025f, 2, 5);
            ImGui::SameLine(); ImGui::Text("x"); ImGui::SameLine();
            ImGui::DragInt("##pcgrows", &m_pcgSettings.gridRows, 0.025f, 2, 5);
            ImGui::PopItemWidth();
            Label("Street W");
            ImGui::DragFloat("##pcgstreet", &m_pcgSettings.streetWidth, 0.05f, 2.0f, 8.0f, "%.1f");
            Label("Wall H");
            ImGui::DragFloat("##pcgwallh", &m_pcgSettings.wallHeight, 0.05f, 2.0f, 8.0f, "%.1f");
            EndPropGroup();
        }

        if (GroupHeader("Buildings##pcg", true)) {
            BeginPropGroup();
            Label("Height");
            ImGui::PushItemWidth(60);
            ImGui::DragFloat("##pcgbhmin", &m_pcgSettings.buildingMinH, 0.05f, 1.5f, 8.0f, "%.1f");
            ImGui::SameLine(); Muted("-"); ImGui::SameLine();
            ImGui::DragFloat("##pcgbhmax", &m_pcgSettings.buildingMaxH, 0.05f, 2.0f, 10.0f, "%.1f");
            ImGui::PopItemWidth();
            Label("Building %");
            float bpct = m_pcgSettings.buildingChance * 100.0f;
            if (ImGui::DragFloat("##pcgbchance", &bpct, 0.25f, 0.0f, 100.0f, "%.0f%%"))
                m_pcgSettings.buildingChance = bpct / 100.0f;
            Label("Roof %");
            float rpct = m_pcgSettings.roofChance * 100.0f;
            if (ImGui::DragFloat("##pcgroof", &rpct, 0.25f, 0.0f, 100.0f, "%.0f%%"))
                m_pcgSettings.roofChance = rpct / 100.0f;
            EndPropGroup();
        }

        if (GroupHeader("Detail##pcg")) {
            BeginPropGroup();
            Label("Cover Objs");
            ImGui::DragInt("##pcgcover", &m_pcgSettings.coverDensity, 0.1f, 0, 50);
            Label("Detail Props");
            ImGui::DragInt("##pcgdetail", &m_pcgSettings.detailDensity, 0.1f, 0, 60);
            Label("Windows");
            ImGui::Checkbox("##pcgwin", &m_pcgSettings.addWindows);
            ImGui::SameLine();
            Label("Fences");
            ImGui::Checkbox("##pcgfen", &m_pcgSettings.addFences);
            EndPropGroup();
        }

        ImGui::Spacing();
        if (GreenButton("Generate Urban Level", ImVec2(-1, 28))) {
            LevelGenerator gen;
            gen.settings = m_pcgSettings;
            gen.Generate(state.scene);
            state.selectedEntity = -1;
            state.physicsRebuildRequested = true;
            state.navRebuildRequested = true;
            state.entityDirty = true;
            m_unsavedChanges = true;
            m_currentLevelPath.clear();
            LOG_INFO("PCG: Generated level (seed %u, %d entities)",
                     gen.GetUsedSeed(), state.scene.GetEntityCount());
        }
    } else {
        // --- Warfield ---
        Label("Seed");
        int wfSeed = static_cast<int>(m_warfieldSettings.seed);
        if (ImGui::DragInt("##wfseed", &wfSeed, 0.5f, 0, 999999))
            m_warfieldSettings.seed = static_cast<uint32_t>(wfSeed);
        Muted("  0 = random");

        if (GroupHeader("Terrain##wf", true)) {
            BeginPropGroup();
            Label("Map Size");
            ImGui::DragFloat("##wfsize", &m_warfieldSettings.mapSize, 0.5f, 100.0f, 500.0f, "%.0f");
            Label("Height");
            ImGui::DragFloat("##wftheight", &m_warfieldSettings.terrainHeight, 0.05f, 1.0f, 20.0f, "%.1f");
            Label("Frequency");
            ImGui::DragFloat("##wftfreq", &m_warfieldSettings.terrainFreq, 0.0005f, 0.005f, 0.1f, "%.3f");
            Label("Resolution");
            ImGui::DragInt("##wftres", &m_warfieldSettings.terrainRes, 0.5f, 32, 256);
            EndPropGroup();
        }

        if (GroupHeader("Towns##wf", true)) {
            BeginPropGroup();
            Label("Count");
            ImGui::DragInt("##wftowns", &m_warfieldSettings.townCount, 0.05f, 1, 8);
            Label("Blocks");
            ImGui::PushItemWidth(50);
            ImGui::DragInt("##wftbmin", &m_warfieldSettings.townMinBlocks, 0.025f, 1, 5);
            ImGui::SameLine(); Muted("-"); ImGui::SameLine();
            ImGui::DragInt("##wftbmax", &m_warfieldSettings.townMaxBlocks, 0.025f, 2, 8);
            ImGui::PopItemWidth();
            Label("Street W");
            ImGui::DragFloat("##wfstreet", &m_warfieldSettings.streetWidth, 0.05f, 2.0f, 10.0f, "%.1f");
            Label("Building H");
            ImGui::PushItemWidth(60);
            ImGui::DragFloat("##wfbhmin", &m_warfieldSettings.buildingMinH, 0.05f, 1.5f, 10.0f, "%.1f");
            ImGui::SameLine(); Muted("-"); ImGui::SameLine();
            ImGui::DragFloat("##wfbhmax", &m_warfieldSettings.buildingMaxH, 0.05f, 2.0f, 12.0f, "%.1f");
            ImGui::PopItemWidth();
            Label("Building %");
            float wfbpct = m_warfieldSettings.buildingChance * 100.0f;
            if (ImGui::DragFloat("##wfbchance", &wfbpct, 0.25f, 0.0f, 100.0f, "%.0f%%"))
                m_warfieldSettings.buildingChance = wfbpct / 100.0f;
            Label("Roof %");
            float wfrpct = m_warfieldSettings.roofChance * 100.0f;
            if (ImGui::DragFloat("##wfroof", &wfrpct, 0.25f, 0.0f, 100.0f, "%.0f%%"))
                m_warfieldSettings.roofChance = wfrpct / 100.0f;
            EndPropGroup();
        }

        if (GroupHeader("Features##wf")) {
            BeginPropGroup();
            Label("Outposts");
            ImGui::DragInt("##wfoutposts", &m_warfieldSettings.outpostCount, 0.05f, 0, 12);
            Label("Forests");
            ImGui::DragInt("##wfforests", &m_warfieldSettings.forestClusters, 0.05f, 0, 20);
            Label("Trees/Clst");
            ImGui::DragInt("##wftrees", &m_warfieldSettings.treesPerCluster, 0.1f, 5, 40);
            Label("Field Cover");
            ImGui::DragInt("##wfcover", &m_warfieldSettings.fieldCover, 0.1f, 0, 100);
            ImGui::Spacing();
            ImGui::Checkbox("Forests##wffor", &m_warfieldSettings.addForests);
            ImGui::SameLine();
            ImGui::Checkbox("Outposts##wfop", &m_warfieldSettings.addOutposts);
            EndPropGroup();
        }

        // Entity estimate
        ImGui::Spacing();
        int terrChunks = (int)(m_warfieldSettings.mapSize / 10.0f);
        terrChunks *= terrChunks;
        int estTrees = m_warfieldSettings.addForests ? m_warfieldSettings.forestClusters * m_warfieldSettings.treesPerCluster * 2 : 0;
        int estTotal = terrChunks + m_warfieldSettings.townCount * 20 + m_warfieldSettings.outpostCount * 6 + estTrees + m_warfieldSettings.fieldCover;
        Muted(("  ~" + std::to_string(estTotal) + " entities").c_str());
        ImGui::Spacing();

        if (OrangeButton("Generate Warfield", ImVec2(-1, 28))) {
            WarfieldGenerator gen;
            gen.settings = m_warfieldSettings;
            gen.Generate(state.scene);
            state.selectedEntity = -1;
            state.physicsRebuildRequested = true;
            state.navRebuildRequested = true;
            state.entityDirty = true;
            m_unsavedChanges = true;
            m_currentLevelPath.clear();
            state.groundHalfExtent = m_warfieldSettings.mapSize * 0.5f;
            state.groundRebuildRequested = true;
            LOG_INFO("Warfield: Generated (seed %u, %d entities, %.0fm)",
                     gen.GetUsedSeed(), state.scene.GetEntityCount(), m_warfieldSettings.mapSize);
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.50f, 0.18f, 0.7f));
    ImGui::Text("  Replaces current level!");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Checkbox("Random on launch##pcglaunch", &state.pcgOnLaunch);

    EndPropGroup();
}

// ─────────────────────────────────────────────────────────────
//  TAB: SCENE — Entity management + prefabs
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawEntitySection(EditorState& state) {
    auto& scene = state.scene;
    int count = scene.GetEntityCount();

    // ── Spawn row ──
    {
        float rowW = ImGui::GetContentRegionAvail().x;
        float btnW = (rowW - 9) / 4.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

        if (AccentButton("+ Cube", ImVec2(btnW, 22))) {
            int idx = scene.AddEntity("Cube_" + std::to_string(count), MeshType::Cube);
            state.selectedEntity = idx; m_unsavedChanges = true;
        }
        ImGui::SameLine(0, 3);

        ImGui::PushStyleColor(ImGuiCol_Button, kGreenBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kGreenHov);
        if (ImGui::Button("+ Med", ImVec2(btnW, 22))) {
            int idx = scene.AddEntity("Medpack_" + std::to_string(count), MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.color[0] = 0.1f; e.color[1] = 0.9f; e.color[2] = 0.2f; e.color[3] = 1.0f;
            e.scale[0] = 0.4f; e.scale[1] = 0.25f; e.scale[2] = 0.3f;
            e.pickupType = PickupType::Health; e.pickupAmount = 25.0f;
            state.selectedEntity = idx; m_unsavedChanges = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 3);

        ImGui::PushStyleColor(ImGuiCol_Button, kOrangeBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kOrangeHov);
        if (ImGui::Button("+ Ammo", ImVec2(btnW, 22))) {
            int idx = scene.AddEntity("AmmoPack_" + std::to_string(count), MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.color[0] = 0.9f; e.color[1] = 0.7f; e.color[2] = 0.1f; e.color[3] = 1.0f;
            e.scale[0] = 0.3f; e.scale[1] = 0.2f; e.scale[2] = 0.25f;
            e.pickupType = PickupType::Ammo; e.pickupAmount = 30.0f;
            state.selectedEntity = idx; m_unsavedChanges = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 3);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.14f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.20f, 0.12f, 1.0f));
        if (ImGui::Button("+ Door", ImVec2(btnW, 22))) {
            int idx = scene.AddEntity("Door_" + std::to_string(count), MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.color[0] = 0.45f; e.color[1] = 0.30f; e.color[2] = 0.18f; e.color[3] = 1.0f;
            e.scale[0] = 1.0f; e.scale[1] = 2.5f; e.scale[2] = 0.15f;
            e.interactType = InteractType::Door;
            e.doorOpenAngle = 90.0f; e.doorSpeed = 3.0f; e.destructible = false;
            state.selectedEntity = idx; m_unsavedChanges = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::PopStyleVar();
    }

    // Row 2: Model + Dup + Del
    {
        float rowW = ImGui::GetContentRegionAvail().x;
        auto modelNames = ResourceManager::Get().GetModelNames();
        float actionW = 40.0f;
        bool hasSelection = (count > 0 && state.selectedEntity >= 0);
        float modelW = rowW - (hasSelection ? (actionW * 2 + 9.0f) : 0.0f);

        if (!modelNames.empty()) {
            if (AccentButton("+ Model", ImVec2(modelW, 22)))
                ImGui::OpenPopup("##LEModelSpawn");
            if (ImGui::BeginPopup("##LEModelSpawn")) {
                ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                ImGui::Text("Select Model"); ImGui::PopStyleColor();
                ImGui::Separator();
                for (auto& mname : modelNames) {
                    if (ImGui::MenuItem(mname.c_str())) {
                        int idx = scene.AddEntity("", MeshType::Custom);
                        scene.GetEntity(idx).meshName = mname;
                        scene.GetEntity(idx).name = mname + "_" + std::to_string(idx);
                        std::string autoTex = ResourceManager::Get().FindMatchingTexture(mname);
                        if (!autoTex.empty()) scene.GetEntity(idx).textureName = autoTex;
                        state.selectedEntity = idx; m_unsavedChanges = true;
                    }
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("+ Model", ImVec2(modelW, 22));
            ImGui::EndDisabled();
        }

        if (hasSelection) {
            ImGui::SameLine(0, 3);
            if (AccentButton("Dup", ImVec2(actionW, 22))) {
                int idx = scene.DuplicateEntity(state.selectedEntity);
                if (idx >= 0) { state.selectedEntity = idx; m_unsavedChanges = true; }
            }
            ImGui::SameLine(0, 3);
            if (DangerButton("Del", ImVec2(actionW, 22))) {
                scene.RemoveEntity(state.selectedEntity);
                if (state.selectedEntity >= scene.GetEntityCount())
                    state.selectedEntity = scene.GetEntityCount() - 1;
                m_unsavedChanges = true;
            }
        }
    }

    count = scene.GetEntityCount();
    ImGui::Spacing();

    // ── Entity List ──
    if (count > 0) {
        // Search / filter bar
        static char filterBuf[64] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        ImGui::InputTextWithHint("##entfilter", "Search entities...", filterBuf, sizeof(filterBuf));
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, kBgChild);
        float listH = (float)((std::min)(count, 14)) * 22.0f + 4.0f;
        ImGui::BeginChild("##leentlist", ImVec2(0, listH), ImGuiChildFlags_Borders);

        std::string filterStr(filterBuf);
        for (auto& c : filterStr) c = (char)tolower((unsigned char)c);

        for (int i = 0; i < count; i++) {
            auto& e = scene.GetEntity(i);

            // Filter check
            if (!filterStr.empty()) {
                std::string lname = e.name;
                for (auto& c : lname) c = (char)tolower((unsigned char)c);
                if (lname.find(filterStr) == std::string::npos) continue;
            }

            bool selected = (state.selectedEntity == i);
            bool hovered  = (m_hoveredEntity == i);

            // Alternating row background
            if (i % 2 == 0) {
                ImVec2 rMin = ImGui::GetCursorScreenPos();
                ImVec2 rMax = ImVec2(rMin.x + ImGui::GetContentRegionAvail().x, rMin.y + 22.0f);
                ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax, IM_COL32(255, 255, 255, 5));
            }

            // Badge: colored dot + short type letter
            const char* badge = "C";
            ImU32 dotColor = IM_COL32(100, 100, 120, 255);
            if (e.interactType == InteractType::Door)         { badge = "D"; dotColor = IM_COL32(80, 200, 255, 255); }
            else if (e.interactType == InteractType::Switch)  { badge = "S"; dotColor = IM_COL32(80, 200, 255, 255); }
            else if (e.interactType == InteractType::Button)  { badge = "B"; dotColor = IM_COL32(80, 200, 255, 255); }
            else if (e.interactType == InteractType::Pickup)  { badge = "P"; dotColor = IM_COL32(80, 220, 100, 255); }
            else if (e.interactType == InteractType::Loot)    { badge = "L"; dotColor = IM_COL32(230, 180, 60, 255); }
            else if (e.pickupType == PickupType::Health)      { badge = "H"; dotColor = IM_COL32(80, 220, 100, 255); }
            else if (e.pickupType == PickupType::Ammo)        { badge = "A"; dotColor = IM_COL32(230, 190, 60, 255); }
            else if (e.meshType == MeshType::Custom)          { badge = "M"; dotColor = IM_COL32(190, 155, 70, 255); }

            // Draw colored dot indicator
            ImVec2 dotPos = ImVec2(ImGui::GetCursorScreenPos().x + 8, ImGui::GetCursorScreenPos().y + 11);
            ImGui::GetWindowDrawList()->AddCircleFilled(dotPos, 4.0f, dotColor);

            // Selectable label (offset to make room for dot)
            char buf[140];
            snprintf(buf, sizeof(buf), "    %s  %s##%d", badge, e.name.c_str(), i);

            ImVec4 selColor = selected ? ImVec4(0.16f, 0.24f, 0.42f, 1.0f) :
                              hovered  ? ImVec4(0.12f, 0.16f, 0.28f, 0.6f) :
                                         ImVec4(0, 0, 0, 0);
            ImGui::PushStyleColor(ImGuiCol_Header, selColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.20f, 0.35f, 0.7f));
            if (ImGui::Selectable(buf, selected, 0, ImVec2(0, 20)))
                state.selectedEntity = i;
            ImGui::PopStyleColor(2);

            // Hidden indicator + tag on right side
            {
                bool hasTag = !e.tag.empty();
                if (!e.visible || hasTag) {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ((!e.visible && hasTag) ? 60.0f : 30.0f));
                    if (hasTag) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 0.8f, 0.8f));
                        ImGui::Text("[%s]", e.tag.c_str());
                        ImGui::PopStyleColor();
                        if (!e.visible) ImGui::SameLine();
                    }
                    if (!e.visible) Muted("(H)");
                }
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        Muted("  No entities. Spawn with buttons above.");
    }

    // ── Properties (selected entity) ──
    if (state.selectedEntity >= 0 && state.selectedEntity < count) {
        auto& e = scene.GetEntity(state.selectedEntity);
        ImGui::Spacing();

        // Name header card
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.10f, 0.14f, 1.0f));
        ImGui::BeginChild("##enthdr", ImVec2(0, 46), ImGuiChildFlags_Borders);
        ImGui::Spacing();
        char nameBuf[128];
        strncpy_s(nameBuf, e.name.c_str(), sizeof(nameBuf) - 1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##entname", nameBuf, sizeof(nameBuf))) {
            e.name = nameBuf; m_unsavedChanges = true;
        }
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
            const char* tn = MeshTypeName(e.meshType);
            if (e.meshType == MeshType::Custom && !e.meshName.empty())
                ImGui::Text("  %s | %s", tn, e.meshName.c_str());
            else
                ImGui::Text("  %s", tn);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // --- Transform ---
        if (GroupHeader("Transform##ent", true)) {
            BeginPropGroup();
            Label("Position");
            if (ImGui::InputFloat3("##epos", e.position, "%.3f")) m_unsavedChanges = true;
            Label("Rotation");
            if (ImGui::InputFloat3("##erot", e.rotation, "%.2f")) m_unsavedChanges = true;
            Label("Scale");
            if (ImGui::InputFloat3("##escl", e.scale, "%.3f")) m_unsavedChanges = true;
            Label("Color");
            if (ImGui::ColorEdit4("##ecol", e.color,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) m_unsavedChanges = true;
            EndPropGroup();
        }

        // --- Appearance ---
        if (GroupHeader("Appearance##ent")) {
            BeginPropGroup();
            if (e.meshType == MeshType::Custom) {
                Label("Model");
                auto allModels = ResourceManager::Get().GetModelNames();
                int cur = -1;
                for (int m = 0; m < (int)allModels.size(); m++)
                    if (allModels[m] == e.meshName) { cur = m; break; }
                if (ImGui::BeginCombo("##emodel", cur >= 0 ? allModels[cur].c_str() : "<none>")) {
                    for (int m = 0; m < (int)allModels.size(); m++) {
                        bool sel = (m == cur);
                        if (ImGui::Selectable(allModels[m].c_str(), sel)) {
                            e.meshName = allModels[m]; m_unsavedChanges = true;
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            {
                Label("Texture");
                auto allTex = ResourceManager::Get().GetTextureNames();
                int curTex = -1;
                for (int t = 0; t < (int)allTex.size(); t++)
                    if (allTex[t] == e.textureName) { curTex = t; break; }
                if (ImGui::BeginCombo("##etex", curTex >= 0 ? allTex[curTex].c_str() : "(default)")) {
                    if (ImGui::Selectable("(default)", curTex < 0)) { e.textureName.clear(); m_unsavedChanges = true; }
                    ImGui::Separator();
                    for (int t = 0; t < (int)allTex.size(); t++) {
                        bool sel = (t == curTex);
                        if (ImGui::Selectable(allTex[t].c_str(), sel)) { e.textureName = allTex[t]; m_unsavedChanges = true; }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            EndPropGroup();
        }

        // --- Flags ---
        if (GroupHeader("Flags##ent")) {
            BeginPropGroup();
            Label("Visible");
            if (ImGui::Checkbox("##evis", &e.visible)) m_unsavedChanges = true;
            ImGui::SameLine(kLabelW + 80);
            ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary); ImGui::Text("Shadow"); ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Checkbox("##eshd", &e.castShadow)) m_unsavedChanges = true;

            Label("Ladder");
            if (ImGui::Checkbox("##eladder", &e.isLadder)) m_unsavedChanges = true;
            ImGui::SameLine(kLabelW + 80);
            ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary); ImGui::Text("Gravity"); ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Checkbox("##egrav", &e.hasGravity)) { m_unsavedChanges = true; state.physicsRebuildRequested = true; }

            Label("Tag");
            char tagBuf[128] = {};
            strncpy(tagBuf, e.tag.c_str(), sizeof(tagBuf) - 1);
            if (ImGui::InputText("##etag", tagBuf, sizeof(tagBuf))) {
                e.tag = tagBuf; m_unsavedChanges = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gameplay tag (e.g. spawn_point, objective, cover)");

            Label("Cull Cat.");
            const char* cullNames[] = {"Building", "Prop", "Detail", "Foliage", "Always"};
            int cullIdx = static_cast<int>(e.cullCategory);
            if (ImGui::Combo("##ecullcat", &cullIdx, cullNames, IM_ARRAYSIZE(cullNames))) {
                e.cullCategory = static_cast<CullCategory>(cullIdx); m_unsavedChanges = true;
            }
            EndPropGroup();
        }

        // --- Destruction ---
        if (GroupHeader("Destruction##ent")) {
            BeginPropGroup();
            Label("Material");
            const char* matNames[] = {"Concrete", "Wood", "Metal", "Glass"};
            int matIdx = static_cast<int>(e.materialType);
            if (ImGui::Combo("##emat", &matIdx, matNames, IM_ARRAYSIZE(matNames))) {
                e.materialType = static_cast<MaterialType>(matIdx); m_unsavedChanges = true;
            }
            Label("Destructible");
            if (ImGui::Checkbox("##edest", &e.destructible)) m_unsavedChanges = true;

            if (e.destructible) {
                ThinSeparator();

                // HP bar
                float frac = e.GetHealthFraction();
                ImVec4 barCol = frac > 0.5f
                    ? ImVec4((1.0f - frac) * 2.0f, 1.0f, 0.0f, 1.0f)
                    : ImVec4(1.0f, frac * 2.0f, 0.0f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
                char hpBuf[32]; snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", e.health, e.maxHealth);
                ImGui::ProgressBar(frac, ImVec2(-1, 12), hpBuf);
                ImGui::PopStyleColor();
                ImGui::Spacing();

                Label("Health");
                if (ImGui::DragFloat("##ehp", &e.health, 0.5f, 0.0f, 10000.0f, "%.0f")) m_unsavedChanges = true;
                Label("Max HP");
                if (ImGui::DragFloat("##emhp", &e.maxHealth, 0.5f, 1.0f, 10000.0f, "%.0f")) {
                    if (e.health > e.maxHealth) e.health = e.maxHealth;
                    m_unsavedChanges = true;
                }
                Label("Debris");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
                if (ImGui::DragInt("##edc", &e.debrisCount, 0.05f, 1, 50)) m_unsavedChanges = true;
                ImGui::SameLine(); Muted("x"); ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##eds", &e.debrisScale, 0.005f, 0.05f, 2.0f, "%.2f")) m_unsavedChanges = true;

                Label("Break Pcs");
                if (ImGui::DragInt("##ebp", &e.breakPieceCount, 0.05f, 0, 8)) m_unsavedChanges = true;

                Label("Supported");
                char supBuf[128] = {};
                strncpy(supBuf, e.supportedBy.c_str(), sizeof(supBuf) - 1);
                if (ImGui::InputText("##esup", supBuf, sizeof(supBuf))) { e.supportedBy = supBuf; m_unsavedChanges = true; }

                Label("Voxel Mode");
                if (ImGui::Checkbox("##evox", &e.voxelDestruction)) m_unsavedChanges = true;
                if (e.voxelDestruction) {
                    ImGui::SameLine(); Muted("Res:"); ImGui::SameLine(); ImGui::SetNextItemWidth(40);
                    if (ImGui::DragInt("##evoxres", &e.voxelRes, 0.025f, 2, 8)) { e.ResetVoxelMask(); m_unsavedChanges = true; }
                }

                ImGui::Spacing();
                const char* stages[] = {"Pristine", "Scratched", "Damaged", "Critical"};
                Muted(stages[e.GetDamageStage()]);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 62);
                if (AccentButton("Reset##hp", ImVec2(66, 0))) {
                    e.health = e.maxHealth; e.damageFlashTimer = 0; e.hitDecalCount = 0; e.hitDecalNext = 0;
                    if (e.voxelDestruction) e.ResetVoxelMask();
                }
            }
            EndPropGroup();
        }

        // --- Interaction ---
        if (GroupHeader("Interaction##ent")) {
            BeginPropGroup();
            Label("Type");
            const char* iNames[] = {"None", "Door", "Switch", "Button", "Pickup", "Loot"};
            int itype = static_cast<int>(e.interactType);
            if (ImGui::Combo("##einteract", &itype, iNames, IM_ARRAYSIZE(iNames))) {
                e.interactType = static_cast<InteractType>(itype); m_unsavedChanges = true;
            }

            if (e.interactType != InteractType::None) {
                Label("Range");
                if (ImGui::DragFloat("##eintrange", &e.interactRange, 0.1f, 0.5f, 20.0f, "%.1f m")) m_unsavedChanges = true;
                Label("Prompt");
                char promptBuf[128] = {};
                strncpy(promptBuf, e.interactPrompt.c_str(), sizeof(promptBuf) - 1);
                if (ImGui::InputText("##eintprompt", promptBuf, sizeof(promptBuf))) {
                    e.interactPrompt = promptBuf; m_unsavedChanges = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leave empty for auto prompt");

                // Hold interaction
                Label("Hold Interact");
                if (ImGui::Checkbox("##ehold", &e.holdInteract)) m_unsavedChanges = true;
                if (e.holdInteract) {
                    Label("Hold Duration");
                    if (ImGui::DragFloat("##eholddur", &e.holdInteractDuration, 0.1f, 0.0f, 10.0f, "%.1f s")) m_unsavedChanges = true;
                }

                // Required item
                Label("Requires Item ID");
                if (ImGui::DragInt("##ereqitem", &e.requiresItemID, 1.0f, 0, 99999)) m_unsavedChanges = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = no requirement");

                if (e.interactType == InteractType::Door) {
                    ThinSeparator();
                    Muted("  Door Settings");
                    Label("Open Angle");
                    if (ImGui::DragFloat("##edoorangle", &e.doorOpenAngle, 1.0f, -180.0f, 180.0f, "%.0f\xc2\xb0")) m_unsavedChanges = true;
                    Label("Slide Dist");
                    if (ImGui::DragFloat("##edoorslide", &e.doorSlideDist, 0.1f, 0.0f, 20.0f, "%.1f m")) m_unsavedChanges = true;
                    Label("Speed");
                    if (ImGui::DragFloat("##edoorspeed", &e.doorSpeed, 0.1f, 0.1f, 20.0f, "%.1f")) m_unsavedChanges = true;
                }

                if (e.interactType == InteractType::Switch) {
                    ThinSeparator();
                    Muted("  Switch Settings");
                    Label("Target");
                    char targetBuf[128] = {};
                    strncpy(targetBuf, e.interactTarget.c_str(), sizeof(targetBuf) - 1);
                    if (ImGui::InputText("##einttarget", targetBuf, sizeof(targetBuf))) {
                        e.interactTarget = targetBuf; m_unsavedChanges = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Entity name to toggle");
                }

                if (e.interactType == InteractType::Pickup) {
                    ThinSeparator();
                    Muted("  Pickup Settings");
                    Label("Item ID");
                    if (ImGui::DragInt("##epickid", &e.pickupItemID, 1.0f, 0, 99999)) m_unsavedChanges = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Item database ID");
                }

                if (e.interactType == InteractType::Loot) {
                    ThinSeparator();
                    Muted("  Loot Settings");
                    Label("Loot Container");
                    if (ImGui::Checkbox("##elootcont", &e.isLootContainer)) m_unsavedChanges = true;
                    Label("Loot Table");
                    char lootBuf[128] = {};
                    strncpy(lootBuf, e.lootTableName.c_str(), sizeof(lootBuf) - 1);
                    if (ImGui::InputText("##eloottable", lootBuf, sizeof(lootBuf))) {
                        e.lootTableName = lootBuf; m_unsavedChanges = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("e.g. crate_common, crate_military, supply_drop");
                }
            }
            EndPropGroup();
        }
    }
}

void LevelEditorWindow::DrawPrefabSection(EditorState& state) {
    if (!GroupHeader("Prefab Browser")) return;
    BeginPropGroup();

    auto& scene = state.scene;
    float btnW = (ImGui::GetContentRegionAvail().x - 4) / 2.0f;

    auto getSpawnPos = [&](float defaultY) -> XMFLOAT3 {
        XMFLOAT3 sp = {0, defaultY, 0};
        if (m_gridSnap) {
            XMFLOAT3 wp = ScreenToWorldPlane(m_width / 2, m_height / 2, 0.0f);
            sp.x = roundf(wp.x / m_gridSnapSize) * m_gridSnapSize;
            sp.z = roundf(wp.z / m_gridSnapSize) * m_gridSnapSize;
        }
        return sp;
    };

    auto spawnCube = [&](const char* label, float sx, float sy, float sz,
                         float r, float g, float b, float hp,
                         float yOff = 0.0f, int debris = 6, float debrisS = 0.3f,
                         const char* texFolder = nullptr) {
        XMFLOAT3 sp = getSpawnPos(yOff);
        int idx = scene.AddEntity(label, MeshType::Cube);
        auto& e = scene.GetEntity(idx);
        e.name = std::string(label) + "_" + std::to_string(idx);
        e.position[0] = sp.x; e.position[1] = sp.y; e.position[2] = sp.z;
        e.scale[0] = sx; e.scale[1] = sy; e.scale[2] = sz;
        e.color[0] = r; e.color[1] = g; e.color[2] = b; e.color[3] = 1.0f;
        e.health = hp; e.maxHealth = hp;
        e.debrisCount = debris; e.debrisScale = debrisS;
        if (texFolder && texFolder[0]) {
            std::string texKey = std::string(texFolder) + "texture";
            if (ResourceManager::Get().GetTexture(texKey)) e.textureName = texKey;
        }
        state.selectedEntity = idx; m_unsavedChanges = true;
    };

    auto spawnLadder = [&](const char* label = "Ladder", float height = 4.0f) {
        XMFLOAT3 sp = getSpawnPos(height * 0.5f);
        int idx = scene.AddEntity(label, MeshType::Cube);
        auto& e = scene.GetEntity(idx);
        e.name = std::string(label) + "_" + std::to_string(idx);
        e.position[0] = sp.x; e.position[1] = sp.y; e.position[2] = sp.z;
        e.scale[0] = 0.15f; e.scale[1] = height; e.scale[2] = 0.6f;
        e.color[0] = 0.40f; e.color[1] = 0.32f; e.color[2] = 0.22f; e.color[3] = 1.0f;
        e.destructible = false; e.materialType = MaterialType::Wood; e.isLadder = true;
        state.selectedEntity = idx; m_unsavedChanges = true;
    };

    auto spawnModel = [&](const std::string& modelKey, const char* texFolder, float yOff = 0.0f) {
        std::string display = modelKey.substr(modelKey.rfind('/') + 1);
        XMFLOAT3 sp = getSpawnPos(yOff);
        int idx = scene.AddEntity("", MeshType::Custom);
        auto& e = scene.GetEntity(idx);
        e.meshName = modelKey;
        e.name = display + "_" + std::to_string(idx);
        e.position[0] = sp.x; e.position[1] = sp.y; e.position[2] = sp.z;
        e.color[0] = 1; e.color[1] = 1; e.color[2] = 1; e.color[3] = 1;
        e.health = 200; e.maxHealth = 200;
        if (texFolder && texFolder[0]) {
            std::string texKey = std::string(texFolder) + "texture";
            if (ResourceManager::Get().GetTexture(texKey)) e.textureName = texKey;
        }
        if (e.textureName.empty()) {
            std::string autoTex = ResourceManager::Get().FindMatchingTexture(modelKey);
            if (!autoTex.empty()) e.textureName = autoTex;
        }
        state.selectedEntity = idx; m_unsavedChanges = true;
    };

    auto allModels = ResourceManager::Get().GetModelNames();

    struct PrefabCat { const char* label; const char* folder; const char* texFolder; ImVec4 col; ImVec4 hover; };
    const PrefabCat cats[] = {
        {"Walls",      "PreFabs/Walls/",      "Walls/",  {0.18f,0.16f,0.14f,1}, {0.24f,0.22f,0.20f,1}},
        {"Floors",     "PreFabs/Floors/",     "Floors/", {0.14f,0.18f,0.14f,1}, {0.20f,0.26f,0.20f,1}},
        {"Structures", "PreFabs/Structures/", "Walls/",  {0.16f,0.14f,0.18f,1}, {0.22f,0.20f,0.26f,1}},
        {"Doors",      "PreFabs/Doors/",      "Walls/",  {0.18f,0.14f,0.14f,1}, {0.24f,0.20f,0.20f,1}},
        {"Props",      "PreFabs/Props/",      "Props/",  {0.18f,0.16f,0.10f,1}, {0.26f,0.22f,0.14f,1}},
        {"Buildings",  "Prefabs/",            "",        {0.12f,0.18f,0.20f,1}, {0.18f,0.26f,0.30f,1}},
    };
    constexpr int catCount = 6;

    for (int c = 0; c < catCount; c++) {
        if (c % 2 != 0) ImGui::SameLine(0, 4);

        int modelCount = 0;
        for (auto& m : allModels) if (m.rfind(cats[c].folder, 0) == 0) modelCount++;

        ImGui::PushStyleColor(ImGuiCol_Button, cats[c].col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cats[c].hover);
        char btnId[64]; snprintf(btnId, sizeof(btnId), "%s (%d)##pf%d", cats[c].label, modelCount, c);
        if (ImGui::Button(btnId, ImVec2(btnW, 26)))
            ImGui::OpenPopup(("##PFB_" + std::to_string(c)).c_str());
        ImGui::PopStyleColor(2);

        std::string popId = "##PFB_" + std::to_string(c);
        ImGui::SetNextWindowSizeConstraints(ImVec2(240, 120), ImVec2(360, 500));
        if (ImGui::BeginPopup(popId.c_str())) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            ImGui::Text("%s", cats[c].label); ImGui::PopStyleColor();
            ImGui::Separator();

            Muted("Basic Shapes");
            if (c == 0) { if (ImGui::Selectable("  Wall (4x3x0.3)")) spawnCube("Wall", 4,3,0.3f, 0.55f,0.52f,0.48f, 200, 1.5f); }
            else if (c == 1) { if (ImGui::Selectable("  Floor (8x0.1x8)")) spawnCube("Floor", 8,0.1f,8, 0.45f,0.44f,0.42f, 500, -0.05f); }
            else if (c == 2) {
                if (ImGui::Selectable("  Pillar (0.5x4x0.5)")) spawnCube("Pillar", 0.5f,4,0.5f, 0.50f,0.48f,0.45f, 300, 2.0f);
                if (ImGui::Selectable("  Bunker (5x2.5x4)"))   spawnCube("Bunker", 5,2.5f,4, 0.35f,0.36f,0.34f, 800, 1.25f, 12);
                if (ImGui::Selectable("  Ladder (0.15x4x0.6)")) spawnLadder();
            }
            else if (c == 3) { Muted("  (none)"); }
            else if (c == 4) {
                if (ImGui::Selectable("  Crate (1x1x1)"))  spawnCube("Crate", 1,1,1, 0.55f,0.40f,0.22f, 50, 0.5f, 8, 0.2f);
                if (ImGui::Selectable("  Cover (3x1x0.4)")) spawnCube("Cover", 3,1,0.4f, 0.40f,0.42f,0.38f, 150, 0.5f);
            }
            else if (c == 5) { if (ImGui::Selectable("  Building (6x4x6)")) spawnCube("Building", 6,4,6, 0.48f,0.46f,0.44f, 500, 2.0f, 12, 0.4f); }

            ImGui::Spacing();
            Muted("Models");
            ImGui::Separator();

            std::vector<std::string> catModels;
            for (auto& mname : allModels)
                if (mname.rfind(cats[c].folder, 0) == 0) catModels.push_back(mname);

            for (auto& mname : catModels) {
                std::string display = mname.substr(mname.rfind('/') + 1);
                for (auto& ch : display) if (ch == '_') ch = ' ';
                char selLabel[128]; snprintf(selLabel, sizeof(selLabel), "  %s##%s", display.c_str(), mname.c_str());
                if (ImGui::Selectable(selLabel)) spawnModel(mname, cats[c].texFolder, 0.0f);
            }
            if (catModels.empty()) Muted("  No models loaded");

            if (!catModels.empty()) {
                ImGui::Spacing(); ImGui::Separator();
                char batchLabel[64]; snprintf(batchLabel, sizeof(batchLabel), "Spawn All (%d)##batch%d", (int)catModels.size(), c);
                if (GreenButton(batchLabel, ImVec2(-1, 26))) {
                    for (auto& mname : catModels) spawnModel(mname, cats[c].texFolder, 0.0f);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    Muted("  Browse & spawn prefabs by category.");
    EndPropGroup();
}

void LevelEditorWindow::DrawSceneSection(EditorState& state) {
    if (!GroupHeader("Scene Overview")) return;
    BeginPropGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBgChild);
    ImGui::BeginChild("##sceneovw", ImVec2(0, 42), ImGuiChildFlags_Borders);
    Muted("  Ground Plane  400 x 400");
    if (state.showDebug) Muted("  Debug Visuals  ON");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    Label("Debug Lines");
    ImGui::Checkbox("##scenedbg", &state.showDebug);

    EndPropGroup();
}

// ─────────────────────────────────────────────────────────────
//  TAB: WORLD — Lighting, Sky, Shadows, SSAO
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawLightingSection(EditorState& state) {
    if (GroupHeader("Directional Light", true)) {
        BeginPropGroup();
        Label("Direction");
        state.lightingDirty |= ImGui::DragFloat3("##sundir", state.sunDirection, 0.005f, -1.0f, 1.0f);
        Label("Intensity");
        state.lightingDirty |= ImGui::DragFloat("##sunint", &state.sunIntensity, 0.025f, 0.0f, 10.0f);
        Label("Color");
        state.lightingDirty |= ImGui::ColorEdit3("##suncol", state.sunColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        EndPropGroup();
    }

    if (GroupHeader("Ambient", true)) {
        BeginPropGroup();
        Label("Color");
        state.lightingDirty |= ImGui::ColorEdit3("##ambcol", state.ambientColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Intensity");
        state.lightingDirty |= ImGui::DragFloat("##ambint", &state.ambientIntensity, 0.025f, 0.0f, 5.0f);
        EndPropGroup();
    }

    if (GroupHeader("Fog")) {
        BeginPropGroup();
        Label("Color");
        state.lightingDirty |= ImGui::ColorEdit3("##fogcol", state.fogColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Density");
        state.lightingDirty |= ImGui::DragFloat("##fogden", &state.fogDensity, 0.005f, 0.0f, 5.0f);
        Label("Start");
        state.lightingDirty |= ImGui::DragFloat("##fogst", &state.fogStart, 0.5f, 0.0f, 1000.0f);
        Label("End");
        state.lightingDirty |= ImGui::DragFloat("##fogen", &state.fogEnd, 0.5f, 0.0f, 2000.0f);
        EndPropGroup();
    }
}

void LevelEditorWindow::DrawSkySection(EditorState& state) {
    if (GroupHeader("Atmosphere", true)) {
        BeginPropGroup();
        Label("Zenith");
        state.skyDirty |= ImGui::ColorEdit3("##skyzen", state.skyZenithColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Horizon");
        state.skyDirty |= ImGui::ColorEdit3("##skyhor", state.skyHorizonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Ground");
        state.skyDirty |= ImGui::ColorEdit3("##skygnd", state.skyGroundColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Brightness");
        state.skyDirty |= ImGui::DragFloat("##skybrt", &state.skyBrightness, 0.005f, 0.1f, 5.0f);
        Label("Horizon Fall");
        state.skyDirty |= ImGui::DragFloat("##skyhf", &state.skyHorizonFalloff, 0.005f, 0.1f, 3.0f);
        EndPropGroup();
    }

    if (GroupHeader("Sun Disc")) {
        BeginPropGroup();
        Label("Size");
        float discDeg = acosf(state.sunDiscSize) * (180.0f / 3.14159f);
        if (ImGui::DragFloat("##sdisc", &discDeg, 0.005f, 0.01f, 5.0f, "%.2f\xc2\xb0")) {
            state.sunDiscSize = cosf(discDeg * (3.14159f / 180.0f)); state.skyDirty = true;
        }
        Label("Glow Int");
        state.skyDirty |= ImGui::DragFloat("##sglowi", &state.sunGlowIntensity, 0.005f, 0.0f, 2.0f);
        Label("Glow Fall");
        state.skyDirty |= ImGui::DragFloat("##sglowf", &state.sunGlowFalloff, 0.5f, 1.0f, 256.0f);
        EndPropGroup();
    }

    if (GroupHeader("Clouds")) {
        BeginPropGroup();
        Label("Coverage");
        state.skyDirty |= ImGui::DragFloat("##cldcov", &state.cloudCoverage, 0.005f, 0.0f, 1.0f);
        Label("Speed");
        state.skyDirty |= ImGui::DragFloat("##cldspd", &state.cloudSpeed, 0.0025f, 0.0f, 0.5f);
        Label("Density");
        state.skyDirty |= ImGui::DragFloat("##cldden", &state.cloudDensity, 0.05f, 0.5f, 10.0f);
        Label("Height");
        state.skyDirty |= ImGui::DragFloat("##cldhgt", &state.cloudHeight, 0.005f, 0.05f, 1.0f);
        Label("Color");
        state.skyDirty |= ImGui::ColorEdit3("##cldcol", state.cloudColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Sun Lit");
        state.skyDirty |= ImGui::DragFloat("##cldsun", &state.cloudSunInfluence, 0.005f, 0.0f, 1.0f);
        EndPropGroup();
    }
}

void LevelEditorWindow::DrawShadowsSection(EditorState& state) {
    if (!GroupHeader("Shadow Mapping")) return;
    BeginPropGroup();

    Label("Enabled");
    ImGui::Checkbox("##shden", &state.shadowsEnabled);
    if (state.shadowsEnabled) {
        Label("Intensity");
        ImGui::DragFloat("##shdint", &state.shadowIntensity, 0.005f, 0.0f, 1.0f);
        Label("Bias");
        ImGui::DragFloat("##shdbias", &state.shadowBias, 0.00005f, 0.0f, 0.01f, "%.4f");
        Label("Normal Bias");
        ImGui::DragFloat("##shdnbias", &state.shadowNormalBias, 0.0005f, 0.0f, 0.1f, "%.3f");
        Label("Distance");
        ImGui::DragFloat("##shddist", &state.shadowDistance, 0.25f, 5.0f, 100.0f);
        ThinSeparator();
        Label("Resolution");
        ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
        ImGui::Text("%dx%d", state.shadowMapResolution, state.shadowMapResolution);
        ImGui::PopStyleColor();
    }

    EndPropGroup();
}

void LevelEditorWindow::DrawSSAOSection(EditorState& state) {
    if (!GroupHeader("Ambient Occlusion")) return;
    BeginPropGroup();

    Label("Enabled");
    ImGui::Checkbox("##ssaoen", &state.ssaoEnabled);
    if (state.ssaoEnabled) {
        Label("Radius");
        ImGui::DragFloat("##ssaorad", &state.ssaoRadius, 0.005f, 0.05f, 5.0f);
        Label("Bias");
        ImGui::DragFloat("##ssaobias", &state.ssaoBias, 0.0005f, 0.0f, 0.1f, "%.3f");
        Label("Intensity");
        ImGui::DragFloat("##ssaoint", &state.ssaoIntensity, 0.025f, 0.0f, 5.0f);
        Label("Samples");
        int ks = state.ssaoKernelSize;
        if (ImGui::SliderInt("##ssaokernel", &ks, 4, 64)) state.ssaoKernelSize = ks;
    }

    EndPropGroup();
}

// ─────────────────────────────────────────────────────────────
//  TAB: RENDER — Post-process, art style
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawPostProcessSection(EditorState& state) {
    if (GroupHeader("Bloom", true)) {
        BeginPropGroup();
        Label("Enabled");
        ImGui::Checkbox("##ppbloom", &state.ppBloomEnabled);
        if (state.ppBloomEnabled) {
            Label("Threshold");
            ImGui::DragFloat("##ppbloomth", &state.ppBloomThreshold, 0.005f, 0.0f, 5.0f);
            Label("Intensity");
            ImGui::DragFloat("##ppbloomint", &state.ppBloomIntensity, 0.005f, 0.0f, 3.0f);
        }
        EndPropGroup();
    }

    if (GroupHeader("Vignette")) {
        BeginPropGroup();
        Label("Enabled");
        ImGui::Checkbox("##ppvig", &state.ppVignetteEnabled);
        if (state.ppVignetteEnabled) {
            Label("Intensity");
            ImGui::DragFloat("##ppvigint", &state.ppVignetteIntensity, 0.005f, 0.0f, 2.0f);
            Label("Smoothness");
            ImGui::DragFloat("##ppvigsm", &state.ppVignetteSmoothness, 0.005f, 0.0f, 2.0f);
        }
        EndPropGroup();
    }

    if (GroupHeader("Color Correction", true)) {
        BeginPropGroup();
        Label("Brightness");
        ImGui::DragFloat("##ppbright", &state.ppBrightness, 0.0025f, -1.0f, 1.0f);
        Label("Contrast");
        ImGui::DragFloat("##ppcontrast", &state.ppContrast, 0.005f, 0.0f, 2.0f);
        Label("Saturation");
        ImGui::DragFloat("##ppsat", &state.ppSaturation, 0.005f, 0.0f, 2.0f);
        Label("Gamma");
        ImGui::DragFloat("##ppgamma", &state.ppGamma, 0.005f, 0.5f, 2.0f);
        Label("Tint");
        ImGui::ColorEdit3("##pptint", state.ppTint, ImGuiColorEditFlags_Float);
        EndPropGroup();
    }
}

void LevelEditorWindow::DrawArtStyleSection(EditorState& state) {
    if (GroupHeader("Cel-Shading", true)) {
        BeginPropGroup();
        Label("Enabled");
        state.lightingDirty |= ImGui::Checkbox("##celen", &state.celEnabled);
        if (state.celEnabled) {
            Label("Bands");
            state.lightingDirty |= ImGui::DragFloat("##celbands", &state.celBands, 0.05f, 2.0f, 6.0f, "%.0f");
            Label("Rim Light");
            state.lightingDirty |= ImGui::DragFloat("##celrim", &state.celRimIntensity, 0.005f, 0.0f, 2.0f);
        }
        EndPropGroup();
    }

    if (GroupHeader("Ink Outlines")) {
        BeginPropGroup();
        Label("Enabled");
        ImGui::Checkbox("##outlineen", &state.outlineEnabled);
        if (state.outlineEnabled) {
            Label("Thickness");
            ImGui::DragFloat("##outthick", &state.outlineThickness, 0.025f, 0.5f, 4.0f);
            Label("Color");
            ImGui::ColorEdit3("##outcol", state.outlineColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        }
        EndPropGroup();
    }

    if (GroupHeader("Hand-Drawn")) {
        BeginPropGroup();
        Label("Paper Grain");
        ImGui::DragFloat("##ppgrain", &state.paperGrainIntensity, 0.001f, 0.0f, 0.15f, "%.3f");
        Label("Hatching");
        ImGui::DragFloat("##hatchint", &state.hatchingIntensity, 0.005f, 0.0f, 1.0f);
        if (state.hatchingIntensity > 0.001f) {
            Label("Scale");
            ImGui::DragFloat("##hatchscl", &state.hatchingScale, 0.05f, 1.0f, 16.0f);
        }
        EndPropGroup();
    }
}

// ─────────────────────────────────────────────────────────────
//  TAB: SETTINGS — Grid, placement, character
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawGridSection() {
    if (!GroupHeader("Grid & Snap", true)) return;
    BeginPropGroup();

    Label("Show Grid");
    bool showGrid = (m_gridExtent > 0);
    if (ImGui::Checkbox("##showgrid", &showGrid)) m_gridExtent = showGrid ? 50 : 0;

    Label("Grid Size");
    ImGui::DragFloat("##gridsize", &m_gridSize, 0.05f, 0.25f, 10.0f, "%.2f");

    ThinSeparator();

    Label("Snap");
    ImGui::Checkbox("##snapon", &m_gridSnap);
    ImGui::SameLine(); Muted("(`)");
    Label("Snap Size");
    ImGui::DragFloat("##snapsize", &m_gridSnapSize, 0.05f, 0.1f, 10.0f, "%.2f");

    ThinSeparator();

    Label("Gizmo Size");
    ImGui::DragFloat("##gizmosize", &m_gizmoLength, 0.05f, 0.5f, 10.0f, "%.1f");
    Label("Cam Speed");
    ImGui::DragFloat("##camspeed", &m_camSpeed, 0.25f, 1.0f, 100.0f, "%.1f");
    Label("Cam FOV");
    ImGui::DragFloat("##camfov", &m_camFOV, 0.25f, 30.0f, 120.0f, "%.0f");

    EndPropGroup();
}

void LevelEditorWindow::DrawPlacementSection() {
    if (m_currentTool != LevelEditTool::Place) return;
    if (!GroupHeader("Placement Tool", true)) return;
    BeginPropGroup();

    Label("Mesh");
    const char* meshTypes[] = {"Cube", "Custom"};
    int mt = static_cast<int>(m_placeMeshType);
    if (ImGui::Combo("##plcmesh", &mt, meshTypes, 2)) m_placeMeshType = static_cast<MeshType>(mt);

    if (m_placeMeshType == MeshType::Custom) {
        Label("Model");
        auto modelNames = ResourceManager::Get().GetModelNames();
        if (ImGui::BeginCombo("##plcmodel", m_placeMeshName.empty() ? "<select>" : m_placeMeshName.c_str())) {
            for (auto& mname : modelNames) {
                bool sel = (mname == m_placeMeshName);
                if (ImGui::Selectable(mname.c_str(), sel)) m_placeMeshName = mname;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    Label("Color");
    ImGui::ColorEdit4("##plccol", m_placeColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    EndPropGroup();
}

// ─────────────────────────────────────────────────────────────
//  Culling & Level Streaming settings
// ─────────────────────────────────────────────────────────────
void LevelEditorWindow::DrawCullingSection(EditorState& state) {
    if (!GroupHeader("EYE  Culling")) return;
    BeginPropGroup();

    Label("Frustum Culli");
    ImGui::Checkbox("##cullfrustum", &state.cullingEnabled);

    Label("BVH Accelerat");
    ImGui::Checkbox("##cullbvh", &state.bvhAcceleration);
    if (state.bvhAcceleration) {
        ImGui::SameLine();
        Muted(("(" + std::to_string(state.bvhNodeCount) + " nodes, depth " + std::to_string(state.bvhDepth) + ")").c_str());
    }

    Label("Level Streami");
    ImGui::Checkbox("##cullstream", &state.streamingEnabled);

    Label("Stream Distan");
    ImGui::DragFloat("##cullstreamdist", &state.streamDistance, 1.0f, 10.0f, 1000.0f, "%.0f");

    ThinSeparator();
    Muted("  Category Distances");

    Label("Building");
    ImGui::DragFloat("##cullbuilding", &state.cullDistBuilding, 0.01f, 0.0f, 2.0f, "%.0f%%");

    Label("Prop");
    ImGui::DragFloat("##cullprop", &state.cullDistProp, 0.01f, 0.0f, 2.0f, "%.0f%%");

    Label("Detail");
    ImGui::DragFloat("##culldetail", &state.cullDistDetail, 0.01f, 0.0f, 2.0f, "%.0f%%");

    Label("Foliage");
    ImGui::DragFloat("##cullfoliage", &state.cullDistFoliage, 0.01f, 0.0f, 2.0f, "%.0f%%");

    Muted("  Always = never distance-culled");

    ThinSeparator();

    Label("Shadow Distan");
    ImGui::DragFloat("##cullshadow", &state.shadowCullDistance, 1.0f, 10.0f, 500.0f, "%.0f");

    Label("Debug Bounds");
    ImGui::Checkbox("##culldbg", &state.showCullingDebug);

    EndPropGroup();
}

void LevelEditorWindow::DrawCharacterSection(EditorState& state) {
    Label("FPS Mode");
    ImGui::Checkbox("##charmode", &state.characterMode);
    ImGui::SameLine(); ImGui::TextDisabled("(F8)");
    Label("Show Body");
    ImGui::Checkbox("##charbody", &state.charShowBody);

    if (!state.characterMode) return;

    if (GroupHeader("Movement##chr", true)) {
        BeginPropGroup();
        Label("Speed");
        ImGui::DragFloat("##charspd", &state.charMoveSpeed, 0.05f, 1.0f, 20.0f);
        Label("Sprint");
        ImGui::DragFloat("##charsprint", &state.charSprintMult, 0.05f, 1.0f, 5.0f);
        Label("Jump");
        ImGui::DragFloat("##charjump", &state.charJumpForce, 0.05f, 1.0f, 20.0f);
        Label("Gravity");
        ImGui::DragFloat("##chargrav", &state.charGravity, 0.25f, 1.0f, 50.0f);
        Label("Ground Y");
        ImGui::DragFloat("##chargy", &state.charGroundY, 0.05f, -10.0f, 10.0f);
        Label("Eye Height");
        ImGui::DragFloat("##chareye", &state.charEyeHeight, 0.025f, 0.5f, 3.0f);
        EndPropGroup();
    }

    if (GroupHeader("Crouch##chr")) {
        BeginPropGroup();
        Label("Eye Height");
        ImGui::DragFloat("##croucheye", &state.charCrouchEyeHeight, 0.025f, 0.3f, 1.5f);
        Label("Speed Mult");
        ImGui::DragFloat("##crouchspd", &state.charCrouchSpeedMult, 0.025f, 0.1f, 1.0f);
        Label("Transition");
        ImGui::DragFloat("##crouchtrans", &state.charCrouchTransSpeed, 0.25f, 1.0f, 20.0f);
        ImGui::TextDisabled("Hold Ctrl to crouch");
        EndPropGroup();
    }

    if (GroupHeader("Camera Effects##chr")) {
        BeginPropGroup();
        Label("Tilt");
        ImGui::Checkbox("##tiltena", &state.charCameraTiltEnabled);
        if (state.charCameraTiltEnabled) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(60);
            ImGui::DragFloat("##tiltamt", &state.charCameraTiltAmount, 0.05f, 0.5f, 8.0f, "%.1f\xc2\xb0");
            Label("Tilt Speed");
            ImGui::DragFloat("##tiltspd", &state.charCameraTiltSpeed, 0.25f, 1.0f, 20.0f);
        }

        ThinSeparator();

        Label("Head Bob");
        ImGui::Checkbox("##headbob", &state.charHeadBobEnabled);
        if (state.charHeadBobEnabled) {
            Label("Bob Speed");
            ImGui::DragFloat("##hbspd", &state.charHeadBobSpeed, 0.25f, 2.0f, 30.0f);
            Label("Bob Amount");
            ImGui::DragFloat("##hbamt", &state.charHeadBobAmount, 0.0025f, 0.0f, 0.2f, "%.3f");
            Label("Sway");
            ImGui::DragFloat("##hbsway", &state.charHeadBobSway, 0.0025f, 0.0f, 0.1f, "%.3f");
        }
        EndPropGroup();
    }

    if (GroupHeader("Body Colors##chr")) {
        BeginPropGroup();
        Label("Head");
        ImGui::ColorEdit3("##colhead", state.charHeadColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Torso");
        ImGui::ColorEdit3("##coltorso", state.charTorsoColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Arms");
        ImGui::ColorEdit3("##colarms", state.charArmsColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        Label("Legs");
        ImGui::ColorEdit3("##collegs", state.charLegsColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        EndPropGroup();
    }

    if (GroupHeader("Character Model##chr")) {
        BeginPropGroup();
        Label("Scale");
        ImGui::DragFloat("##charscale", &state.charModelScale, 0.0025f, 0.01f, 1.0f, "%.3f");
        EndPropGroup();
    }
}

} // namespace WT
