#include "Application.h"
#include "Util/Log.h"
#include "Editor/LevelFile.h"
#include "PCG/LevelGenerator.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>
#include <filesystem>

namespace WT {

static std::string ResolveStartupLevelPath(const std::wstring& exeDir, const std::string& requestedPath) {
    if (requestedPath.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path requested(requestedPath);
    if (requested.is_absolute() && std::filesystem::exists(requested, ec)) {
        return requested.string();
    }

    const std::filesystem::path exeBase(exeDir);
    const std::filesystem::path candidates[] = {
        requested,
        exeBase / requested,
        exeBase / ".." / ".." / requested,
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec)) {
            return std::filesystem::weakly_canonical(candidate, ec).string();
        }
        ec.clear();
    }

    return requestedPath;
}

// ==================== Win32 Window ====================

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return Application::Get().HandleMessage(hwnd, msg, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Forward to ImGui first — if it consumes the event, don't pass to game input
    if (m_editorVisible && m_editorUI.HandleMessage(hwnd, msg, wParam, lParam))
        return 0;

    m_input.ProcessMessage(msg, wParam, lParam);

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            m_running = false;
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            m_width = w;
            m_height = h;

            if (wParam == SIZE_MINIMIZED) {
                m_minimized = true;
            } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
                m_minimized = false;
                if (!m_resizing && m_rendererReady && w > 0 && h > 0) {
                    m_renderer.OnResize(w, h);
                    m_postProcess.OnResize(m_renderer.GetDevice(), w, h);
                    m_ssao.OnResize(m_renderer.GetDevice(), w, h);
                    m_fsrUpscaler.OnResize(m_renderer.GetDevice(), w, h);
                    if (m_editorState.fsrEnabled) {
                        int rw, rh;
                        m_fsrUpscaler.GetRenderResolution(w, h, static_cast<FSRQuality>(m_editorState.fsrQuality), rw, rh);
                        m_fsrUpscaler.UpdateRenderTarget(m_renderer.GetDevice(), rw, rh);
                    }
                    m_camera.UpdateProjection(m_renderer.GetAspectRatio());
                }
            }
            return 0;
        }

        case WM_ENTERSIZEMOVE:
            m_resizing = true;
            return 0;

        case WM_EXITSIZEMOVE:
            m_resizing = false;
            if (m_rendererReady && m_width > 0 && m_height > 0) {
                m_renderer.OnResize(m_width, m_height);
                m_postProcess.OnResize(m_renderer.GetDevice(), m_width, m_height);
                m_ssao.OnResize(m_renderer.GetDevice(), m_width, m_height);
                m_fsrUpscaler.OnResize(m_renderer.GetDevice(), m_width, m_height);
                if (m_editorState.fsrEnabled) {
                    int rw, rh;
                    m_fsrUpscaler.GetRenderResolution(m_width, m_height, static_cast<FSRQuality>(m_editorState.fsrQuality), rw, rh);
                    m_fsrUpscaler.UpdateRenderTarget(m_renderer.GetDevice(), rw, rh);
                }
                m_camera.UpdateProjection(m_renderer.GetAspectRatio());
            }
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 240;
            return 0;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                // Window lost focus — release cursor
                if (m_input.IsCursorLocked()) {
                    m_input.SetCursorLocked(false);
                }
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;  // Prevent GDI background erase — DirectX handles rendering

        case WM_PAINT:
            if (!m_rendererReady) {
                // Before renderer is ready, just validate the window region
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;  // Fall through to DefWindowProc when renderer is active
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool Application::CreateAppWindow(HINSTANCE hInstance, int width, int height) {
    m_hInstance = hInstance;

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // Don't paint GDI background — DirectX handles rendering
    wc.lpszClassName = L"WarTimesClass";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        LOG_ERROR("Failed to register window class");
        return false;
    }

    // Adjust window rect so client area is exactly width x height
    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    m_hwnd = CreateWindowEx(
        0,
        L"WarTimesClass",
        m_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create window");
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(m_hwnd);

    // Read the actual client size after maximize
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    m_width  = clientRect.right - clientRect.left;
    m_height = clientRect.bottom - clientRect.top;

    LOG_INFO("Window created (maximized): %dx%d", m_width, m_height);
    return true;
}

// ==================== Init ====================

bool Application::Init(HINSTANCE hInstance, int width, int height) {
    if (!CreateAppWindow(hInstance, width, height)) return false;
    // Use actual client dimensions (may differ from requested if window was maximized)
    if (!m_renderer.Init(m_hwnd, m_width, m_height))    return false;
    m_rendererReady = true;

    m_input.Init(m_hwnd);
    m_camera.Init(70.0f, m_renderer.GetAspectRatio(), 0.1f, 500.0f);
    m_camera.SetPosition(0.0f, 1.4f, -5.0f);  // Eye height on ground

    // Init character controller (starts at origin, ground level)
    m_character.Init({ 0.0f, 0.0f, -5.0f }, 0.0f);

    if (!InitGraphics()) return false;

    // Initialize editor systems
    if (!m_editorUI.Init(m_hwnd, m_renderer.GetDevice(), m_renderer.GetContext())) {
        LOG_ERROR("Editor UI init failed");
        return false;
    }
    m_editorPanels.Init();

    // Initialize level editor (separate window, hidden by default)
    if (!m_levelEditor.Init(m_renderer.GetDevice(), m_hInstance, 1200, 800)) {
        LOG_WARN("Level Editor window init failed — continuing without it");
    }

    // Share rendering resources with level editor so it can draw entities
    {
        LevelEditorResources res;
        res.voxelShader  = &m_voxelShader;
        res.groundShader = &m_groundShader;
        res.cubeMesh     = &m_cubeMesh;
        res.groundMesh   = &m_groundMesh;
        res.cbPerFrame   = &m_cbPerFrame;
        res.cbPerObject  = &m_cbPerObject;
        res.cbLighting   = &m_cbLighting;
        m_levelEditor.SetResources(res);
    }

    m_timer.Reset();
    m_running = true;

    // Start in FPS mode with cursor locked (F6 to open editor)
    m_editorVisible = false;
    m_input.SetCursorLocked(true);

    // Auto-load default level or generate a random one
    if (!m_startupLevelPath.empty()) {
        const std::string resolvedPath = ResolveStartupLevelPath(m_exeDir, m_startupLevelPath);
        if (LevelFile::Load(resolvedPath, m_editorState.scene)) {
            m_editorState.entityDirty = true;
            m_editorState.physicsRebuildRequested = true;
            LOG_INFO("Loaded startup level: %s (%d entities)", resolvedPath.c_str(), m_editorState.scene.GetEntityCount());
        } else {
            LOG_ERROR("Failed to load startup level: %s", resolvedPath.c_str());
            m_editorState.scene.Clear();
            m_editorState.entityDirty = true;
            m_editorState.physicsRebuildRequested = true;
        }
    } else if (m_editorState.pcgOnLaunch) {
        LevelGenerator gen;
        gen.Generate(m_editorState.scene);
        m_editorState.entityDirty = true;
        m_editorState.physicsRebuildRequested = true;
        LOG_INFO("PCG: Generated random level on launch (seed %u, %d entities)",
                 gen.GetUsedSeed(), m_editorState.scene.GetEntityCount());
    } else {
        // Start with a blank level (no entities)
        m_editorState.scene.Clear();
        m_editorState.entityDirty = true;
        m_editorState.physicsRebuildRequested = true;
        LOG_INFO("Started with blank level");
    }

    LOG_INFO("Application initialized successfully");
    return true;
}

std::wstring Application::GetExeDir() const {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);
    return dir;
}

bool Application::InitGraphics() {
    auto* device = m_renderer.GetDevice();

    // Resolve shader paths relative to executable directory
    m_exeDir = GetExeDir();
    std::wstring shaderDir = m_exeDir + L"shaders/";

    LOG_INFO("Loading shaders from exe directory");

    // Load voxel shaders
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    if (!m_voxelShader.LoadVS(device, shaderDir + L"VoxelVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    if (!m_voxelShader.LoadPS(device, shaderDir + L"VoxelPS.hlsl", "PSMain"))
        return false;

    // Sky shader — fullscreen triangle, no input layout needed (uses SV_VertexID)
    // Load with a dummy layout since our Shader class requires one for VS
    if (!m_skyShader.LoadVS(device, shaderDir + L"SkyVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    if (!m_skyShader.LoadPS(device, shaderDir + L"SkyPS.hlsl", "PSMain"))
        return false;

    // Ground plane shader — procedural checkerboard
    if (!m_groundShader.LoadVS(device, shaderDir + L"GroundVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    if (!m_groundShader.LoadPS(device, shaderDir + L"GroundPS.hlsl", "PSMain"))
        return false;

    // Shadow depth shader — depth-only pass from light’s perspective
    if (!m_shadowShader.LoadVS(device, shaderDir + L"ShadowVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    // No pixel shader — depth-only rendering

    // Create constant buffers (PerFrame b0, PerObject b1, Lighting b2, Sky b3, Shadow b4)
    if (!m_cbPerFrame.Init(device))  return false;
    if (!m_cbPerObject.Init(device)) return false;
    if (!m_cbLighting.Init(device))  return false;
    if (!m_cbSky.Init(device))       return false;
    if (!m_cbShadow.Init(device))    return false;

    // Shadow map
    if (!m_shadowMap.Init(device, 2048)) return false;

    // Post-processing pipeline
    if (!m_postProcess.Init(device, m_width, m_height, shaderDir)) return false;

    // SSAO
    if (!m_ssao.Init(device, m_width, m_height, shaderDir)) return false;

    // FSR upscaler
    if (!m_fsrUpscaler.Init(device, m_width, m_height, shaderDir)) return false;

    // Init debug renderer
    if (!m_debugRenderer.Init(device, shaderDir)) return false;

    // Create default textures
    if (!m_defaultWhite.CreateFromColor(device, 1.0f, 1.0f, 1.0f, 1.0f)) return false;

    // Create test cube
    if (!CreateCubeMesh()) return false;

    // Create sphere mesh (for agents)
    if (!CreateSphereMesh()) return false;

    // Create rotor mesh (for drone propellers)
    if (!CreateRotorMesh()) return false;

    // Create ground plane (single large quad)
    if (!CreateGroundMesh()) return false;

    // AI Navigation Grid
    m_navGrid.Init(m_editorState.navGridWidth, m_editorState.navGridHeight,
                   m_editorState.navCellSize,
                   m_editorState.navOriginX, m_editorState.navOriginZ,
                   m_editorState.navGridY);

    // AI System
    m_aiSystem.Init();

    // Physics / Collision World
    m_physicsWorld.Init();

    // Fracture & Debris Systems
    m_fractureSystem.Init();
    m_debrisManager.Init();

    // Networking
    m_networkManager.Init();

    // Weapon System
    m_weaponSystem.Init();
    m_weaponSystem.SetPresetsDir(m_exeDir + L"../../weapons/");
    m_weaponSystem.LoadAllPresets();
    m_hud.Init();
    m_editorState.pWeaponSystem = &m_weaponSystem;
    m_editorState.pLevelEditor  = &m_levelEditor;

    // Item & Inventory Systems
    ItemDatabase::Get().Init();
    LootTableRegistry::Get().Init();
    m_inventory.Init();
    m_inventoryUI.Init();

    // Give starting items (pistol + some ammo)
    m_inventory.AddItem(1002, 1);  // Pistol
    m_inventory.AddItem(2001, 60); // Rifle Ammo
    m_inventory.AddItem(2002, 30); // Pistol Ammo
    m_inventory.AddItem(3002, 3);  // Bandages

    // Particle / FX System
    // (GPU particle system handles all particle effects now)

    // GPU Particle System (compute shader driven)
    if (!m_gpuParticles.Init(device, shaderDir, 65536)) {
        LOG_ERROR("GPU Particle System init failed — falling back to CPU particles");
    }

    // Decal System
    m_decals.Init(256);

    // Resource Manager
    ResourceManager::Get().Init(device, shaderDir);

    // Load models (.mesh binary files)
    // Use source directory (relative to exe: ../../models/) so hot-reload picks up
    // new exports from Blender without needing a rebuild
    std::wstring modelsDir = m_exeDir + L"../../models/";
    int meshCount = ResourceManager::Get().LoadMeshDirectory(modelsDir);
    LOG_INFO("Loaded %d models", meshCount);

    // Load textures
    int texCount = 0;

    // Create dev prototype grid textures FIRST (overrides any broken PNGs)
    texCount += ResourceManager::Get().CreateDevTextures();

    // Load textures from source directory (relative to exe: ../../textures/)
    // so hot-reload picks up new files without needing a rebuild
    std::wstring texturesDir = m_exeDir + L"../../textures/";
    texCount += ResourceManager::Get().LoadTextureDirectory(texturesDir);
    LOG_INFO("Loaded %d textures", texCount);

    // Create default white texture for untextured meshes
    ResourceManager::Get().CreateColorTexture("_white", 1.0f, 1.0f, 1.0f, 1.0f);

    LOG_INFO("Graphics resources initialized");
    return true;
}

bool Application::CreateCubeMesh() {
    // Unit cube centered at origin with per-face normals and colors
    // Each face has 4 unique vertices (for correct normals)
    using V = VertexPosNormalColor;

    // Face colors (BattleBit low-poly style — earth tones)
    XMFLOAT4 green  = { 0.4f, 0.7f, 0.3f, 1.0f };
    XMFLOAT4 brown  = { 0.5f, 0.35f, 0.2f, 1.0f };
    XMFLOAT4 gray   = { 0.6f, 0.6f, 0.6f, 1.0f };

    std::vector<V> vertices = {
        // Front face (+Z) — green (grass-like top)
        {{ -0.5f, -0.5f,  0.5f }, { 0, 0, 1 }, green, {0,0} },
        {{  0.5f, -0.5f,  0.5f }, { 0, 0, 1 }, green, {1,0} },
        {{  0.5f,  0.5f,  0.5f }, { 0, 0, 1 }, green, {1,1} },
        {{ -0.5f,  0.5f,  0.5f }, { 0, 0, 1 }, green, {0,1} },

        // Back face (-Z)
        {{  0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, brown, {0,0} },
        {{ -0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, brown, {1,0} },
        {{ -0.5f,  0.5f, -0.5f }, { 0, 0, -1 }, brown, {1,1} },
        {{  0.5f,  0.5f, -0.5f }, { 0, 0, -1 }, brown, {0,1} },

        // Top face (+Y) — green
        {{ -0.5f,  0.5f,  0.5f }, { 0, 1, 0 }, green, {0,0} },
        {{  0.5f,  0.5f,  0.5f }, { 0, 1, 0 }, green, {1,0} },
        {{  0.5f,  0.5f, -0.5f }, { 0, 1, 0 }, green, {1,1} },
        {{ -0.5f,  0.5f, -0.5f }, { 0, 1, 0 }, green, {0,1} },

        // Bottom face (-Y)
        {{ -0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, brown, {0,0} },
        {{  0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, brown, {1,0} },
        {{  0.5f, -0.5f,  0.5f }, { 0, -1, 0 }, brown, {1,1} },
        {{ -0.5f, -0.5f,  0.5f }, { 0, -1, 0 }, brown, {0,1} },

        // Right face (+X)
        {{  0.5f, -0.5f,  0.5f }, { 1, 0, 0 }, gray, {0,0} },
        {{  0.5f, -0.5f, -0.5f }, { 1, 0, 0 }, gray, {1,0} },
        {{  0.5f,  0.5f, -0.5f }, { 1, 0, 0 }, gray, {1,1} },
        {{  0.5f,  0.5f,  0.5f }, { 1, 0, 0 }, gray, {0,1} },

        // Left face (-X)
        {{ -0.5f, -0.5f, -0.5f }, { -1, 0, 0 }, gray, {0,0} },
        {{ -0.5f, -0.5f,  0.5f }, { -1, 0, 0 }, gray, {1,0} },
        {{ -0.5f,  0.5f,  0.5f }, { -1, 0, 0 }, gray, {1,1} },
        {{ -0.5f,  0.5f, -0.5f }, { -1, 0, 0 }, gray, {0,1} },
    };

    std::vector<UINT> indices = {
        0,1,2,  0,2,3,       // Front
        4,5,6,  4,6,7,       // Back
        8,9,10, 8,10,11,     // Top
        12,13,14, 12,14,15,  // Bottom
        16,17,18, 16,18,19,  // Right
        20,21,22, 20,22,23,  // Left
    };

    return m_cubeMesh.Create(m_renderer.GetDevice(), vertices, indices);
}

bool Application::CreateSphereMesh() {
    // UV sphere — 16 slices x 12 stacks, unit radius centered at origin
    using V = VertexPosNormalColor;
    const int slices = 16;
    const int stacks = 12;
    XMFLOAT4 white = { 1.0f, 1.0f, 1.0f, 1.0f };

    std::vector<V> vertices;
    std::vector<UINT> indices;

    // Generate vertices
    for (int st = 0; st <= stacks; st++) {
        float phi = XM_PI * static_cast<float>(st) / static_cast<float>(stacks);
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);
        for (int sl = 0; sl <= slices; sl++) {
            float theta = 2.0f * XM_PI * static_cast<float>(sl) / static_cast<float>(slices);
            float sinTheta = sinf(theta);
            float cosTheta = cosf(theta);

            float x = sinPhi * cosTheta;
            float y = cosPhi;
            float z = sinPhi * sinTheta;

            float u = static_cast<float>(sl) / static_cast<float>(slices);
            float v = static_cast<float>(st) / static_cast<float>(stacks);

            V vert;
            vert.Position = { x * 0.5f, y * 0.5f, z * 0.5f }; // radius 0.5 to match cube extents
            vert.Normal   = { x, y, z };
            vert.Color    = white;
            vert.TexCoord = { u, v };
            vertices.push_back(vert);
        }
    }

    // Generate indices
    for (int st = 0; st < stacks; st++) {
        for (int sl = 0; sl < slices; sl++) {
            int first  = st * (slices + 1) + sl;
            int second = first + slices + 1;
            indices.push_back(static_cast<UINT>(first));
            indices.push_back(static_cast<UINT>(second));
            indices.push_back(static_cast<UINT>(first + 1));
            indices.push_back(static_cast<UINT>(second));
            indices.push_back(static_cast<UINT>(second + 1));
            indices.push_back(static_cast<UINT>(first + 1));
        }
    }

    return m_sphereMesh.Create(m_renderer.GetDevice(), vertices, indices);
}

bool Application::CreateRotorMesh() {
    // Propeller disc — 3 blades radiating from a small central hub
    // Each blade is a thin flat wedge. Looks like a real propeller.
    using V = VertexPosNormalColor;
    std::vector<V> vertices;
    std::vector<UINT> indices;

    const float hubR    = 0.06f;   // Central hub radius
    const float bladeR  = 0.5f;    // Blade tip radius
    const float halfH   = 0.015f;  // Very thin
    const int   blades  = 3;
    const float bladeArc = 0.28f;  // Blade angular width (radians, ~16 degrees)
    const int   radSteps = 6;      // Steps along blade length
    const int   arcSteps = 3;      // Steps across blade width
    XMFLOAT4 hubCol   = { 0.35f, 0.35f, 0.38f, 1.0f }; // Dark metal hub
    XMFLOAT4 bladeCol = { 0.55f, 0.55f, 0.6f, 1.0f };  // Lighter metal blade

    // Central hub disc (simple circle)
    UINT hubCenter = static_cast<UINT>(vertices.size());
    vertices.push_back({{ 0, halfH, 0 }, { 0, 1, 0 }, hubCol, { 0.5f, 0.5f }});
    const int hubSegs = 12;
    for (int i = 0; i <= hubSegs; i++) {
        float a = 2.0f * XM_PI * static_cast<float>(i) / static_cast<float>(hubSegs);
        vertices.push_back({{ hubR * cosf(a), halfH, hubR * sinf(a) }, { 0, 1, 0 }, hubCol, { 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a) }});
    }
    for (int i = 0; i < hubSegs; i++) {
        indices.push_back(hubCenter);
        indices.push_back(hubCenter + 1 + i);
        indices.push_back(hubCenter + 1 + ((i + 1) % hubSegs));
    }
    // Hub bottom
    UINT hubBotCenter = static_cast<UINT>(vertices.size());
    vertices.push_back({{ 0, -halfH, 0 }, { 0, -1, 0 }, hubCol, { 0.5f, 0.5f }});
    for (int i = 0; i <= hubSegs; i++) {
        float a = 2.0f * XM_PI * static_cast<float>(i) / static_cast<float>(hubSegs);
        vertices.push_back({{ hubR * cosf(a), -halfH, hubR * sinf(a) }, { 0, -1, 0 }, hubCol, { 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a) }});
    }
    for (int i = 0; i < hubSegs; i++) {
        indices.push_back(hubBotCenter);
        indices.push_back(hubBotCenter + 1 + ((i + 1) % hubSegs));
        indices.push_back(hubBotCenter + 1 + i);
    }

    // 3 blades
    for (int b = 0; b < blades; b++) {
        float baseAngle = (2.0f * XM_PI * b) / static_cast<float>(blades);
        UINT bladeStart = static_cast<UINT>(vertices.size());

        // Generate blade grid vertices (top face)
        for (int ri = 0; ri <= radSteps; ri++) {
            float t = static_cast<float>(ri) / static_cast<float>(radSteps);
            float r = hubR + (bladeR - hubR) * t;
            // Blade tapers: wider at hub, narrower at tip
            float arcW = bladeArc * (1.0f - t * 0.5f);
            for (int ai = 0; ai <= arcSteps; ai++) {
                float at = static_cast<float>(ai) / static_cast<float>(arcSteps);
                float a = baseAngle + (at - 0.5f) * arcW;
                // Color gradient: darker at hub, lighter at tip
                XMFLOAT4 c = {
                    bladeCol.x * (0.7f + 0.3f * t),
                    bladeCol.y * (0.7f + 0.3f * t),
                    bladeCol.z * (0.7f + 0.3f * t),
                    1.0f };
                vertices.push_back({{ r * cosf(a), halfH, r * sinf(a) }, { 0, 1, 0 }, c, { t, at }});
            }
        }
        // Blade top face indices
        for (int ri = 0; ri < radSteps; ri++) {
            for (int ai = 0; ai < arcSteps; ai++) {
                UINT tl = bladeStart + ri * (arcSteps + 1) + ai;
                UINT tr = tl + 1;
                UINT bl = tl + (arcSteps + 1);
                UINT br = bl + 1;
                indices.push_back(tl); indices.push_back(bl); indices.push_back(br);
                indices.push_back(tl); indices.push_back(br); indices.push_back(tr);
            }
        }

        // Bottom face (same grid, flipped normal and winding)
        UINT bladeBotStart = static_cast<UINT>(vertices.size());
        for (int ri = 0; ri <= radSteps; ri++) {
            float t = static_cast<float>(ri) / static_cast<float>(radSteps);
            float r = hubR + (bladeR - hubR) * t;
            float arcW = bladeArc * (1.0f - t * 0.5f);
            for (int ai = 0; ai <= arcSteps; ai++) {
                float at = static_cast<float>(ai) / static_cast<float>(arcSteps);
                float a = baseAngle + (at - 0.5f) * arcW;
                XMFLOAT4 c = {
                    bladeCol.x * (0.7f + 0.3f * t),
                    bladeCol.y * (0.7f + 0.3f * t),
                    bladeCol.z * (0.7f + 0.3f * t),
                    1.0f };
                vertices.push_back({{ r * cosf(a), -halfH, r * sinf(a) }, { 0, -1, 0 }, c, { t, at }});
            }
        }
        for (int ri = 0; ri < radSteps; ri++) {
            for (int ai = 0; ai < arcSteps; ai++) {
                UINT tl = bladeBotStart + ri * (arcSteps + 1) + ai;
                UINT tr = tl + 1;
                UINT bl = tl + (arcSteps + 1);
                UINT br = bl + 1;
                indices.push_back(tl); indices.push_back(br); indices.push_back(bl);
                indices.push_back(tl); indices.push_back(tr); indices.push_back(br);
            }
        }
    }

    // ---- Guard ring / shroud around the blade tips ----
    const float ringR     = bladeR + 0.03f; // Slightly outside blade tips
    const float ringTube  = 0.025f;         // Tube thickness of the ring
    const int   ringSegs  = 24;             // Segments around the ring circle
    const int   tubeSegs  = 6;              // Segments around the tube cross-section
    XMFLOAT4 ringCol = { 0.3f, 0.3f, 0.33f, 1.0f }; // Dark metal frame

    for (int i = 0; i <= ringSegs; i++) {
        float theta = 2.0f * XM_PI * static_cast<float>(i) / static_cast<float>(ringSegs);
        float ct = cosf(theta);
        float st = sinf(theta);
        for (int j = 0; j <= tubeSegs; j++) {
            float phi = 2.0f * XM_PI * static_cast<float>(j) / static_cast<float>(tubeSegs);
            float cp = cosf(phi);
            float sp = sinf(phi);

            float x = (ringR + ringTube * cp) * ct;
            float y = ringTube * sp;
            float z = (ringR + ringTube * cp) * st;
            // Normal points outward from tube center
            float nx = cp * ct;
            float ny = sp;
            float nz = cp * st;

            float u = static_cast<float>(i) / static_cast<float>(ringSegs);
            float v = static_cast<float>(j) / static_cast<float>(tubeSegs);
            vertices.push_back({{ x, y, z }, { nx, ny, nz }, ringCol, { u, v }});
        }
    }
    UINT ringBase = static_cast<UINT>(vertices.size()) - (ringSegs + 1) * (tubeSegs + 1);
    for (int i = 0; i < ringSegs; i++) {
        for (int j = 0; j < tubeSegs; j++) {
            UINT cur  = ringBase + i * (tubeSegs + 1) + j;
            UINT next = ringBase + (i + 1) * (tubeSegs + 1) + j;
            indices.push_back(cur);     indices.push_back(next);     indices.push_back(next + 1);
            indices.push_back(cur);     indices.push_back(next + 1); indices.push_back(cur + 1);
        }
    }

    return m_rotorMesh.Create(m_renderer.GetDevice(), vertices, indices);
}

bool Application::CreateGroundMesh() {
    // Large flat quad for the ground plane
    using V = VertexPosNormalColor;
    float s = 200.0f;  // Half-extent
    XMFLOAT4 groundCol = { 0.35f, 0.55f, 0.28f, 1.0f };  // Natural green

    std::vector<V> vertices = {
        {{ -s, 0.0f,  s }, { 0, 1, 0 }, groundCol, {0,0} },
        {{  s, 0.0f,  s }, { 0, 1, 0 }, groundCol, {1,0} },
        {{  s, 0.0f, -s }, { 0, 1, 0 }, groundCol, {1,1} },
        {{ -s, 0.0f, -s }, { 0, 1, 0 }, groundCol, {0,1} },
    };

    std::vector<UINT> indices = { 0, 1, 2, 0, 2, 3 };

    return m_groundMesh.Create(m_renderer.GetDevice(), vertices, indices);
}

// ==================== Main Loop ====================

int Application::Run() {
    MSG msg = {};

    // Frame rate limiter — 240 FPS cap
    LARGE_INTEGER qpcFreq, frameStart, frameEnd;
    QueryPerformanceFrequency(&qpcFreq);
    const double targetFrameTime = 1.0 / 240.0; // ~4.167ms

    while (m_running) {
        QueryPerformanceCounter(&frameStart);

        // Process all pending Windows messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!m_running) break;
        if (m_minimized) {
            Sleep(16);
            continue;
        }

        // Tick timer
        m_timer.Tick();

        // Fixed timestep simulation
        while (m_timer.ShouldDoFixedUpdate()) {
            FixedUpdate(m_timer.FixedDeltaTime());
        }

        // Per-frame update
        Update(m_timer.DeltaTime());

        // Render
        Render();

        // Update input state (after everything else — so "pressed this frame" works next frame)
        m_input.Update();

        // Title bar — just the engine name (stats shown in editor menu bar)
        SetWindowText(m_hwnd, m_title.c_str());

        // --- Frame rate limiter: spin-wait to hit 60 FPS (skip when uncapped) ---
        if (!m_editorState.uncapFps) {
            do {
                QueryPerformanceCounter(&frameEnd);
            } while (static_cast<double>(frameEnd.QuadPart - frameStart.QuadPart)
                   / static_cast<double>(qpcFreq.QuadPart) < targetFrameTime);
        }
    }

    return static_cast<int>(msg.wParam);
}

// ==================== Update ====================

void Application::FixedUpdate(float dt) {
    (void)dt;
}

void Application::Update(float dt) {
    (void)dt;

    // --- Network: sync movement settings, then poll/tick ---
    if (m_networkManager.IsServer()) {
        auto& ss = m_networkManager.GetServerState();
        ss.moveSpeed  = m_editorState.charMoveSpeed;
        ss.sprintMult = m_editorState.charSprintMult;
        ss.gravity    = m_editorState.charGravity;
        ss.jumpForce  = m_editorState.charJumpForce;
        ss.groundY    = m_editorState.charGroundY;
    }
    if (m_networkManager.IsClient()) {
        auto& cs = m_networkManager.GetClientState();
        cs.moveSpeed  = m_editorState.charMoveSpeed;
        cs.sprintMult = m_editorState.charSprintMult;
        cs.gravity    = m_editorState.charGravity;
        cs.jumpForce  = m_editorState.charJumpForce;
        cs.groundY    = m_editorState.charGroundY;
    }
    m_networkManager.Update(dt, m_editorState.scene);

    // --- Client: apply received level data from server ---
    if (m_networkManager.ConsumeClientLevelReceived()) {
        m_editorState.scene.Clear();
        const auto& receivedEntities = m_networkManager.GetReceivedLevelEntities();
        for (const auto& ned : receivedEntities) {
            int idx = m_editorState.scene.AddEntity(ned.name, static_cast<MeshType>(ned.meshType));
            Entity& e = m_editorState.scene.GetEntity(idx);
            e.meshName     = ned.meshName;
            e.textureName  = ned.textureName;
            e.textureCacheDirty = true;
            e.position[0]  = ned.position.x; e.position[1] = ned.position.y; e.position[2] = ned.position.z;
            e.rotation[0]  = ned.rotation.x; e.rotation[1] = ned.rotation.y; e.rotation[2] = ned.rotation.z;
            e.scale[0]     = ned.scale.x;    e.scale[1]    = ned.scale.y;    e.scale[2]    = ned.scale.z;
            e.color[0]     = ned.color[0]; e.color[1] = ned.color[1];
            e.color[2]     = ned.color[2]; e.color[3] = ned.color[3];
            e.visible          = ned.visible;
            e.castShadow       = ned.castShadow;
            e.isLadder         = ned.isLadder;
            e.hasGravity       = ned.hasGravity;
            e.destructible     = ned.destructible;
            e.voxelDestruction = ned.voxelDestruction;
            e.smokeOnDamage    = ned.smokeOnDamage;
            e.noCollision      = ned.noCollision;
            e.materialType     = static_cast<MaterialType>(ned.materialType);
            e.health           = ned.maxHealth;
            e.maxHealth        = ned.maxHealth;
            e.debrisCount      = ned.debrisCount;
            e.debrisScale      = ned.debrisScale;
            e.supportedBy      = ned.supportedBy;
            e.breakPieceCount  = ned.breakPieceCount;
            e.voxelRes         = ned.voxelRes;
            if (e.voxelDestruction) e.ResetVoxelMask();
            e.despawnTimer     = ned.despawnTimer;
            e.pickupType       = static_cast<PickupType>(ned.pickupType);
            e.pickupAmount     = ned.pickupAmount;
            e.pickupRadius     = ned.pickupRadius;
            e.pickupBobSpeed   = ned.pickupBobSpeed;
            e.pickupBobHeight  = ned.pickupBobHeight;
            e.pickupSpinSpeed  = ned.pickupSpinSpeed;
            e.pickupRespawnTime = ned.pickupRespawnTime;
            e.damageFlashDuration = ned.damageFlashDuration;
            e.damageFlashColor[0] = ned.damageFlashColor[0];
            e.damageFlashColor[1] = ned.damageFlashColor[1];
            e.damageFlashColor[2] = ned.damageFlashColor[2];
            e.interactType     = static_cast<InteractType>(ned.interactType);
            e.interactRange    = ned.interactRange;
            e.interactPrompt   = ned.interactPrompt;
            e.interactTarget   = ned.interactTarget;
            e.doorOpenAngle    = ned.doorOpenAngle;
            e.doorSlideDist    = ned.doorSlideDist;
            e.doorSpeed        = ned.doorSpeed;
            e.cullCategory     = static_cast<CullCategory>(ned.cullCategory);
        }
        m_editorState.entityDirty = true;
        m_editorState.physicsRebuildRequested = true;
        m_editorState.selectedEntity = -1;
        LOG_INFO("Net: Applied server level (%d entities)", m_editorState.scene.GetEntityCount());
    }

    // --- Client: process server events (VFX, entity removal) ---
    if (m_networkManager.GetRole() == NetworkRole::Client) {
        auto events = m_networkManager.ConsumeClientEvents();
        for (const auto& evt : events) {
            int sceneIdx = m_networkManager.GetReplication().GetSceneIndex(evt.targetNetId);

            switch (evt.eventType) {
                case ServerEventType::VoxelDestroyed: {
                    // Spawn VFX at voxel removal position
                    float matColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
                    MaterialType mat = static_cast<MaterialType>(static_cast<int>(evt.value));
                    if (sceneIdx >= 0) {
                        auto& e = m_editorState.scene.GetEntity(sceneIdx);
                        matColor[0] = e.color[0]; matColor[1] = e.color[1];
                        matColor[2] = e.color[2]; matColor[3] = e.color[3];
                        // Clear the voxel cell immediately for responsiveness
                        if (evt.extra >= 0) e.ClearVoxelCell(evt.extra);
                    }
                    {
                        ParticleEmitterDef sparks, dust; int sCount, dCount;
                        ParticlePresets::GetMaterialImpactDef(mat, matColor, sparks, sCount, dust, dCount);
                        if (sCount > 0) m_gpuParticles.SpawnBurst(evt.position, sparks, sCount);
                        if (dCount > 0) m_gpuParticles.SpawnBurst(evt.position, dust, dCount);
                    }
                    m_gpuParticles.SpawnBurst(evt.position, ParticlePresets::WallChunks(matColor), 6);
                    m_gpuParticles.SpawnBurst(evt.position, ParticlePresets::WallChips(matColor), 12);
                    m_gpuParticles.SpawnBurst(evt.position, ParticlePresets::WallDust(matColor), 5);
                    for (int dc = 0; dc < 3; dc++) {
                        float rx = ((rand() % 200) - 100) / 100.0f * 2.0f;
                        float ry = ((rand() % 200) / 100.0f) * 2.0f + 1.0f;
                        float rz = ((rand() % 200) - 100) / 100.0f * 2.0f;
                        XMFLOAT3 vel = { rx, ry, rz };
                        float s = 0.08f + (rand() % 40) / 1000.0f;
                        XMFLOAT3 debrisScale = { s, s, s };
                        m_debrisManager.SpawnSingle(evt.position, vel, debrisScale, matColor, mat);
                    }
                    m_editorState.physicsRebuildRequested = true;
                    break;
                }

                case ServerEventType::EntityDestroyed: {
                    // Spawn material-based explosion VFX
                    float matColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
                    MaterialType mat = static_cast<MaterialType>(static_cast<int>(evt.value));
                    if (sceneIdx >= 0) {
                        auto& e = m_editorState.scene.GetEntity(sceneIdx);
                        matColor[0] = e.color[0]; matColor[1] = e.color[1];
                        matColor[2] = e.color[2]; matColor[3] = e.color[3];
                    }
                    {
                        ParticleEmitterDef debris, sparks, smoke; int dCnt, sCnt, smCnt;
                        ParticlePresets::GetMaterialExplosionDef(mat, matColor, debris, dCnt, sparks, sCnt, smoke, smCnt);
                        if (dCnt > 0) m_gpuParticles.SpawnBurst(evt.position, debris, dCnt);
                        if (sCnt > 0) m_gpuParticles.SpawnBurst(evt.position, sparks, sCnt);
                        if (smCnt > 0) m_gpuParticles.SpawnBurst(evt.position, smoke, smCnt);
                        m_gpuParticles.SpawnBurst(evt.position, ParticlePresets::ExplosionFlash(), 8);
                    }
                    m_camera.AddScreenShake(0.015f, 0.15f);
                    break;
                }

                case ServerEventType::EntityRemoved: {
                    if (sceneIdx >= 0) {
                        m_editorState.scene.RemoveEntity(sceneIdx);
                        if (m_editorState.selectedEntity == sceneIdx)
                            m_editorState.selectedEntity = -1;
                        else if (m_editorState.selectedEntity > sceneIdx)
                            m_editorState.selectedEntity--;
                        m_editorState.physicsRebuildRequested = true;
                    }
                    m_networkManager.GetReplication().UnregisterEntity(evt.targetNetId);
                    break;
                }

                default:
                    break;
            }
        }
    }

    // --- Client: apply replicated entity state changes ---
    if (m_networkManager.GetRole() == NetworkRole::Client) {
        auto states = m_networkManager.ConsumeReplicationStates();
        for (const auto& st : states) {
            int sceneIdx = m_networkManager.GetReplication().GetSceneIndex(st.netId);
            if (sceneIdx < 0) continue;
            auto& e = m_editorState.scene.GetEntity(sceneIdx);

            e.position[0] = st.position.x; e.position[1] = st.position.y; e.position[2] = st.position.z;
            e.rotation[0] = st.rotation.x; e.rotation[1] = st.rotation.y; e.rotation[2] = st.rotation.z;
            e.scale[0]    = st.scale.x;    e.scale[1]    = st.scale.y;    e.scale[2]    = st.scale.z;
            e.health       = st.health;
            e.visible      = st.visible;
            e.destructible = st.destructible;

            // Interaction state
            e.doorOpen         = st.doorOpen;
            e.switchState      = st.switchState;
            e.pickupCollected  = st.pickupCollected;
            if (e.pickupCollected) e.visible = false;

            // Voxel destruction mask
            if (st.voxelDestruction && e.voxelDestruction) {
                for (int w = 0; w < 8; w++) e.voxelMask[w] = st.voxelMask[w];
                m_editorState.physicsRebuildRequested = true;
            }
        }
    }

    // --- Despawn timer: remove debris entities after their timer expires ---
    {
        bool needsColliderRebuild = false;
        for (int i = m_editorState.scene.GetEntityCount() - 1; i >= 0; i--) {
            auto& e = m_editorState.scene.GetEntity(i);
            if (e.despawnTimer < 0.0f) continue; // no despawn
            e.despawnTimer -= dt;

            // Fade out during last 2 seconds
            if (e.despawnTimer < 2.0f && e.despawnTimer >= 0.0f) {
                e.color[3] = e.despawnTimer / 2.0f;
            }

            if (e.despawnTimer <= 0.0f) {
                m_editorState.scene.RemoveEntity(i);
                if (m_editorState.selectedEntity == i)
                    m_editorState.selectedEntity = -1;
                else if (m_editorState.selectedEntity > i)
                    m_editorState.selectedEntity--;
                needsColliderRebuild = true;
            }
        }
        if (needsColliderRebuild) {
            m_editorState.physicsRebuildRequested = true;
            m_sceneCuller.InvalidateBounds();
        }
    }

    // Suppress game input when editor UI wants it
    bool editorWantsKeyboard = m_editorVisible && m_editorUI.WantsKeyboard();
    bool editorWantsMouse    = m_editorVisible && m_editorUI.WantsMouse();

    // F6: Toggle level editor window
    if (m_input.IsKeyPressed(VK_F6)) {
        m_levelEditor.SetOpen(!m_levelEditor.IsOpen());
        LOG_INFO("Level Editor: %s", m_levelEditor.IsOpen() ? "OPEN" : "CLOSED");
    }

    // Escape: unlock cursor / toggle cursor lock when editor hidden
    if (m_input.IsKeyPressed(VK_ESCAPE)) {
        if (m_editorVisible) {
            // Always unlock when editor is showing
            m_input.SetCursorLocked(false);
        } else {
            m_input.ToggleCursorLock();
        }
    }

    // F1: Toggle wireframe
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F1)) {
        m_editorState.wireframe = !m_editorState.wireframe;
        m_renderer.SetWireframe(m_editorState.wireframe);
        LOG_INFO("Wireframe: %s", m_editorState.wireframe ? "ON" : "OFF");
    }

    // F2: Toggle VSync (only without Ctrl)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F2) && !m_input.IsKeyDown(VK_CONTROL)) {
        m_editorState.vsync = !m_editorState.vsync;
        m_renderer.SetVSync(m_editorState.vsync);
        LOG_INFO("VSync: %s", m_editorState.vsync ? "ON" : "OFF");
    }

    // F10: Toggle FPS uncap
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F10)) {
        m_editorState.uncapFps = !m_editorState.uncapFps;
        LOG_INFO("FPS cap: %s", m_editorState.uncapFps ? "UNCAPPED" : "240 FPS");
    }

    // F3: Cycle MSAA (1 -> 2 -> 4 -> 8 -> 1)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F3)) {
        UINT msaa = m_renderer.GetMSAASamples();
        msaa = (msaa >= 8) ? 1 : msaa * 2;
        m_editorState.msaaSamples = msaa;
        m_renderer.SetMSAA(msaa);
    }

    // F4: Toggle debug rendering
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F4)) {
        m_editorState.showDebug = !m_editorState.showDebug;
        m_debugRenderer.SetEnabled(m_editorState.showDebug);
        LOG_INFO("Debug rendering: %s", m_editorState.showDebug ? "ON" : "OFF");
    }

    // F8: Toggle character mode (FPS ground walk vs fly cam)
    // Disabled when hosting a dedicated server (always fly cam)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F8) && !m_networkManager.IsServer()) {
        m_characterMode = !m_characterMode;
        m_editorState.characterMode = m_characterMode;
        if (m_characterMode) {
            // Entering character mode: snap character to camera XZ, ground Y
            XMFLOAT3 camPos = m_camera.GetPosition();
            m_character.SetPosition({ camPos.x, m_charSettings.groundY, camPos.z });
        }
        LOG_INFO("Character mode: %s", m_characterMode ? "ON (FPS)" : "OFF (Fly Cam)");
    }
    // Sync from editor state (server always stays in fly cam)
    if (m_networkManager.IsServer()) {
        m_characterMode = false;
        m_editorState.characterMode = false;
    } else {
        m_characterMode = m_editorState.characterMode;
    }

    // M: Toggle cursor lock (hide/show mouse)
    if (!editorWantsKeyboard && m_input.IsKeyPressed('M')) {
        m_input.ToggleCursorLock();
        LOG_INFO("Cursor: %s", m_input.IsCursorLocked() ? "LOCKED (hidden)" : "FREE (visible)");
    }

    // L: Toggle laser sight
    if (!editorWantsKeyboard && m_input.IsKeyPressed('L') && !m_input.IsKeyDown(VK_CONTROL)) {
        m_editorState.weaponLaserEnabled = !m_editorState.weaponLaserEnabled;
        LOG_INFO("Laser: %s", m_editorState.weaponLaserEnabled ? "ON" : "OFF");
    }

    // F5: Force shader hot-reload
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F5)) {
        m_voxelShader.Reload(m_renderer.GetDevice());
    }

    // F9: Force model + texture hot-reload (rescan directories)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F9)) {
        ResourceManager::Get().ReloadMeshDirectory();
        ResourceManager::Get().ReloadTextureDirectory();
    }

    // ---- Networking Hotkeys ----

    // Alt+H: Host listen server (play + host in same instance)
    if (!editorWantsKeyboard && m_input.IsKeyDown(VK_MENU) && m_input.IsKeyPressed('H')) {
        if (!m_networkManager.IsOnline()) {
            if (m_networkManager.HostGame(m_networkManager.defaultPort, true)) {
                // Listen server: host plays in character mode
                m_characterMode = true;
                m_editorState.characterMode = true;
                LOG_INFO("Net: Hosting listen server on port %u", m_networkManager.defaultPort);
            } else {
                LOG_INFO("Net: Failed to host");
            }
        }
    }

    // Alt+J: Join server
    if (!editorWantsKeyboard && m_input.IsKeyDown(VK_MENU) && m_input.IsKeyPressed('J')) {
        if (!m_networkManager.IsOnline()) {
            if (m_networkManager.JoinGame("127.0.0.1", m_networkManager.defaultPort)) {
                // Clients always play in character mode
                m_characterMode = true;
                m_editorState.characterMode = true;
                LOG_INFO("Net: Connecting to 127.0.0.1:%u ...", m_networkManager.defaultPort);
            } else {
                LOG_INFO("Net: Failed to connect");
            }
        }
    }

    // Ctrl+L: Leave / disconnect
    if (!editorWantsKeyboard && m_input.IsKeyDown(VK_CONTROL) && m_input.IsKeyPressed('L')) {
        if (m_networkManager.IsOnline()) {
            m_networkManager.LeaveGame();
            LOG_INFO("Net: Disconnected");
        }
    }

    // Ctrl+N: Toggle net overlay
    if (!editorWantsKeyboard && m_input.IsKeyPressed('N') && m_input.IsKeyDown(VK_CONTROL)) {
        m_showNetOverlay = !m_showNetOverlay;
    }

    // Ctrl+P: Push current level to all clients (server hot-swap)
    if (!editorWantsKeyboard && m_input.IsKeyDown(VK_CONTROL) && m_input.IsKeyPressed('P')) {
        if (m_networkManager.IsServer()) {
            m_networkManager.BroadcastLevelData(m_editorState.scene);
        }
    }

    // Alt+O: Launch another game instance (for multiplayer testing)
    if (!editorWantsKeyboard && m_input.IsKeyDown(VK_MENU) && m_input.IsKeyPressed('O')) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        wchar_t cmdLine[MAX_PATH + 32];
        swprintf_s(cmdLine, L"\"%s\" --no-console", exePath);
        if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            LOG_INFO("Net: Launched new game instance");
        } else {
            LOG_INFO("Net: Failed to launch new instance");
        }
    }

    // Ctrl+F2: Switch to 1280x720 windowed mode
    if (m_input.IsKeyDown(VK_CONTROL) && m_input.IsKeyPressed(VK_F2)) {
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        RECT rc = { 0, 0, 1280, 720 };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(m_hwnd, HWND_TOP, 100, 100,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        LOG_INFO("Window: 1280x720 windowed");
    }

    // Ctrl+D: Debug take damage (10 HP)
    if (m_input.IsKeyDown(VK_CONTROL) && m_input.IsKeyPressed('D')) {
        m_character.TakeDamage(10.0f);
        LOG_INFO("Debug: took 10 damage (HP: %.0f)", m_character.GetHealth());
    }

    // Auto hot-reload check every 2 seconds
    m_hotReloadTimer += dt;
    if (m_hotReloadTimer >= 2.0f) {
        m_hotReloadTimer = 0.0f;
        if (m_voxelShader.HasFileChanged()) {
            m_voxelShader.Reload(m_renderer.GetDevice());
        }
        m_gpuParticles.HotReload(m_renderer.GetDevice());
        ResourceManager::Get().ReloadMeshDirectory();
        ResourceManager::Get().ReloadTextureDirectory();
    }

    // Camera rotation from mouse:
    // - When cursor is locked (editor hidden): always rotate
    // - When cursor is free (editor visible): right-click-drag to rotate
    // - In character mode with editor visible: left-click-drag to rotate
    bool doRotate = false;
    if (m_input.IsCursorLocked() && !editorWantsMouse) {
        doRotate = true;
    } else if (!m_input.IsCursorLocked() && !editorWantsMouse) {
        if (m_input.IsRightMouseDown()) {
            doRotate = true;
        } else if (m_characterMode && m_input.IsLeftMouseDown()) {
            doRotate = true;
        }
    }
    if (doRotate) {
        XMFLOAT2 delta = m_input.GetMouseDelta();
        m_camera.Update(delta.x, delta.y);
    }


    // ---- Physics World (must run BEFORE character update) ----
    m_physicsWorld.showDebug = m_editorState.physicsShowDebug;

    // Rebuild static colliders only when something changed (dirty flag)
    if (m_editorState.physicsRebuildRequested) {
        m_editorState.physicsRebuildRequested = false;
        m_physicsWorld.RebuildStaticColliders(m_editorState.scene);
    }

    // Sync physics settings
    {
        PhysicsSettings& physSettings = m_physicsWorld.GetSettings();
        physSettings.gravity       = m_editorState.charGravity;
        physSettings.groundY       = m_editorState.charGroundY;
        physSettings.groundEnabled = true;
    }

    // --- Physics simulation step for dynamic entities ---
    m_physicsWorld.Step(dt, m_physicsWorld.GetSettings());

    // --- Fracture fragment & debris simulation ---
    {
        float groundY = m_physicsWorld.GetSettings().groundY;
        m_fractureSystem.Update(dt, groundY);
        m_debrisManager.Update(dt, groundY);
        m_fractureSystem.Cleanup();
        m_debrisManager.Cleanup();
    }

    // --- Sync entity positions from dynamic physics bodies ---
    for (int i = 0; i < m_physicsWorld.GetBodyCount(); ++i) {
        auto& body = m_physicsWorld.GetBody(i);
        if (body.type == PhysicsBodyType::Dynamic && body.entityIndex >= 0 && !body.sleeping) {
            if (body.entityIndex < m_editorState.scene.GetEntityCount()) {
                auto& entity = m_editorState.scene.GetEntity(body.entityIndex);
                entity.position[0] = body.worldPos.x;
                entity.position[1] = body.worldPos.y;
                entity.position[2] = body.worldPos.z;
            }
        }
    }

    if (m_characterMode) {
        // ---- FPS Character Controller ----
        // Sync editor settings to character settings
        m_charSettings.moveSpeed      = m_editorState.charMoveSpeed;
        m_charSettings.sprintMult     = m_editorState.charSprintMult;
        m_charSettings.jumpForce      = m_editorState.charJumpForce;
        m_charSettings.gravity        = m_editorState.charGravity;
        m_charSettings.groundY        = m_editorState.charGroundY;
        m_charSettings.eyeHeight      = m_editorState.charEyeHeight;
        m_charSettings.crouchEyeHeight  = m_editorState.charCrouchEyeHeight;
        m_charSettings.crouchSpeedMult  = m_editorState.charCrouchSpeedMult;
        m_charSettings.crouchTransSpeed = m_editorState.charCrouchTransSpeed;
        m_charSettings.cameraTiltEnabled = m_editorState.charCameraTiltEnabled;
        m_charSettings.cameraTiltAmount  = m_editorState.charCameraTiltAmount;
        m_charSettings.cameraTiltSpeed   = m_editorState.charCameraTiltSpeed;
        m_charSettings.headBobEnabled = m_editorState.charHeadBobEnabled;
        m_charSettings.headBobSpeed   = m_editorState.charHeadBobSpeed;
        m_charSettings.headBobAmount  = m_editorState.charHeadBobAmount;
        m_charSettings.headBobSway    = m_editorState.charHeadBobSway;
        m_charSettings.leanEnabled    = m_editorState.charLeanEnabled;
        m_charSettings.leanAngle      = m_editorState.charLeanAngle;
        m_charSettings.leanOffset     = m_editorState.charLeanOffset;
        m_charSettings.leanSpeed      = m_editorState.charLeanSpeed;
        memcpy(m_charSettings.headColor,  m_editorState.charHeadColor,  sizeof(float) * 4);
        memcpy(m_charSettings.torsoColor, m_editorState.charTorsoColor, sizeof(float) * 4);
        memcpy(m_charSettings.armsColor,  m_editorState.charArmsColor,  sizeof(float) * 4);
        memcpy(m_charSettings.legsColor,  m_editorState.charLegsColor,  sizeof(float) * 4);
        m_charSettings.collisionEnabled = m_editorState.physicsCollisionEnabled;

        m_character.Update(dt, m_input, m_camera, m_charSettings,
                           editorWantsMouse, editorWantsKeyboard,
                           m_editorState.physicsCollisionEnabled ? &m_physicsWorld : nullptr,
                           &m_editorState.scene);

        // ---- Networking: sample input, send to server, smooth correction ----
        if (m_networkManager.GetRole() == NetworkRole::Client) {
            auto& cs = m_networkManager.GetClientState();

            // Read movement keys (same as Character reads)
            // Only sample and send input at the network tick rate (60Hz)
            // to avoid the server applying movement for every frame
            if (cs.TickReady()) {
                float netMoveX = 0.0f, netMoveZ = 0.0f;
                if (!editorWantsKeyboard) {
                    if (m_input.IsKeyDown('W')) netMoveZ += 1.0f;
                    if (m_input.IsKeyDown('S')) netMoveZ -= 1.0f;
                    if (m_input.IsKeyDown('A')) netMoveX -= 1.0f;
                    if (m_input.IsKeyDown('D')) netMoveX += 1.0f;
                }

                InputCommand cmd = cs.SampleInput(
                    netMoveX, netMoveZ,
                    XMConvertToDegrees(m_camera.GetYaw()),
                    XMConvertToDegrees(m_camera.GetPitch()),
                    !editorWantsKeyboard && m_input.IsKeyPressed(VK_SPACE),
                    m_input.IsKeyDown('C'),
                    m_input.IsKeyDown(VK_SHIFT),
                    m_input.IsLeftMouseDown(),
                    m_input.IsKeyPressed('R'),
                    m_input.IsKeyPressed('E'),
                    static_cast<uint8_t>(m_weaponSystem.GetCurrentWeapon())
                );

                m_networkManager.SendInput(cmd);
            }

            // Continuous server correction — blend toward authoritative position every frame
            XMFLOAT3 corrected = cs.SmoothCorrect(m_character.GetPosition(), dt);
            m_character.SetPosition(corrected);
        }

        // ---- AI Sound: Footsteps ----
        // Emit footstep sounds when player is walking/sprinting
        if (m_character.IsMoving() && m_character.IsGrounded()) {
            static float footstepTimer = 0.0f;
            float stepInterval = m_character.IsSprinting() ? 0.25f : 0.4f;
            footstepTimer += dt;
            if (footstepTimer >= stepInterval) {
                footstepTimer -= stepInterval;
                float radius = m_character.IsSprinting() ? 10.0f : 5.0f;
                m_aiSystem.PostFootstep(m_character.GetPosition(), radius);
            }
        }

        // ---- Weapon System Update (only in character mode) ----
        m_weaponSystem.Update(dt, m_input, m_camera, m_character, editorWantsMouse,
                               m_editorState.physicsCollisionEnabled ? &m_physicsWorld : nullptr,
                               &m_aiSystem);

        // Apply weapon recoil to camera
        float recoilPitch = m_weaponSystem.GetRecoilPitch();
        if (recoilPitch > 0.01f) {
            // Nudge camera pitch up by recoil amount (scaled down for smooth feel)
            m_camera.Update(m_weaponSystem.GetRecoilYaw() * dt * 2.0f,
                            -recoilPitch * dt * 2.0f);
        }

        // ---- Destruction: process bullet hits against entities ----
        if (m_weaponSystem.JustFired()) {
            const auto& hit = m_weaponSystem.GetLastHit();

            // ---- AI Sound: Gunshot ----
            XMFLOAT3 firePos = m_characterMode ? m_character.GetPosition() : m_camera.GetPosition();
            m_aiSystem.PostGunshot(firePos);

            // ---- GPU Particle Muzzle FX ----
            {
                XMFLOAT3 muzzlePos = m_weaponSystem.GetMuzzlePosition();
                XMFLOAT3 fireDir   = m_weaponSystem.GetFireDirection();
                m_gpuParticles.SpawnBurst(muzzlePos, fireDir, ParticlePresets::MuzzleFlashCore(), 4);
                m_gpuParticles.SpawnBurst(muzzlePos, fireDir, ParticlePresets::MuzzleFlashSparks(), 8);
                m_gpuParticles.SpawnBurst(muzzlePos, fireDir, ParticlePresets::MuzzleSmoke(), 3);
                m_gpuParticles.SpawnBurst(muzzlePos, fireDir, ParticlePresets::BulletTracer(), 3);
            }

            // ---- AI Sound: Bullet impact ----
            if (hit.hit) {
                m_aiSystem.PostBulletImpact(hit.hitPosition);

                // If we hit an entity, use material-aware impact FX
                if (hit.entityIndex >= 0 && hit.entityIndex < m_editorState.scene.GetEntityCount()) {
                    auto& entity = m_editorState.scene.GetEntity(hit.entityIndex);

                    // Material-aware impact FX (sparks + dust tuned per material)
                    {
                        ParticleEmitterDef sparks, dust; int sCount, dCount;
                        ParticlePresets::GetMaterialImpactDef(entity.materialType, entity.color, sparks, sCount, dust, dCount);
                        if (sCount > 0) m_gpuParticles.SpawnBurst(hit.hitPosition, sparks, sCount);
                        if (dCount > 0) m_gpuParticles.SpawnBurst(hit.hitPosition, dust, dCount);
                        m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::ImpactFlash(), 2);
                    }

                    // Add hit decal (bullet scar) at world-space hit position
                    entity.AddHitDecal(hit.hitPosition.x, hit.hitPosition.y, hit.hitPosition.z);

                    if (entity.destructible) {
                        // Auto-enable voxel destruction on cubes that don't have it yet
                        if (!entity.voxelDestruction && entity.meshType == MeshType::Cube) {
                            entity.voxelDestruction = true;
                            // Pick resolution: thin/fragile materials get smaller grids
                            float minScale = (std::min)({entity.scale[0], entity.scale[1], entity.scale[2]});
                            float avgScale = (entity.scale[0] + entity.scale[1] + entity.scale[2]) / 3.0f;

                            if (entity.materialType == MaterialType::Glass ||
                                entity.materialType == MaterialType::Wood ||
                                minScale < 0.5f) {
                                entity.voxelRes = 5;
                            } else if (avgScale >= 4.0f) {
                                entity.voxelRes = 8;
                            } else if (avgScale >= 2.0f) {
                                entity.voxelRes = 6;
                            } else {
                                entity.voxelRes = 5;
                            }
                            entity.ResetVoxelMask();
                            m_editorState.physicsRebuildRequested = true;
                        }

                        // Voxel chunk destruction: remove the hit cell only
                        if (entity.voxelDestruction && entity.meshType == MeshType::Cube) {
                            // Prefer direct cell index from physics (works from any direction)
                            bool removed = false;
                            if (hit.voxelCellIndex >= 0) {
                                removed = entity.RemoveVoxelCell(hit.voxelCellIndex);
                            } else {
                                // Fallback for first hit before per-cell colliders exist
                                removed = entity.RemoveVoxelAt(hit.hitPosition.x, hit.hitPosition.y, hit.hitPosition.z);
                            }
                            if (removed) {
                                // Spawn small debris for the removed chunk
                                XMFLOAT3 chunkScale = {
                                    entity.scale[0] / entity.voxelRes,
                                    entity.scale[1] / entity.voxelRes,
                                    entity.scale[2] / entity.voxelRes
                                };
                                {
                                    ParticleEmitterDef sparks, dust; int sCount, dCount;
                                    ParticlePresets::GetMaterialImpactDef(entity.materialType, entity.color, sparks, sCount, dust, dCount);
                                    if (sCount > 0) m_gpuParticles.SpawnBurst(hit.hitPosition, sparks, sCount);
                                    if (dCount > 0) m_gpuParticles.SpawnBurst(hit.hitPosition, dust, dCount);
                                }
                                // Wall fragments: big chunks + small chips + dust cloud
                                m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::WallChunks(entity.color), 6);
                                m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::WallChips(entity.color), 12);
                                m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::WallDust(entity.color), 5);

                                // Spawn 3D mesh debris cubes that tumble with physics
                                for (int dc = 0; dc < 3; dc++) {
                                    float rx = ((rand() % 200) - 100) / 100.0f * 2.0f;
                                    float ry = ((rand() % 200) / 100.0f) * 2.0f + 1.0f;
                                    float rz = ((rand() % 200) - 100) / 100.0f * 2.0f;
                                    XMFLOAT3 vel = { rx, ry, rz };
                                    float s = chunkScale.x * (0.2f + (rand() % 40) / 100.0f);
                                    s = fminf(s, 0.2f); // Hard cap
                                    XMFLOAT3 debrisScale = { s, s, s };
                                    m_debrisManager.SpawnSingle(hit.hitPosition, vel, debrisScale, entity.color, entity.materialType);
                                }

                                // Mark colliders dirty so player can walk/shoot through the hole
                                m_editorState.physicsRebuildRequested = true;

                                // Broadcast voxel destruction event for remote clients (VFX)
                                if (m_networkManager.IsServer()) {
                                    NetEntityId netId = m_networkManager.GetReplication().GetNetId(hit.entityIndex);
                                    if (netId != InvalidNetEntity) {
                                        ServerEvent evt;
                                        evt.eventType = ServerEventType::VoxelDestroyed;
                                        evt.targetNetId = netId;
                                        evt.position = hit.hitPosition;
                                        evt.extra = static_cast<int16_t>(hit.voxelCellIndex);
                                        evt.value = static_cast<float>(entity.materialType);
                                        m_networkManager.QueueServerEvent(evt);
                                    }
                                }

                                // Evaluate fracture — detach disconnected voxel clusters
                                int fractureCount = m_fractureSystem.EvaluateFracture(
                                    hit.entityIndex, m_editorState.scene, m_gpuParticles);
                                if (fractureCount > 0) {
                                    m_editorState.physicsRebuildRequested = true;
                                }
                            }

                            // Destroy entity only when ALL voxel cells are gone
                            if (entity.GetActiveVoxelCount() == 0) {
                                entity.health = 0.0f;
                            }
                        } else {
                            // Non-voxel entities: normal HP damage
                            float damage = m_weaponSystem.GetCurrentDef().damage
                                         * m_weaponSystem.GetCurrentDef().pelletsPerShot;
                            entity.TakeDamage(damage);
                        }

                        bool destroyed = entity.IsDestroyed();

                        // Spawn smoke if entity is below 50% health (still alive, non-voxel only)
                        if (!destroyed && !entity.voxelDestruction &&
                            entity.GetHealthFraction() < 0.5f && entity.smokeOnDamage) {
                            XMFLOAT3 smokeCenter = { entity.position[0], entity.position[1], entity.position[2] };
                            m_gpuParticles.SpawnBurst(smokeCenter, ParticlePresets::DamageSmoke(), 2);
                        }

                        // Fire embers on critical damage (below 25%, non-voxel only)
                        if (!destroyed && !entity.voxelDestruction && entity.GetHealthFraction() < 0.25f) {
                            XMFLOAT3 fireCenter = { entity.position[0], entity.position[1], entity.position[2] };
                            m_gpuParticles.SpawnBurst(fireCenter, ParticlePresets::DamageEmbers(), 3);
                        }

                        if (destroyed) {
                            // IMPORTANT: Copy entity data BEFORE modifying the scene,
                            // because AddEntity/RemoveEntity can reallocate the vector
                            // and invalidate the 'entity' reference.
                            Entity destroyedCopy = entity;

                            // Broadcast destruction event to clients
                            if (m_networkManager.IsServer()) {
                                NetEntityId netId = m_networkManager.GetReplication().GetNetId(hit.entityIndex);
                                if (netId != InvalidNetEntity) {
                                    ServerEvent evt;
                                    evt.eventType = ServerEventType::EntityDestroyed;
                                    evt.targetNetId = netId;
                                    evt.position = { destroyedCopy.position[0], destroyedCopy.position[1], destroyedCopy.position[2] };
                                    evt.value = static_cast<float>(destroyedCopy.materialType);
                                    m_networkManager.QueueServerEvent(evt);
                                }
                            }

                            // Full material-aware explosion
                            XMFLOAT3 center = { destroyedCopy.position[0], destroyedCopy.position[1], destroyedCopy.position[2] };
                            XMFLOAT3 entScale = { destroyedCopy.scale[0], destroyedCopy.scale[1], destroyedCopy.scale[2] };
                            {
                                ParticleEmitterDef debris, sparks, smoke; int dCount, sCount, smCount;
                                ParticlePresets::GetMaterialExplosionDef(destroyedCopy.materialType, destroyedCopy.color,
                                                                        debris, dCount, sparks, sCount, smoke, smCount);
                                if (dCount > 0) m_gpuParticles.SpawnBurst(center, debris, dCount);
                                if (sCount > 0) m_gpuParticles.SpawnBurst(center, sparks, sCount);
                                if (smCount > 0) m_gpuParticles.SpawnBurst(center, smoke, smCount);
                                m_gpuParticles.SpawnBurst(center, ParticlePresets::ExplosionFlash(), 8);
                            }

                            // Spawn physical debris fragments via DebrisManager
                            m_debrisManager.SpawnFromDestruction(center, entScale, destroyedCopy.color,
                                                                 destroyedCopy.debrisCount, destroyedCopy.debrisScale,
                                                                 destroyedCopy.materialType);

                            // Screen shake proportional to entity size (reduced 90%)
                            float avgScale = (destroyedCopy.scale[0] + destroyedCopy.scale[1] + destroyedCopy.scale[2]) / 3.0f;

                            // Wake nearby sleeping debris/fragments from the explosion
                            m_fractureSystem.WakeNear(center, avgScale * 2.0f);
                            m_debrisManager.WakeNear(center, avgScale * 2.0f);
                            m_camera.AddScreenShake(0.008f * avgScale, 0.15f);

                            // --- Structural support: auto-collapse entities resting on top ---
                            // Destroyed entity bounding box top
                            float dTop    = destroyedCopy.position[1] + destroyedCopy.scale[1] * 0.5f;
                            float dMinX   = destroyedCopy.position[0] - destroyedCopy.scale[0] * 0.5f;
                            float dMaxX   = destroyedCopy.position[0] + destroyedCopy.scale[0] * 0.5f;
                            float dMinZ   = destroyedCopy.position[2] - destroyedCopy.scale[2] * 0.5f;
                            float dMaxZ   = destroyedCopy.position[2] + destroyedCopy.scale[2] * 0.5f;

                            // Broadcast entity removal to clients (get netId BEFORE removal)
                            NetEntityId removeNetId = InvalidNetEntity;
                            if (m_networkManager.IsServer()) {
                                removeNetId = m_networkManager.GetReplication().GetNetId(hit.entityIndex);
                                if (removeNetId != InvalidNetEntity) {
                                    ServerEvent revt;
                                    revt.eventType = ServerEventType::EntityRemoved;
                                    revt.targetNetId = removeNetId;
                                    revt.position = center;
                                    m_networkManager.QueueServerEvent(revt);
                                }
                            }

                            // Remove the destroyed entity
                            m_editorState.scene.RemoveEntity(hit.entityIndex);

                            if (removeNetId != InvalidNetEntity)
                                m_networkManager.GetReplication().UnregisterEntity(removeNetId);

                            // Deselect if it was selected
                            if (m_editorState.selectedEntity == hit.entityIndex)
                                m_editorState.selectedEntity = -1;
                            else if (m_editorState.selectedEntity > hit.entityIndex)
                                m_editorState.selectedEntity--;

                            // Auto structural support: any entity whose bottom rests near
                            // the top of the destroyed entity (or overlaps vertically and horizontally)
                            // will collapse. This handles roofs on walls, stacked objects, etc.
                            for (int si = m_editorState.scene.GetEntityCount() - 1; si >= 0; si--) {
                                auto& supported = m_editorState.scene.GetEntity(si);

                                // Non-destructible entities cannot be collapsed
                                if (!supported.destructible) continue;

                                // Check explicit name-based support OR automatic proximity
                                bool shouldCollapse = false;

                                // Name-based: supportedBy field matches destroyed entity name
                                if (!supported.supportedBy.empty() && supported.supportedBy == destroyedCopy.name) {
                                    shouldCollapse = true;
                                }

                                // Proximity-based: entity bottom is near destroyed entity top,
                                // and they overlap horizontally (cube entities only —
                                // custom meshes have complex shapes, skip auto-collapse)
                                if (!shouldCollapse &&
                                    destroyedCopy.meshType == MeshType::Cube &&
                                    supported.meshType == MeshType::Cube) {
                                    float sBottom = supported.position[1] - supported.scale[1] * 0.5f;
                                    float sMinX   = supported.position[0] - supported.scale[0] * 0.5f;
                                    float sMaxX   = supported.position[0] + supported.scale[0] * 0.5f;
                                    float sMinZ   = supported.position[2] - supported.scale[2] * 0.5f;
                                    float sMaxZ   = supported.position[2] + supported.scale[2] * 0.5f;

                                    // Bottom of supported entity is within 1.5 units of top of destroyed
                                    float tolerance = 1.5f;
                                    bool restingOnTop = (sBottom >= dTop - tolerance) && (sBottom <= dTop + tolerance);
                                    // Horizontal overlap check (XZ bounding boxes intersect)
                                    bool overlapX = (sMinX < dMaxX) && (sMaxX > dMinX);
                                    bool overlapZ = (sMinZ < dMaxZ) && (sMaxZ > dMinZ);

                                    if (restingOnTop && overlapX && overlapZ) {
                                        shouldCollapse = true;
                                    }
                                }

                                if (shouldCollapse) {
                                    // Cascade: explode the supported entity
                                    XMFLOAT3 sc = { supported.position[0], supported.position[1], supported.position[2] };
                                    XMFLOAT3 ss = { supported.scale[0], supported.scale[1], supported.scale[2] };
                                    {
                                        ParticleEmitterDef debris, sparks, smoke; int dCount, sCount, smCount;
                                        ParticlePresets::GetMaterialExplosionDef(supported.materialType, supported.color,
                                                                                debris, dCount, sparks, sCount, smoke, smCount);
                                        if (dCount > 0) m_gpuParticles.SpawnBurst(sc, debris, dCount);
                                        if (sCount > 0) m_gpuParticles.SpawnBurst(sc, sparks, sCount);
                                        if (smCount > 0) m_gpuParticles.SpawnBurst(sc, smoke, smCount);
                                        m_gpuParticles.SpawnBurst(sc, ParticlePresets::ExplosionFlash(), 8);
                                    }
                                    float suppAvg = (supported.scale[0] + supported.scale[1] + supported.scale[2]) / 3.0f;
                                    m_camera.AddScreenShake(0.006f * suppAvg, 0.12f);

                                    // Broadcast cascade collapse to clients
                                    NetEntityId sNetId = InvalidNetEntity;
                                    if (m_networkManager.IsServer()) {
                                        sNetId = m_networkManager.GetReplication().GetNetId(si);
                                        if (sNetId != InvalidNetEntity) {
                                            ServerEvent devt;
                                            devt.eventType = ServerEventType::EntityDestroyed;
                                            devt.targetNetId = sNetId;
                                            devt.position = sc;
                                            devt.value = static_cast<float>(supported.materialType);
                                            m_networkManager.QueueServerEvent(devt);

                                            ServerEvent revt;
                                            revt.eventType = ServerEventType::EntityRemoved;
                                            revt.targetNetId = sNetId;
                                            revt.position = sc;
                                            m_networkManager.QueueServerEvent(revt);
                                        }
                                    }

                                    m_editorState.scene.RemoveEntity(si);

                                    if (sNetId != InvalidNetEntity)
                                        m_networkManager.GetReplication().UnregisterEntity(sNetId);

                                    if (m_editorState.selectedEntity == si)
                                        m_editorState.selectedEntity = -1;
                                    else if (m_editorState.selectedEntity > si)
                                        m_editorState.selectedEntity--;

                                    LOG_INFO("Supported entity collapsed!");
                                }
                            }

                            // Mark physics colliders dirty
                            m_editorState.physicsRebuildRequested = true;

                            LOG_INFO("Entity destroyed!");
                        }
                    }
                } else {
                    // Hit world geometry (no entity) — default concrete-like impact
                    m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::ImpactSparks(), 8);
                    m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::ImpactFlash(), 2);
                    float grey[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
                    m_gpuParticles.SpawnBurst(hit.hitPosition, ParticlePresets::ImpactDust(grey), 5);
                }
            }
        }

        // Update AI agent damage flash timers
        for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
            auto& agent = m_aiSystem.GetAgent(i);
            if (agent.damageFlashTimer > 0.0f) {
                agent.damageFlashTimer -= dt;
            }
        }
    } else {
        // ---- Fly Camera ----
        if (!editorWantsKeyboard) {
            float speed = m_editorState.cameraMoveSpeed * dt;
            if (m_input.IsKeyDown(VK_SHIFT)) speed *= m_editorState.cameraSprintMult;

            XMFLOAT3 forward = m_camera.GetForward();
            XMFLOAT3 right   = m_camera.GetRight();
            XMFLOAT3 pos     = m_camera.GetPosition();

            if (m_input.IsKeyDown('W')) {
                pos.x += forward.x * speed;
                pos.y += forward.y * speed;
                pos.z += forward.z * speed;
            }
            if (m_input.IsKeyDown('S')) {
                pos.x -= forward.x * speed;
                pos.y -= forward.y * speed;
                pos.z -= forward.z * speed;
            }
            if (m_input.IsKeyDown('A')) {
                pos.x -= right.x * speed;
                pos.z -= right.z * speed;
            }
            if (m_input.IsKeyDown('D')) {
                pos.x += right.x * speed;
                pos.z += right.z * speed;
            }
            if (m_input.IsKeyDown(VK_SPACE)) {
                pos.y += speed;
            }
            if (m_input.IsKeyDown(VK_CONTROL)) {
                pos.y -= speed;
            }

            m_camera.SetPosition(pos);
        } // end !editorWantsKeyboard

        // Reset camera roll in fly cam mode
        m_camera.SetRoll(0.0f);
    }

    // ---- GPU Particle System Update ----
    {
        auto* ctx = m_renderer.GetContext();
        m_gpuParticles.Update(ctx, dt, m_renderer.GetDepthSRV(), m_cbPerFrame.GetBuffer());
    }

    // ---- Decal System Update ----
    m_decals.Update(dt);

    // ---- Entity Damage Flash Timers ----
    for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
        auto& e = m_editorState.scene.GetEntity(i);
        if (e.damageFlashTimer > 0.0f) {
            e.damageFlashTimer -= dt;
            if (e.damageFlashTimer < 0.0f) e.damageFlashTimer = 0.0f;
        }
    }

    // ---- Camera Screen Shake ----
    m_camera.UpdateShake(dt);

    // ---- AI Navigation & Agents ----
    // Sync nav grid settings from editor
    m_navGrid.showDebug = m_editorState.navShowDebug;
    m_aiSystem.showDebug = m_editorState.aiShowDebug;

    // Handle nav grid rebuild request
    if (m_editorState.navRebuildRequested) {
        m_editorState.navRebuildRequested = false;
        // Re-init grid if size changed
        if (m_navGrid.GetWidth() != m_editorState.navGridWidth ||
            m_navGrid.GetHeight() != m_editorState.navGridHeight ||
            m_navGrid.GetCellSize() != m_editorState.navCellSize) {
            m_navGrid.Init(m_editorState.navGridWidth, m_editorState.navGridHeight,
                           m_editorState.navCellSize,
                           m_editorState.navOriginX, m_editorState.navOriginZ,
                           m_editorState.navGridY);
        }
        m_navGrid.SetOrigin(m_editorState.navOriginX, m_editorState.navOriginZ);
        m_navGrid.SetGridY(m_editorState.navGridY);
        m_navGrid.RebuildFromEntities(m_editorState.scene);
        LOG_INFO("NavGrid rebuilt from %d entities", m_editorState.scene.GetEntityCount());

        // Also rebuild physics colliders when entities change
        m_physicsWorld.RebuildStaticColliders(m_editorState.scene);
    }

    // Handle ground mesh rebuild (after warfield generation resizes map)
    if (m_editorState.groundRebuildRequested) {
        m_editorState.groundRebuildRequested = false;
        m_groundMesh.Release();
        // Rebuild with new half-extent
        using V = VertexPosNormalColor;
        float s = m_editorState.groundHalfExtent;
        XMFLOAT4 groundCol = { 0.35f, 0.55f, 0.28f, 1.0f };
        std::vector<V> groundVerts = {
            {{ -s, -0.05f,  s }, { 0, 1, 0 }, groundCol, {0,0} },
            {{  s, -0.05f,  s }, { 0, 1, 0 }, groundCol, {1,0} },
            {{  s, -0.05f, -s }, { 0, 1, 0 }, groundCol, {1,1} },
            {{ -s, -0.05f, -s }, { 0, 1, 0 }, groundCol, {0,1} },
        };
        std::vector<UINT> groundIdx = { 0, 1, 2, 0, 2, 3 };
        m_groundMesh.Create(m_renderer.GetDevice(), groundVerts, groundIdx);

        // Also extend shadow distance and fog for massive maps
        if (s > 100.0f) {
            m_editorState.shadowDistance = s * 0.5f;
            m_editorState.shadowDirty = true;
            m_editorState.fogStart = s * 0.3f;
            m_editorState.fogEnd = s * 0.9f;
            m_editorState.cameraFarZ = s * 2.5f;
            m_editorState.cameraDirty = true;
            m_editorState.lightingDirty = true;
        }
        LOG_INFO("Ground mesh rebuilt: half-extent=%.0f", s);
    }

    // Handle agent spawn request (selectedAgent == -2 means "spawn new")
    if (m_editorState.aiSelectedAgent == -2) {
        XMFLOAT3 spawnPos = { m_editorState.aiSpawnPos[0],
                              m_editorState.aiSpawnPos[1],
                              m_editorState.aiSpawnPos[2] };
        bool isDrone = (m_editorState.aiDefaultType == 1);
        int idx = m_aiSystem.AddAgent(isDrone ? "Drone" : "", spawnPos);
        AIAgent& agent = m_aiSystem.GetAgent(idx);
        agent.type = isDrone ? AIAgentType::Drone : AIAgentType::Ground;
        agent.settings.moveSpeed   = m_editorState.aiDefaultSpeed;
        agent.settings.chaseSpeed  = m_editorState.aiDefaultChaseSpeed;
        agent.settings.detectRange = m_editorState.aiDefaultDetectRange;
        agent.settings.loseRange   = m_editorState.aiDefaultLoseRange;
        memcpy(agent.settings.bodyColor, m_editorState.aiDefaultColor, sizeof(float) * 4);
        if (isDrone) {
            // Drone-specific defaults
            agent.settings.fovAngle = 360.0f;   // 360° vision
            agent.settings.bodyScale = 0.6f;     // Smaller body
            agent.settings.seekCoverOnDamage = false;  // Drones don't take cover
            agent.position.y = spawnPos.y + agent.settings.droneHoverHeight;
            agent.homePosition.y = spawnPos.y;
            agent.droneTargetAlt = agent.settings.droneHoverHeight;
            // Cyan color for drones by default
            agent.settings.bodyColor[0] = 0.2f;
            agent.settings.bodyColor[1] = 0.7f;
            agent.settings.bodyColor[2] = 0.9f;
            agent.InitRotors();  // Initialize rotor health from settings
        }
        m_editorState.aiSelectedAgent = idx;
    }

    // Update AI agents — pass player position for chase detection
    XMFLOAT3 playerPos = m_camera.GetPosition();
    if (m_characterMode) {
        playerPos = m_character.GetPosition();
    }
    m_aiSystem.Update(dt, m_navGrid, playerPos,
                       m_editorState.physicsCollisionEnabled ? &m_physicsWorld : nullptr);

    // ---- Drone crash / rotor failure logic ----
    for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
        auto& agent = m_aiSystem.GetAgent(i);
        if (!agent.alive || agent.droneExploded) continue;
        if (agent.type != AIAgentType::Drone) continue;

        // Gradually slow damaged rotors (visual: spin speed decays toward health ratio)
        for (int r = 0; r < 4; r++) {
            if (agent.rotorAlive[r]) {
                float healthPct = agent.rotorHealth[r] / agent.settings.droneRotorHealth;
                float targetSpeed = 0.3f + 0.7f * healthPct; // Damaged rotors spin slower
                agent.rotorSpinSpeed[r] += (targetSpeed - agent.rotorSpinSpeed[r]) * 3.0f * dt;
            }
        }

        // Check for crash condition: 2 rotors dead on same side
        if (!agent.droneCrashing && agent.ShouldCrash()) {
            agent.droneCrashing = true;
            agent.droneCrashTimer = 0.0f;
            agent.droneVerticalVel = 0.0f;
            LOG_INFO("Drone '%s' lost critical rotors — crashing!", agent.name.c_str());
        }

        // Update crashing drone
        if (agent.droneCrashing) {
            agent.droneCrashTimer += dt;

            // Gravity pull — accelerate downward
            agent.droneVerticalVel -= 15.0f * dt;
            agent.position.y += agent.droneVerticalVel * dt;

            // Spin out — rapidly increase roll and pitch
            agent.droneRoll  += 180.0f * dt;  // 180 deg/s spin
            agent.dronePitch += 60.0f * dt;

            // Slight horizontal drift
            float yawRad = XMConvertToRadians(agent.yaw);
            agent.position.x += std::sin(yawRad) * 1.5f * dt;
            agent.position.z += std::cos(yawRad) * 1.5f * dt;

            // Trail smoke while falling
            if (agent.droneCrashTimer > 0.1f) {
                agent.droneDownwashTimer -= dt;
                if (agent.droneDownwashTimer <= 0.0f) {
                    agent.droneDownwashTimer = 0.05f;
                    // Smoke trail particle — small gray puffs
                    m_gpuParticles.SpawnBurst(agent.position, ParticlePresets::DroneCrashSmoke(), 1);
                }
            }

            // Check if hit the ground
            float groundY = m_editorState.charGroundY;
            if (agent.position.y <= groundY + 0.2f) {
                agent.position.y = groundY + 0.1f;
                agent.droneExploded = true;
                agent.alive = false;
                agent.active = false;

                // Explosion — multi-phase drone crash FX
                m_gpuParticles.SpawnBurst(agent.position, ParticlePresets::DroneCrashExplosion(), 35);
                m_gpuParticles.SpawnBurst(agent.position, ParticlePresets::DroneCrashDebris(), 16);
                m_gpuParticles.SpawnBurst(agent.position, ParticlePresets::ExplosionSmoke(), 12);
                m_gpuParticles.SpawnBurst(agent.position, ParticlePresets::ExplosionFlash(), 8);
                LOG_INFO("Drone '%s' crashed and exploded!", agent.name.c_str());
            }
        }
    }

    // ---- Drone downwash particle effects ----
    for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
        auto& agent = m_aiSystem.GetAgent(i);
        if (!agent.active || !agent.alive) continue;
        if (agent.type != AIAgentType::Drone) continue;
        if (agent.droneCrashing) continue; // No downwash while crashing

        // Emit downwash on timer
        agent.droneDownwashTimer -= dt;
        if (agent.droneDownwashTimer <= 0.0f) {
            agent.droneDownwashTimer = agent.settings.droneDownwashRate;
            XMFLOAT3 downwashGround = { agent.position.x, m_editorState.charGroundY + 0.1f, agent.position.z };
            m_gpuParticles.SpawnBurst(downwashGround, ParticlePresets::DroneGroundDust(), 6);
            m_gpuParticles.SpawnBurst(agent.position, ParticlePresets::DroneDownwash(), 12);
        }
    }

    // Apply editor state changes from panels
    if (m_editorState.rendererDirty) {
        m_editorState.rendererDirty = false;
        m_renderer.SetWireframe(m_editorState.wireframe);
        m_renderer.SetVSync(m_editorState.vsync);
        m_renderer.SetMSAA(m_editorState.msaaSamples);
    }

    // FSR upscaler state changes
    if (m_editorState.fsrDirty) {
        m_editorState.fsrDirty = false;
        if (m_editorState.fsrEnabled) {
            int rw, rh;
            m_fsrUpscaler.GetRenderResolution(m_width, m_height,
                static_cast<FSRQuality>(m_editorState.fsrQuality), rw, rh);
            m_fsrUpscaler.UpdateRenderTarget(m_renderer.GetDevice(), rw, rh);
        }
    }

    if (m_editorState.cameraDirty) {
        m_editorState.cameraDirty = false;
        m_camera.Init(m_editorState.cameraFOV, m_renderer.GetAspectRatio(),
                      m_editorState.cameraNearZ, m_editorState.cameraFarZ);
    }

    m_debugRenderer.SetEnabled(m_editorState.showDebug);

    // Sync weapon system settings from editor
    m_weaponSystem.showDebug = m_editorState.weaponShowDebug;
    // Bidirectional sync: editor combo ↔ actual weapon (1/2/3 keys)
    int actualWeapon = static_cast<int>(m_weaponSystem.GetCurrentWeapon());
    if (m_editorState.weaponType != actualWeapon) {
        // Editor combo was changed by user — switch weapon to match
        if (m_editorState.weaponType != m_lastEditorWeaponType) {
            m_weaponSystem.SwitchWeapon(static_cast<WeaponType>(m_editorState.weaponType));
        } else {
            // Weapon changed via 1/2/3 keys — update editor to match
            m_editorState.weaponType = actualWeapon;
        }
    }
    m_lastEditorWeaponType = m_editorState.weaponType;
    // Sync HUD visibility
    m_hud.GetSettings().showCrosshair = m_editorState.weaponShowHUD;
    m_hud.GetSettings().showAmmo      = m_editorState.weaponShowHUD;
    m_hud.GetSettings().showHitMarker = m_editorState.weaponShowHUD;
    m_hud.GetSettings().showReloadBar = m_editorState.weaponShowHUD;

    // Sync post-processing settings from editor state
    m_postProcessSettings.bloomEnabled       = m_editorState.ppBloomEnabled;
    m_postProcessSettings.bloomThreshold     = m_editorState.ppBloomThreshold;
    m_postProcessSettings.bloomIntensity     = m_editorState.ppBloomIntensity;
    m_postProcessSettings.vignetteEnabled    = m_editorState.ppVignetteEnabled;
    m_postProcessSettings.vignetteIntensity  = m_editorState.ppVignetteIntensity;
    m_postProcessSettings.vignetteSmoothness = m_editorState.ppVignetteSmoothness;
    m_postProcessSettings.brightness         = m_editorState.ppBrightness;
    m_postProcessSettings.contrast           = m_editorState.ppContrast;
    m_postProcessSettings.saturation         = m_editorState.ppSaturation;
    m_postProcessSettings.gamma              = m_editorState.ppGamma;
    m_postProcessSettings.tint[0]            = m_editorState.ppTint[0];
    m_postProcessSettings.tint[1]            = m_editorState.ppTint[1];
    m_postProcessSettings.tint[2]            = m_editorState.ppTint[2];
    m_postProcessSettings.ssaoEnabled        = m_editorState.ssaoEnabled;

    // Sync art style settings
    m_postProcessSettings.outlineEnabled       = m_editorState.outlineEnabled;
    m_postProcessSettings.outlineThickness     = m_editorState.outlineThickness;
    m_postProcessSettings.outlineColor[0]      = m_editorState.outlineColor[0];
    m_postProcessSettings.outlineColor[1]      = m_editorState.outlineColor[1];
    m_postProcessSettings.outlineColor[2]      = m_editorState.outlineColor[2];
    m_postProcessSettings.paperGrainIntensity  = m_editorState.paperGrainIntensity;
    m_postProcessSettings.hatchingIntensity    = m_editorState.hatchingIntensity;
    m_postProcessSettings.hatchingScale        = m_editorState.hatchingScale;

    // Sync SSAO settings
    m_ssaoSettings.enabled    = m_editorState.ssaoEnabled;
    m_ssaoSettings.radius     = m_editorState.ssaoRadius;
    m_ssaoSettings.bias       = m_editorState.ssaoBias;
    m_ssaoSettings.intensity  = m_editorState.ssaoIntensity;
    m_ssaoSettings.kernelSize = m_editorState.ssaoKernelSize;

    // ---- Day/Night + Weather System ----
    {
        // Sync editor state -> TimeWeatherSettings
        m_timeWeatherSettings.dayNightEnabled = m_editorState.dayNightEnabled;
        m_timeWeatherSettings.timeOfDay       = m_editorState.timeOfDay;
        m_timeWeatherSettings.daySpeed        = m_editorState.daySpeed;
        m_timeWeatherSettings.paused          = m_editorState.dayNightPaused;
        m_timeWeatherSettings.sunAzimuth      = m_editorState.sunAzimuth;
        m_timeWeatherSettings.targetWeather   = static_cast<WeatherType>(m_editorState.weatherType);
        m_timeWeatherSettings.windDirection   = m_editorState.windDirection;
        m_timeWeatherSettings.windSpeed       = m_editorState.windSpeed;

        m_timeWeather.Update(dt, m_timeWeatherSettings);

        // Sync time back to editor (it advances in the system)
        m_editorState.timeOfDay = m_timeWeatherSettings.timeOfDay;

        // When day/night is enabled, override editor lighting/sky from computed values
        if (m_editorState.dayNightEnabled) {
            m_timeWeather.ApplyToEditorState(
                m_editorState.sunDirection, m_editorState.sunIntensity, m_editorState.sunColor,
                m_editorState.ambientColor, m_editorState.ambientIntensity,
                m_editorState.fogColor, m_editorState.fogDensity,
                m_editorState.skyZenithColor, m_editorState.skyHorizonColor, m_editorState.skyGroundColor,
                m_editorState.skyBrightness,
                m_editorState.cloudCoverage, m_editorState.cloudSpeed, m_editorState.cloudColor
            );
            m_editorState.lightingDirty = true;
            m_editorState.skyDirty = true;
        }
    }

    // ---- Interaction System ----
    if (m_characterMode) {
        m_interactionSystem.Update(dt, m_camera, m_input,
                                   m_editorState.scene, m_physicsWorld, m_character,
                                   &m_inventory);
        // Pass prompt to HUD
        auto& target = m_interactionSystem.GetTarget();
        m_hud.SetInteractPrompt(target.found ? target.prompt : "");
        m_hud.SetInteractTarget(target.found ? target.entityIndex : -1);
    } else {
        m_hud.SetInteractPrompt("");
        m_hud.SetInteractTarget(-1);
    }

    // ---- Pickup Collection ----
    if (m_characterMode) {
        XMFLOAT3 pPos = m_character.GetPosition();
        for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
            auto& e = m_editorState.scene.GetEntity(i);
            if (e.pickupType == PickupType::None || e.pickupCollected) continue;

            // Distance check
            float dx = e.position[0] - pPos.x;
            float dy = e.position[1] - pPos.y;
            float dz = e.position[2] - pPos.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < e.pickupRadius * e.pickupRadius) {
                // Collect!
                if (e.pickupType == PickupType::Health) {
                    m_character.Heal(e.pickupAmount);
                } else if (e.pickupType == PickupType::Ammo) {
                    m_weaponSystem.AddReserveAmmo(static_cast<int>(e.pickupAmount));
                }
                e.pickupCollected = true;
                e.pickupRespawnTimer = e.pickupRespawnTime;
                e.visible = false;
            }
        }

        // Update pickup respawn timers
        for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
            auto& e = m_editorState.scene.GetEntity(i);
            if (!e.pickupCollected) continue;
            e.pickupRespawnTimer -= dt;
            if (e.pickupRespawnTimer <= 0.0f) {
                e.pickupCollected = false;
                e.visible = true;
                e.pickupRespawnTimer = 0.0f;
            }
        }
    }

    // ---- Pickup Bobbing & Spinning Animation ----
    {
        float totalTime = m_timer.TotalTime();
        for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
            auto& e = m_editorState.scene.GetEntity(i);
            if (e.pickupType == PickupType::None || e.pickupCollected) continue;
            // Spin
            e.rotation[1] += e.pickupSpinSpeed * dt;
            if (e.rotation[1] > 360.0f) e.rotation[1] -= 360.0f;
            // Bob (sinusoidal offset on Y position)
            e.position[1] = e.scale[1] * 0.5f + 0.5f +
                            sinf(totalTime * e.pickupBobSpeed) * e.pickupBobHeight;
        }
    }

    // Update level editor
    m_levelEditor.Update(dt, m_editorState);

    // Hot-swap: level editor pushed a new scene into the game
    if (m_levelEditor.HasPendingHotSwap()) {
        m_levelEditor.ClearHotSwap();
        m_editorState.physicsRebuildRequested = true;
        LOG_INFO("Hot-swap: level editor scene applied to game");
    }
}

// ==================== Render ====================

void Application::Render() {
    // ---- Level Editor Window (separate swap chain) ----
    if (m_levelEditor.IsOpen()) {
        m_levelEditor.Render(m_renderer.GetContext(), m_editorState);
    }

    auto* ctx = m_renderer.GetContext();

    // ============================================================
    // PASS 1: Shadow Map (depth-only from light's POV)
    // ============================================================
    XMFLOAT3 sunDir = { m_editorState.sunDirection[0], m_editorState.sunDirection[1], m_editorState.sunDirection[2] };
    XMFLOAT3 sceneCenter = { 0.0f, 0.0f, 0.0f };
    float sceneRadius = m_editorState.shadowDistance;

    XMMATRIX lightVP = m_shadowMap.CalcLightViewProjection(sunDir, sceneCenter, sceneRadius);

    if (m_editorState.shadowsEnabled) {
        m_shadowMap.BeginShadowPass(ctx);

        // Bind shadow VS (no pixel shader — depth only)
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_shadowShader.GetVS(), nullptr, 0);
        ctx->PSSetShader(nullptr, nullptr, 0);
        ctx->IASetInputLayout(m_voxelShader.GetInputLayout());

        // Update shadow CB with light VP
        CBShadow shadowData = {};
        XMStoreFloat4x4(&shadowData.LightViewProjection, XMMatrixTranspose(lightVP));
        shadowData.ShadowBias       = m_editorState.shadowBias;
        shadowData.ShadowNormalBias = m_editorState.shadowNormalBias;
        shadowData.ShadowIntensity  = m_editorState.shadowIntensity;
        shadowData.ShadowMapSize    = static_cast<float>(m_shadowMap.GetResolution());
        m_cbShadow.Update(ctx, shadowData);
        m_cbShadow.BindBoth(ctx, 4);

        CBPerObject objData = {};

        // Shadow pass: Ground
        XMMATRIX groundWorld = XMMatrixTranslation(0.0f, -0.01f, 0.0f);
        XMStoreFloat4x4(&objData.World, XMMatrixTranspose(groundWorld));
        XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, groundWorld));
        m_cbPerObject.Update(ctx, objData);
        m_groundMesh.Draw(ctx);

        // Shadow pass: Entities
        // NOTE: Shadow shader only reads gWorld, not gWorldInvTranspose.
        // Skip expensive XMMatrixInverse for shadow pass.
        for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
            const auto& e = m_editorState.scene.GetEntity(i);
            if (!e.visible || !e.castShadow) continue;

            // Frustum + distance culling for shadow pass
            if (m_editorState.cullingEnabled && !m_sceneCuller.IsVisibleShadow(i)) continue;

            // Voxel chunk shadow: draw each active cell
            if (e.voxelDestruction && e.meshType == MeshType::Cube) {
                int res = e.voxelRes;
                for (int vz = 0; vz < res; vz++)
                for (int vy = 0; vy < res; vy++)
                for (int vx = 0; vx < res; vx++) {
                    int idx = vx + vy * res + vz * res * res;
                    if (!e.IsVoxelCellActive(idx)) continue;
                    XMMATRIX cellWorld = e.GetVoxelCellWorldMatrix(vx, vy, vz);
                    XMStoreFloat4x4(&objData.World, XMMatrixTranspose(cellWorld));
                    m_cbPerObject.Update(ctx, objData);
                    m_cbPerObject.BindVS(ctx, 1);
                    m_cubeMesh.Draw(ctx);
                }
            } else {
                XMMATRIX entWorld = e.GetWorldMatrix();
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(entWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindVS(ctx, 1);
                if (e.meshType == MeshType::Cube) {
                    m_cubeMesh.Draw(ctx);
                } else if (e.meshType == MeshType::Custom) {
                    Mesh* customMesh = ResourceManager::Get().GetMesh(e.meshName);
                    if (customMesh) customMesh->Draw(ctx);
                }
            }
        }

        // Shadow pass: Character body parts — disabled (player doesn't cast shadow)
        // if (m_characterMode && m_editorState.charShowBody) { ... }

        // Shadow pass: AI Agents
        for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
            const auto& agent = m_aiSystem.GetAgent(i);
            if (!agent.visible || !agent.active) continue;
            float bs = agent.settings.bodyScale;

            if (agent.type == AIAgentType::Drone) {
                // Drone — sphere shadow
                XMMATRIX droneScale = XMMatrixScaling(bs, bs, bs);
                XMMATRIX droneRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
                XMMATRIX droneTrans = XMMatrixTranslation(agent.position.x,
                                                           agent.position.y,
                                                           agent.position.z);
                XMMATRIX droneWorld = droneScale * droneRot * droneTrans;
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(droneWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindVS(ctx, 1);
                m_sphereMesh.Draw(ctx);

                // Shadow: rotor meshes
                float yawRad = XMConvertToRadians(agent.yaw);
                float armLen = bs * 0.5f * 1.4f;
                float propY  = agent.position.y + bs * 0.5f * 0.3f;
                float rotorScale = bs * 0.4f;
                for (int p = 0; p < 4; p++) {
                    if (!agent.rotorAlive[p]) continue;
                    float angle = yawRad + (p * 1.5708f) + 0.7854f;
                    float px = std::sin(angle) * armLen;
                    float pz = std::cos(angle) * armLen;
                    float discSpin = agent.droneBobPhase * 15.0f * agent.rotorSpinSpeed[p];
                    XMMATRIX rScale = XMMatrixScaling(rotorScale, rotorScale, rotorScale);
                    XMMATRIX rSpin  = XMMatrixRotationY(discSpin);
                    XMMATRIX rTrans = XMMatrixTranslation(
                        agent.position.x + px, propY, agent.position.z + pz);
                    XMMATRIX rotorWorld = rScale * rSpin * rTrans;
                    XMStoreFloat4x4(&objData.World, XMMatrixTranspose(rotorWorld));
                    m_cbPerObject.Update(ctx, objData);
                    m_cbPerObject.BindVS(ctx, 1);
                    m_rotorMesh.Draw(ctx);
                }
            } else {
                // Ground — torso cube + head sphere
                float bodyW = bs * 0.7f;
                float bodyH = bs * 1.0f;
                float bodyD = bs * 0.5f;
                float headR = bs * 0.35f;

                // Torso
                XMMATRIX torsoScale = XMMatrixScaling(bodyW, bodyH, bodyD);
                XMMATRIX torsoRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
                XMMATRIX torsoTrans = XMMatrixTranslation(agent.position.x,
                                                           agent.position.y + bodyH * 0.5f,
                                                           agent.position.z);
                XMMATRIX torsoWorld = torsoScale * torsoRot * torsoTrans;
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(torsoWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindVS(ctx, 1);
                m_cubeMesh.Draw(ctx);

                // Head
                float headDiam = headR * 2.0f;
                XMMATRIX headScale = XMMatrixScaling(headDiam, headDiam, headDiam);
                XMMATRIX headTrans = XMMatrixTranslation(agent.position.x,
                                                          agent.position.y + bodyH + headR,
                                                          agent.position.z);
                XMMATRIX headWorld = headScale * headTrans;
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(headWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindVS(ctx, 1);
                m_sphereMesh.Draw(ctx);
            }
        }

        m_shadowMap.EndShadowPass(ctx);
    }

    // ============================================================
    // PASS 2: Main Scene
    // ============================================================
    m_renderer.BeginFrame(m_editorState.clearColor[0], m_editorState.clearColor[1],
                          m_editorState.clearColor[2], m_editorState.clearColor[3]);

    // ---- Per-Frame Setup (b0) ----
    XMMATRIX view     = m_camera.GetViewMatrix();

    // Apply screen shake offset to the view
    if (m_camera.IsShaking()) {
        XMFLOAT3 shake = m_camera.GetShakeOffset();
        view = view * XMMatrixTranslation(shake.x, shake.y, shake.z);
    }

    XMMATRIX proj     = m_camera.GetProjectionMatrix();
    XMMATRIX viewProj = view * proj;

    // ---- Frustum Culling & Level Streaming ----
    {
        int entityCount = m_editorState.scene.GetEntityCount();
        float streamDist = (m_editorState.cullingEnabled && m_editorState.streamingEnabled)
                           ? m_editorState.streamDistance : 0.0f;

        // Invalidate cached bounds when scene structure changes
        if (m_editorState.entityDirty) {
            m_sceneCuller.InvalidateBounds();
            m_editorState.entityDirty = false;
        }

        if (m_editorState.cullingEnabled) {
            // Build per-category absolute distances from base * multiplier
            float catDists[5] = {
                streamDist * m_editorState.cullDistBuilding,
                streamDist * m_editorState.cullDistProp,
                streamDist * m_editorState.cullDistDetail,
                streamDist * m_editorState.cullDistFoliage,
                0.0f  // Always — unused, handled as special case in culler
            };
            m_sceneCuller.Update(entityCount,
                [this](int i) -> const Entity& { return m_editorState.scene.GetEntity(i); },
                viewProj, m_camera.GetPosition(), streamDist, m_editorState.bvhAcceleration,
                catDists);

            // Shadow frustum culling
            if (m_editorState.shadowsEnabled) {
                m_sceneCuller.UpdateShadowFrustum(entityCount,
                    [this](int i) -> const Entity& { return m_editorState.scene.GetEntity(i); },
                    lightVP, m_camera.GetPosition(), m_editorState.shadowCullDistance);
            }

            // Update stats for editor display
            auto& cs = m_sceneCuller.GetStats();
            m_editorState.cullStatsTotal    = cs.totalEntities;
            m_editorState.cullStatsFrustum  = cs.frustumCulled;
            m_editorState.cullStatsDistance  = cs.distanceCulled;
            m_editorState.cullStatsRendered  = cs.rendered;
            m_editorState.bvhNodeCount      = m_sceneCuller.GetBVHNodeCount();
            m_editorState.bvhDepth          = m_sceneCuller.GetBVHDepth();
        } else {
            m_editorState.cullStatsTotal    = entityCount;
            m_editorState.cullStatsFrustum  = 0;
            m_editorState.cullStatsDistance  = 0;
            m_editorState.cullStatsRendered  = entityCount;
        }
    }

    CBPerFrame frameData = {};
    XMStoreFloat4x4(&frameData.View,           XMMatrixTranspose(view));
    XMStoreFloat4x4(&frameData.Projection,     XMMatrixTranspose(proj));
    XMStoreFloat4x4(&frameData.ViewProjection, XMMatrixTranspose(viewProj));

    XMVECTOR det = XMMatrixDeterminant(viewProj);
    XMMATRIX invVP = XMMatrixInverse(&det, viewProj);
    XMStoreFloat4x4(&frameData.InvViewProjection, XMMatrixTranspose(invVP));

    frameData.CameraPosition = m_camera.GetPosition();
    frameData.Time       = m_timer.TotalTime();
    frameData.ScreenSize = { static_cast<float>(m_width), static_cast<float>(m_height) };
    frameData.NearZ      = 0.1f;
    frameData.FarZ       = 500.0f;
    m_cbPerFrame.Update(ctx, frameData);
    m_cbPerFrame.BindBoth(ctx, 0);

    // ---- Lighting Setup (b2) ----
    CBLighting lightData = {};
    lightData.SunDirection     = sunDir;
    lightData.SunIntensity     = m_editorState.sunIntensity;
    lightData.SunColor         = { m_editorState.sunColor[0], m_editorState.sunColor[1], m_editorState.sunColor[2] };
    lightData.AmbientColor     = { m_editorState.ambientColor[0], m_editorState.ambientColor[1], m_editorState.ambientColor[2] };
    lightData.AmbientIntensity = m_editorState.ambientIntensity;
    lightData.FogColor         = { m_editorState.fogColor[0], m_editorState.fogColor[1], m_editorState.fogColor[2] };
    lightData.FogDensity       = m_editorState.fogDensity;
    lightData.FogStart         = m_editorState.fogStart;
    lightData.FogEnd           = m_editorState.fogEnd;
    lightData.CelEnabled       = m_editorState.celEnabled ? 1.0f : 0.0f;
    lightData.CelRimIntensity  = m_editorState.celRimIntensity;
    lightData.CelBands         = m_editorState.celBands;
    m_cbLighting.Update(ctx, lightData);
    m_cbLighting.BindPS(ctx, 2);

    // ---- Sky Setup (b3) ----
    CBSky skyData = {};
    skyData.ZenithColor      = { m_editorState.skyZenithColor[0], m_editorState.skyZenithColor[1], m_editorState.skyZenithColor[2] };
    skyData.Brightness       = m_editorState.skyBrightness;
    skyData.HorizonColor     = { m_editorState.skyHorizonColor[0], m_editorState.skyHorizonColor[1], m_editorState.skyHorizonColor[2] };
    skyData.HorizonFalloff   = m_editorState.skyHorizonFalloff;
    skyData.GroundColor      = { m_editorState.skyGroundColor[0], m_editorState.skyGroundColor[1], m_editorState.skyGroundColor[2] };
    skyData.SunDiscSize      = m_editorState.sunDiscSize;
    skyData.SunGlowIntensity = m_editorState.sunGlowIntensity;
    skyData.SunGlowFalloff   = m_editorState.sunGlowFalloff;
    skyData.CloudCoverage    = m_editorState.cloudCoverage;
    skyData.CloudSpeed       = m_editorState.cloudSpeed;
    skyData.CloudDensity     = m_editorState.cloudDensity;
    skyData.CloudHeight      = m_editorState.cloudHeight;
    skyData.CloudColor       = { m_editorState.cloudColor[0], m_editorState.cloudColor[1], m_editorState.cloudColor[2] };
    skyData.CloudSunInfluence = m_editorState.cloudSunInfluence;
    m_cbSky.Update(ctx, skyData);
    m_cbSky.BindBoth(ctx, 3);

    // ---- Shadow Setup (b4) — for main pass sampling ----
    CBShadow shadowData = {};
    XMStoreFloat4x4(&shadowData.LightViewProjection, XMMatrixTranspose(lightVP));
    shadowData.ShadowBias       = m_editorState.shadowBias;
    shadowData.ShadowNormalBias = m_editorState.shadowNormalBias;
    shadowData.ShadowIntensity  = m_editorState.shadowsEnabled ? m_editorState.shadowIntensity : 0.0f;
    shadowData.ShadowMapSize    = static_cast<float>(m_shadowMap.GetResolution());
    m_cbShadow.Update(ctx, shadowData);
    m_cbShadow.BindBoth(ctx, 4);

    // Bind shadow map SRV for scene shaders to sample
    m_shadowMap.BindSRV(ctx, 0);

    // ---- Post-Processing: Redirect rendering to HDR buffer ----
    bool postProcessOn = m_postProcessSettings.bloomEnabled || m_postProcessSettings.vignetteEnabled
                         || m_postProcessSettings.outlineEnabled
                         || m_postProcessSettings.paperGrainIntensity > 0.001f
                         || m_postProcessSettings.hatchingIntensity > 0.001f;
    if (postProcessOn) {
        // When post-processing is on, we render to a non-MSAA HDR buffer
        // Clear the non-MSAA depth buffer (BeginFrame may have only cleared MSAA DSV)
        ctx->ClearDepthStencilView(m_renderer.GetNonMSAADSV(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        // Redirect scene rendering to the HDR buffer
        m_postProcess.BeginSceneCapture(ctx, m_renderer.GetNonMSAADSV());
    }

    // ---- Draw Sky (fullscreen triangle, no depth write) ----
    m_renderer.SetDepthEnabled(false);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->VSSetShader(m_skyShader.GetVS(), nullptr, 0);
    ctx->PSSetShader(m_skyShader.GetPS(), nullptr, 0);
    ctx->Draw(3, 0);
    m_renderer.TrackDrawCall(3);
    m_renderer.SetDepthEnabled(true);

    // ---- Draw Objects ----
    m_voxelShader.Bind(ctx);

    CBPerObject objData = {};

    // Ground plane
    m_groundShader.Bind(ctx);
    // Bind white texture for ground (uses procedural shader pattern, not texture)
    Texture* defaultWhiteTex = ResourceManager::Get().GetTexture("_white");
    if (defaultWhiteTex) defaultWhiteTex->Bind(ctx, 1);
    XMMATRIX groundWorld = XMMatrixTranslation(0.0f, -0.01f, 0.0f);
    XMStoreFloat4x4(&objData.World, XMMatrixTranspose(groundWorld));
    XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, groundWorld));
    m_cbPerObject.Update(ctx, objData);
    m_cbPerObject.BindBoth(ctx, 1);
    m_groundMesh.Draw(ctx);
    m_renderer.TrackDrawCall(m_groundMesh.GetIndexCount());

    // ---- Draw Entities ----
    m_voxelShader.Bind(ctx);
    Texture* whiteTex = ResourceManager::Get().GetTexture("_white");
    for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
        const auto& e = m_editorState.scene.GetEntity(i);
        if (!e.visible) continue;

        // Frustum + distance culling
        if (m_editorState.cullingEnabled && !m_sceneCuller.IsVisible(i)) continue;

        // Use damage-tinted color (darkens + flash on hit)
        float damagedColor[4];
        e.GetDamagedColor(damagedColor);
        // Custom meshes: alpha=0 tells shader to use mesh UVs + vertex colors
        // Cubes: alpha>0 triggers triplanar world-space UVs with entity color
        float colorAlpha = (e.meshType == MeshType::Custom) ? 0.0f : damagedColor[3];
        objData.ObjectColor = { damagedColor[0], damagedColor[1], damagedColor[2], colorAlpha };

        // Fill hit decal data for the shader
        for (int di = 0; di < Entity::MAX_HIT_DECALS; di++) {
            if (di < e.hitDecalCount) {
                objData.HitDecals[di] = { e.hitDecalPos[di].x, e.hitDecalPos[di].y,
                                           e.hitDecalPos[di].z, e.hitDecalIntensity[di] };
            } else {
                objData.HitDecals[di] = { 0, 0, 0, 0 };
            }
        }
        objData.HitDecalCount = static_cast<float>(e.hitDecalCount);

        // Bind texture once for this entity (uses cached pointer to avoid hash lookup)
        if (e.textureCacheDirty) {
            bool ignorePlaceholderCubeTexture =
                e.meshType == MeshType::Cube &&
                (e.textureName == "Walls/texture" ||
                 e.textureName == "Floors/texture" ||
                 e.textureName == "Props/texture");

            if (!e.textureName.empty() && !ignorePlaceholderCubeTexture)
                e.cachedTexture = ResourceManager::Get().GetTexture(e.textureName);
            else if (e.meshType == MeshType::Custom && !e.meshName.empty())
                e.cachedTexture = ResourceManager::Get().GetTexture(e.meshName);
            else
                e.cachedTexture = nullptr;
            e.textureCacheDirty = false;
        }
        Texture* boundTex = e.cachedTexture ? e.cachedTexture : whiteTex;
        if (boundTex) boundTex->Bind(ctx, 1);

        // Voxel chunk rendering: draw each active cell as a sub-cube
        if (e.voxelDestruction && e.meshType == MeshType::Cube) {
            int res = e.voxelRes;
            for (int vz = 0; vz < res; vz++)
            for (int vy = 0; vy < res; vy++)
            for (int vx = 0; vx < res; vx++) {
                int idx = vx + vy * res + vz * res * res;
                if (!e.IsVoxelCellActive(idx)) continue;

                XMMATRIX cellWorld = e.GetVoxelCellWorldMatrix(vx, vy, vz);
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(cellWorld));
                XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, cellWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindBoth(ctx, 1);
                m_cubeMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
            }
        } else {
            // Normal full-entity rendering
            XMMATRIX entWorld = e.GetWorldMatrix();
            XMStoreFloat4x4(&objData.World, XMMatrixTranspose(entWorld));
            XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, entWorld));
            m_cbPerObject.Update(ctx, objData);
            m_cbPerObject.BindBoth(ctx, 1);

            if (e.meshType == MeshType::Cube) {
                m_cubeMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
            } else if (e.meshType == MeshType::Custom) {
                Mesh* customMesh = ResourceManager::Get().GetMesh(e.meshName);
                if (customMesh) {
                    customMesh->Draw(ctx);
                    m_renderer.TrackDrawCall(customMesh->GetIndexCount());
                }
            }
        }
    }

    // Reset ObjectColor + hit decals after entities
    objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    objData.HitDecalCount = 0.0f;
    for (int di = 0; di < 4; di++) objData.HitDecals[di] = { 0, 0, 0, 0 };
    m_cbPerObject.Update(ctx, objData);
    m_cbPerObject.BindBoth(ctx, 1);

    // ---- Draw Debris Fragments (small 3D cubes from destruction) ----
    {
        m_voxelShader.Bind(ctx);
        if (whiteTex) whiteTex->Bind(ctx, 1);

        const auto& debris = m_debrisManager.GetFragments();
        for (const auto& f : debris) {
            if (!f.alive || f.alpha <= 0.0f) continue;

            objData.ObjectColor = { f.color[0], f.color[1], f.color[2], f.alpha };
            objData.HitDecalCount = 0.0f;

            XMMATRIX S = XMMatrixScaling(f.scale.x, f.scale.y, f.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(f.rotation.x),
                XMConvertToRadians(f.rotation.y),
                XMConvertToRadians(f.rotation.z));
            XMMATRIX T = XMMatrixTranslation(f.position.x, f.position.y, f.position.z);
            XMMATRIX W = S * R * T;

            XMStoreFloat4x4(&objData.World, XMMatrixTranspose(W));
            XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, W));
            m_cbPerObject.Update(ctx, objData);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        }
    }

    // ---- Draw Fracture Fragments (detached voxel clusters as cubes) ----
    {
        m_voxelShader.Bind(ctx);
        if (whiteTex) whiteTex->Bind(ctx, 1);

        const auto& frags = m_fractureSystem.GetFragments();
        for (const auto& f : frags) {
            if (!f.alive || f.alpha <= 0.0f) continue;

            objData.ObjectColor = { f.color[0], f.color[1], f.color[2], f.alpha };
            objData.HitDecalCount = 0.0f;

            XMMATRIX S = XMMatrixScaling(f.scale.x, f.scale.y, f.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(f.rotation.x),
                XMConvertToRadians(f.rotation.y),
                XMConvertToRadians(f.rotation.z));
            XMMATRIX T = XMMatrixTranslation(f.position.x, f.position.y, f.position.z);
            XMMATRIX W = S * R * T;

            XMStoreFloat4x4(&objData.World, XMMatrixTranspose(W));
            XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, W));
            m_cbPerObject.Update(ctx, objData);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        }
    }

    // Reset ObjectColor after debris
    objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_cbPerObject.Update(ctx, objData);
    m_cbPerObject.BindBoth(ctx, 1);

    // ---- Draw GPU Particles (billboard, instanced) ----
    {
        ID3D11ShaderResourceView* whiteSRV = m_defaultWhite.GetSRV();
        m_gpuParticles.Render(ctx, m_cbPerFrame.GetBuffer(), whiteSRV, m_renderer.GetDepthSRV());
    }

    // ---- Draw Character Body (cube body parts) ----
    if (m_characterMode && m_editorState.charShowBody) {
        m_voxelShader.Bind(ctx);

        auto drawBodyPart = [&](const Character::BodyPart& bp, const float color[4]) {
            XMMATRIX S = XMMatrixScaling(bp.scale.x, bp.scale.y, bp.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(bp.rotation.x),
                XMConvertToRadians(bp.rotation.y),
                XMConvertToRadians(bp.rotation.z));
            XMMATRIX T = XMMatrixTranslation(bp.position.x, bp.position.y, bp.position.z);
            XMMATRIX bpWorld = S * R * T;

            CBPerObject bpObj = {};
            XMStoreFloat4x4(&bpObj.World, XMMatrixTranspose(bpWorld));
            XMStoreFloat4x4(&bpObj.WorldInvTranspose, XMMatrixInverse(nullptr, bpWorld));
            bpObj.ObjectColor = { color[0], color[1], color[2], color[3] };
            m_cbPerObject.Update(ctx, bpObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        };

        drawBodyPart(m_character.GetHeadTransform(),     m_charSettings.headColor);
        drawBodyPart(m_character.GetTorsoTransform(),     m_charSettings.torsoColor);
        drawBodyPart(m_character.GetLeftArmTransform(),   m_charSettings.armsColor);
        drawBodyPart(m_character.GetRightArmTransform(),  m_charSettings.armsColor);
        drawBodyPart(m_character.GetLeftLegTransform(),    m_charSettings.legsColor);
        drawBodyPart(m_character.GetRightLegTransform(),   m_charSettings.legsColor);

        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // ---- Draw Remote Players (networked multiplayer) ----
    if (m_networkManager.IsClient()) {
        m_voxelShader.Bind(ctx);
        const auto& remotePlayers = m_networkManager.GetClientState().GetRemotePlayers();

        // Colors for remote player body
        const float rpHead[4]  = { 0.85f, 0.70f, 0.55f, 1.0f };  // Skin
        const float rpTorso[4] = { 0.50f, 0.20f, 0.20f, 1.0f };  // Red-ish
        const float rpArms[4]  = { 0.50f, 0.20f, 0.20f, 1.0f };
        const float rpLegs[4]  = { 0.30f, 0.18f, 0.15f, 1.0f };

        auto drawRemotePart = [&](XMFLOAT3 pos, float yawDeg, XMFLOAT3 scale, const float color[4]) {
            XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
            XMMATRIX R = XMMatrixRotationY(XMConvertToRadians(yawDeg));
            XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
            XMMATRIX W = S * R * T;

            CBPerObject rpObj = {};
            XMStoreFloat4x4(&rpObj.World, XMMatrixTranspose(W));
            XMStoreFloat4x4(&rpObj.WorldInvTranspose, XMMatrixInverse(nullptr, W));
            rpObj.ObjectColor = { color[0], color[1], color[2], color[3] };
            m_cbPerObject.Update(ctx, rpObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        };

        for (const auto& remote : remotePlayers) {
            float px = remote.displayPosition.x;
            float fy = remote.displayPosition.y; // Feet Y (position is feet-level)
            float pz = remote.displayPosition.z;
            float yaw = remote.displayYaw;
            float yawRad = XMConvertToRadians(yaw);
            float sx = sinf(yawRad), cx = cosf(yawRad);

            // Head
            drawRemotePart({ px, fy + 1.55f, pz }, yaw, { 0.3f, 0.3f, 0.3f }, rpHead);
            // Torso
            drawRemotePart({ px, fy + 1.1f, pz }, yaw, { 0.35f, 0.4f, 0.2f }, rpTorso);
            // Left arm
            drawRemotePart({ px - cx * 0.3f, fy + 1.1f, pz + sx * 0.3f }, yaw, { 0.12f, 0.35f, 0.12f }, rpArms);
            // Right arm
            drawRemotePart({ px + cx * 0.3f, fy + 1.1f, pz - sx * 0.3f }, yaw, { 0.12f, 0.35f, 0.12f }, rpArms);
            // Left leg
            drawRemotePart({ px - cx * 0.12f, fy + 0.35f, pz + sx * 0.12f }, yaw, { 0.14f, 0.35f, 0.14f }, rpLegs);
            // Right leg
            drawRemotePart({ px + cx * 0.12f, fy + 0.35f, pz - sx * 0.12f }, yaw, { 0.14f, 0.35f, 0.14f }, rpLegs);
        }

        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // ---- Draw Server-Side Players (server spectator view) ----
    if (m_networkManager.IsServer()) {
        m_voxelShader.Bind(ctx);
        const auto& serverPlayers = m_networkManager.GetServerState().GetPlayers();

        auto drawServerPlayerPart = [&](XMFLOAT3 pos, float yawDeg, XMFLOAT3 scale, const float color[4]) {
            XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
            XMMATRIX R = XMMatrixRotationY(XMConvertToRadians(yawDeg));
            XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
            XMMATRIX W = S * R * T;

            CBPerObject spObj = {};
            XMStoreFloat4x4(&spObj.World, XMMatrixTranspose(W));
            XMStoreFloat4x4(&spObj.WorldInvTranspose, XMMatrixInverse(nullptr, W));
            spObj.ObjectColor = { color[0], color[1], color[2], color[3] };
            m_cbPerObject.Update(ctx, spObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        };

        // Alternate colors per player
        const float pColorsHead[][4]  = { {0.85f,0.70f,0.55f,1}, {0.75f,0.60f,0.50f,1} };
        const float pColorsTorso[][4] = { {0.20f,0.40f,0.70f,1}, {0.70f,0.25f,0.20f,1} };
        const float pColorsLegs[][4]  = { {0.25f,0.25f,0.35f,1}, {0.35f,0.22f,0.15f,1} };

        for (size_t pi = 0; pi < serverPlayers.size(); pi++) {
            const auto& sp = serverPlayers[pi];
            if (!sp.alive) continue;

            int ci = static_cast<int>(pi) % 2;
            float px = sp.position.x;
            float fy = sp.position.y; // Feet Y
            float pz = sp.position.z;
            float yaw = sp.yaw; // Degrees
            float yawRad = XMConvertToRadians(yaw);
            float sx = sinf(yawRad), cx = cosf(yawRad);

            drawServerPlayerPart({ px, fy + 1.55f, pz }, yaw, { 0.3f, 0.3f, 0.3f }, pColorsHead[ci]);
            drawServerPlayerPart({ px, fy + 1.1f, pz }, yaw, { 0.35f, 0.4f, 0.2f }, pColorsTorso[ci]);
            drawServerPlayerPart({ px - cx*0.3f, fy + 1.1f, pz + sx*0.3f }, yaw, { 0.12f, 0.35f, 0.12f }, pColorsTorso[ci]);
            drawServerPlayerPart({ px + cx*0.3f, fy + 1.1f, pz - sx*0.3f }, yaw, { 0.12f, 0.35f, 0.12f }, pColorsTorso[ci]);
            drawServerPlayerPart({ px - cx*0.12f, fy + 0.35f, pz + sx*0.12f }, yaw, { 0.14f, 0.35f, 0.14f }, pColorsLegs[ci]);
            drawServerPlayerPart({ px + cx*0.12f, fy + 0.35f, pz - sx*0.12f }, yaw, { 0.14f, 0.35f, 0.14f }, pColorsLegs[ci]);
        }

        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // ---- Draw AI Agents ----
    {
        if (m_editorState.aiXRayAgents)
            m_renderer.SetDepthEnabled(false);

        m_voxelShader.Bind(ctx);
        for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
            const auto& agent = m_aiSystem.GetAgent(i);
            if (!agent.visible || !agent.active) continue;

            float bs = agent.settings.bodyScale;

            // Color (flash white on damage)
            XMFLOAT4 agentColor;
            if (agent.damageFlashTimer > 0.0f) {
                agentColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            } else {
                agentColor = { agent.settings.bodyColor[0],
                               agent.settings.bodyColor[1],
                               agent.settings.bodyColor[2],
                               agent.settings.bodyColor[3] };
            }

            CBPerObject agentObj = {};

            if (agent.type == AIAgentType::Drone) {
                // ---- Drone: sphere body ----
                XMMATRIX droneScale = XMMatrixScaling(bs, bs, bs);
                XMMATRIX droneRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
                // Apply pitch/roll tilt
                XMMATRIX droneTilt  = XMMatrixRotationRollPitchYaw(
                    XMConvertToRadians(agent.dronePitch),
                    0.0f,
                    XMConvertToRadians(agent.droneRoll));
                XMMATRIX droneTrans = XMMatrixTranslation(agent.position.x,
                                                           agent.position.y,
                                                           agent.position.z);
                XMMATRIX droneWorld = droneScale * droneTilt * droneRot * droneTrans;

                XMStoreFloat4x4(&agentObj.World, XMMatrixTranspose(droneWorld));
                XMStoreFloat4x4(&agentObj.WorldInvTranspose, XMMatrixInverse(nullptr, droneWorld));
                agentObj.ObjectColor = agentColor;
                m_cbPerObject.Update(ctx, agentObj);
                m_cbPerObject.BindBoth(ctx, 1);
                m_sphereMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_sphereMesh.GetIndexCount());

                // ---- Drone: 4 rotor meshes ----
                float yawRad = XMConvertToRadians(agent.yaw);
                float armLen = bs * 0.5f * 1.4f;  // Same as debug propeller arm length
                float propY  = agent.position.y + bs * 0.5f * 0.3f;
                float rotorScale = bs * 0.4f;     // Rotor disc size relative to body
                float pitchRad = XMConvertToRadians(agent.dronePitch);
                float rollRad  = XMConvertToRadians(agent.droneRoll);

                for (int p = 0; p < 4; p++) {
                    if (!agent.rotorAlive[p]) continue; // Don't render destroyed rotors

                    // Fixed rotor position (no spin in position — rotors stay in place)
                    float angle = yawRad + (p * 1.5708f) + 0.7854f;
                    float px = std::sin(angle) * armLen;
                    float pz = std::cos(angle) * armLen;
                    // Apply pitch/roll tilt to rotor position
                    float tipY = propY - px * std::sin(pitchRad) + pz * std::sin(rollRad);

                    // Disc spins in place (visual only)
                    float discSpin = agent.droneBobPhase * 15.0f * agent.rotorSpinSpeed[p];
                    XMMATRIX rScale = XMMatrixScaling(rotorScale, rotorScale, rotorScale);
                    XMMATRIX rSpin  = XMMatrixRotationY(discSpin);
                    XMMATRIX rTilt  = XMMatrixRotationRollPitchYaw(pitchRad, 0.0f, rollRad);
                    XMMATRIX rTrans = XMMatrixTranslation(
                        agent.position.x + px * std::cos(pitchRad),
                        tipY,
                        agent.position.z + pz * std::cos(rollRad));
                    XMMATRIX rotorWorld = rScale * rSpin * rTilt * rTrans;

                    // Darken damaged rotors
                    XMFLOAT4 rotorColor = { 0.5f, 0.5f, 0.55f, 1.0f };
                    float healthPct = agent.rotorHealth[p] / agent.settings.droneRotorHealth;
                    if (healthPct < 0.5f) {
                        rotorColor = { 0.8f, 0.3f, 0.1f, 1.0f }; // Orange = damaged
                    }

                    XMStoreFloat4x4(&agentObj.World, XMMatrixTranspose(rotorWorld));
                    XMStoreFloat4x4(&agentObj.WorldInvTranspose, XMMatrixInverse(nullptr, rotorWorld));
                    agentObj.ObjectColor = rotorColor;
                    m_cbPerObject.Update(ctx, agentObj);
                    m_cbPerObject.BindBoth(ctx, 1);
                    m_rotorMesh.Draw(ctx);
                    m_renderer.TrackDrawCall(m_rotorMesh.GetIndexCount());
                }
            } else {
                // ---- Ground agent: body (tall cube) + sphere head ----
                float bodyW = bs * 0.7f;   // Narrower than tall
                float bodyH = bs * 1.0f;   // Torso height
                float bodyD = bs * 0.5f;   // Depth
                float headR = bs * 0.35f;  // Head radius

                // Torso (cube) — centered above ground
                XMMATRIX torsoScale = XMMatrixScaling(bodyW, bodyH, bodyD);
                XMMATRIX torsoRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
                XMMATRIX torsoTrans = XMMatrixTranslation(agent.position.x,
                                                           agent.position.y + bodyH * 0.5f,
                                                           agent.position.z);
                XMMATRIX torsoWorld = torsoScale * torsoRot * torsoTrans;

                XMStoreFloat4x4(&agentObj.World, XMMatrixTranspose(torsoWorld));
                XMStoreFloat4x4(&agentObj.WorldInvTranspose, XMMatrixInverse(nullptr, torsoWorld));
                agentObj.ObjectColor = agentColor;
                m_cbPerObject.Update(ctx, agentObj);
                m_cbPerObject.BindBoth(ctx, 1);
                m_cubeMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());

                // Head (sphere) — on top of torso
                float headDiam = headR * 2.0f;
                XMMATRIX headScale = XMMatrixScaling(headDiam, headDiam, headDiam);
                XMMATRIX headRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
                XMMATRIX headTrans = XMMatrixTranslation(agent.position.x,
                                                          agent.position.y + bodyH + headR,
                                                          agent.position.z);
                XMMATRIX headWorld = headScale * headRot * headTrans;

                // Slightly lighter color for head
                XMFLOAT4 headColor = agentColor;
                headColor.x = (std::min)(headColor.x * 1.2f, 1.0f);
                headColor.y = (std::min)(headColor.y * 1.2f, 1.0f);
                headColor.z = (std::min)(headColor.z * 1.2f, 1.0f);

                XMStoreFloat4x4(&agentObj.World, XMMatrixTranspose(headWorld));
                XMStoreFloat4x4(&agentObj.WorldInvTranspose, XMMatrixInverse(nullptr, headWorld));
                agentObj.ObjectColor = headColor;
                m_cbPerObject.Update(ctx, agentObj);
                m_cbPerObject.BindBoth(ctx, 1);
                m_sphereMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_sphereMesh.GetIndexCount());
            }
        }

        // Reset ObjectColor
        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);

        if (m_editorState.aiXRayAgents)
            m_renderer.SetDepthEnabled(true);
    }

    // ---- Draw Weapon Viewmodel (first-person gun) ----
    if (m_characterMode) {
        m_voxelShader.Bind(ctx);

        // Clear depth so viewmodel always renders on top
        ID3D11DepthStencilView* activeDSV = postProcessOn
            ? m_renderer.GetNonMSAADSV()
            : m_renderer.GetCurrentDSV();
        ctx->ClearDepthStencilView(activeDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

        // Draw gun model mesh (if using a loaded model)
        if (m_weaponSystem.HasGunModel()) {
            const auto& vm = m_weaponSystem.GetViewmodelMesh();
            if (!vm.meshName.empty()) {
                Mesh* gunMesh = ResourceManager::Get().GetMesh(vm.meshName);
                if (gunMesh) {
                    XMMATRIX S = XMMatrixScaling(vm.scale.x, vm.scale.y, vm.scale.z);
                    XMMATRIX R = XMMatrixRotationRollPitchYaw(
                        XMConvertToRadians(vm.rotation.x),
                        XMConvertToRadians(vm.rotation.y),
                        XMConvertToRadians(vm.rotation.z));
                    XMMATRIX T = XMMatrixTranslation(vm.position.x, vm.position.y, vm.position.z);
                    XMMATRIX gunWorld = S * R * T;

                    CBPerObject vmObj = {};
                    XMStoreFloat4x4(&vmObj.World, XMMatrixTranspose(gunWorld));
                    XMStoreFloat4x4(&vmObj.WorldInvTranspose, XMMatrixInverse(nullptr, gunWorld));
                    vmObj.ObjectColor = { 0, 0, 0, 0 }; // alpha=0 → use vertex colors
                    m_cbPerObject.Update(ctx, vmObj);
                    m_cbPerObject.BindBoth(ctx, 1);

                    // Bind gun texture if specified, otherwise white fallback
                    Texture* gunTex = nullptr;
                    if (!vm.textureName.empty())
                        gunTex = ResourceManager::Get().GetTexture(vm.textureName);
                    if (!gunTex)
                        gunTex = ResourceManager::Get().GetTexture(vm.meshName);
                    if (gunTex) gunTex->Bind(ctx, 1);
                    else if (whiteTex) whiteTex->Bind(ctx, 1);

                    // Use no-cull rasterizer for gun mesh (safety net for mixed winding)
                    ctx->RSSetState(m_renderer.GetNoCullState());
                    gunMesh->Draw(ctx);
                    m_renderer.TrackDrawCall(gunMesh->GetIndexCount());
                    // Restore normal solid rasterizer
                    ctx->RSSetState(m_renderer.GetSolidState());
                }
            }
        }

        // Draw cube-based viewmodel parts (muzzle flash, etc.)
        if (whiteTex) whiteTex->Bind(ctx, 1);
        const auto& viewmodelParts = m_weaponSystem.GetViewmodelParts();
        for (const auto& vp : viewmodelParts) {
            const auto& bp = vp.transform;
            XMMATRIX S = XMMatrixScaling(bp.scale.x, bp.scale.y, bp.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(bp.rotation.x),
                XMConvertToRadians(bp.rotation.y),
                XMConvertToRadians(bp.rotation.z));
            XMMATRIX T = XMMatrixTranslation(bp.position.x, bp.position.y, bp.position.z);
            XMMATRIX bpWorld = S * R * T;

            CBPerObject vmObj = {};
            XMStoreFloat4x4(&vmObj.World, XMMatrixTranspose(bpWorld));
            XMStoreFloat4x4(&vmObj.WorldInvTranspose, XMMatrixInverse(nullptr, bpWorld));
            vmObj.ObjectColor = { vp.color[0], vp.color[1], vp.color[2], vp.color[3] };
            m_cbPerObject.Update(ctx, vmObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        }

        // Reset ObjectColor
        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // Unbind shadow map before next frame's shadow pass
    m_shadowMap.UnbindSRV(ctx, 0);

    // ---- SSAO: Compute ambient occlusion from depth ----
    if (m_ssaoSettings.enabled) {
        // Unbind depth stencil as render target before reading it
        ctx->OMSetRenderTargets(0, nullptr, nullptr);

        XMMATRIX ssaoView = m_camera.GetViewMatrix();
        XMMATRIX ssaoProj = m_camera.GetProjectionMatrix();
        m_ssao.Compute(ctx, m_renderer.GetDepthSRV(), ssaoProj, ssaoView, 0.1f, 500.0f, m_ssaoSettings);

        // Unbind SSAO render targets before binding the AO result as SRV
        // (prevents D3D11 read-write conflict that would force-unbind the SRV)
        ctx->OMSetRenderTargets(0, nullptr, nullptr);

        // Bind AO texture at t4 for the composite shader to use
        ID3D11ShaderResourceView* aoSRV = m_ssao.GetAOTexture();
        ctx->PSSetShaderResources(4, 1, &aoSRV);
    } else {
        // Unbind AO texture if SSAO is off
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(4, 1, &nullSRV);
    }

    // ---- Post-Processing: Apply bloom, vignette, color grading ----
    if (postProcessOn) {
        // When FSR is enabled, post-process outputs to FSR's render-res intermediate RTV
        // Then FSR upscales that to the back buffer at native resolution
        bool fsrActive = m_editorState.fsrEnabled && m_fsrUpscaler.GetRenderRTV();
        ID3D11RenderTargetView* ppOutputRTV = fsrActive
            ? m_fsrUpscaler.GetRenderRTV()
            : m_renderer.GetBackBufferRTV();

        // Composite post-processing
        m_postProcess.Apply(ctx, ppOutputRTV, m_postProcessSettings,
                            m_renderer.GetDepthSRV());

        if (fsrActive) {
            // FSR upscale: render-res → native back buffer
            m_fsrUpscaler.Apply(ctx, m_renderer.GetBackBufferRTV(),
                                m_width, m_height,
                                m_editorState.fsrSharpness);
        }

        // Restore the renderer's render target for debug + ImGui
        ID3D11RenderTargetView* rtv = m_renderer.GetBackBufferRTV();
        ctx->OMSetRenderTargets(1, &rtv, m_renderer.GetNonMSAADSV());

        // Restore viewport
        D3D11_VIEWPORT vp = {};
        vp.Width  = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        // Skip MSAA resolve since post-process already output to back buffer
        m_renderer.SetSkipMSAAResolve(true);
    } else {
        m_renderer.SetSkipMSAAResolve(false);
    }

    // Unbind AO texture
    if (m_ssaoSettings.enabled) {
        m_ssao.Unbind(ctx);
    }

    // ---- Gun Laser Sight ----
    if (m_characterMode && m_editorState.weaponLaserEnabled) {
        // Temporarily enable debug renderer for laser (even if debug view is off)
        bool wasEnabled = m_debugRenderer.IsEnabled();
        m_debugRenderer.SetEnabled(true);

        XMFLOAT3 camPos = m_camera.GetPosition();
        XMFLOAT3 camFwd = m_camera.GetForward();
        XMFLOAT3 camRight = m_camera.GetRight();
        XMFLOAT3 camUp    = m_camera.GetUp();

        // Compute gun barrel tip in world space
        // Use weapon viewmodel offsets + per-weapon model offsets to place laser origin at barrel end
        const auto& ws = m_weaponSystem.GetSettings();
        const auto& wd = m_weaponSystem.GetCurrentDef();
        float gunOffX = ws.viewmodelOffsetX + wd.modelOffsetX;
        float gunOffY = ws.viewmodelOffsetY + wd.modelOffsetY;
        float gunOffZ = ws.viewmodelOffsetZ + wd.modelOffsetZ;
        float barrelLen = wd.barrelLength;

        // Barrel tip = camera + viewmodel offset + forward * barrel length + per-weapon laser offset
        float lOffX = wd.laserOffsetX;
        float lOffY = wd.laserOffsetY;
        float lOffZ = wd.laserOffsetZ;
        XMFLOAT3 barrelTip = {
            camPos.x + camRight.x * (gunOffX + lOffX) + camUp.x * (gunOffY + lOffY) + camFwd.x * (gunOffZ + barrelLen + lOffZ),
            camPos.y + camRight.y * (gunOffX + lOffX) + camUp.y * (gunOffY + lOffY) + camFwd.y * (gunOffZ + barrelLen + lOffZ),
            camPos.z + camRight.z * (gunOffX + lOffX) + camUp.z * (gunOffY + lOffY) + camFwd.z * (gunOffZ + barrelLen + lOffZ)
        };

        // Raycast along camera forward to find hit point
        float maxDist = m_editorState.weaponLaserMaxDist;
        XMFLOAT3 laserEnd = {
            camPos.x + camFwd.x * maxDist,
            camPos.y + camFwd.y * maxDist,
            camPos.z + camFwd.z * maxDist
        };

        // Use physics raycast for accurate endpoint
        if (m_editorState.physicsCollisionEnabled) {
            auto hit = m_physicsWorld.Raycast(camPos, camFwd, maxDist);
            if (hit.hit) {
                laserEnd = {
                    camPos.x + camFwd.x * hit.depth,
                    camPos.y + camFwd.y * hit.depth,
                    camPos.z + camFwd.z * hit.depth
                };
            }
        }

        XMFLOAT4 laserColor = {
            m_editorState.weaponLaserColor[0],
            m_editorState.weaponLaserColor[1],
            m_editorState.weaponLaserColor[2],
            1.0f
        };

        // Draw laser beam from barrel tip to hit point
        m_debugRenderer.DrawLine(barrelTip, laserEnd, laserColor);

        // Draw laser dot at endpoint (cross pattern)
        float dotSize = 0.04f;
        m_debugRenderer.DrawLine(
            { laserEnd.x - dotSize, laserEnd.y, laserEnd.z },
            { laserEnd.x + dotSize, laserEnd.y, laserEnd.z },
            laserColor);
        m_debugRenderer.DrawLine(
            { laserEnd.x, laserEnd.y - dotSize, laserEnd.z },
            { laserEnd.x, laserEnd.y + dotSize, laserEnd.z },
            laserColor);
        m_debugRenderer.DrawLine(
            { laserEnd.x, laserEnd.y, laserEnd.z - dotSize },
            { laserEnd.x, laserEnd.y, laserEnd.z + dotSize },
            laserColor);

        // Flush laser lines immediately, then restore debug renderer state
        m_debugRenderer.Flush(ctx);
        m_debugRenderer.SetEnabled(wasEnabled);
    }

    // ---- Debug Rendering ----
    if (m_editorState.showDebug) {
        m_debugRenderer.DrawGrid(20.0f, 20, { 0.4f, 0.4f, 0.4f, 0.5f });
        m_debugRenderer.DrawAxis({ 0.0f, 0.01f, 0.0f }, 3.0f);
        m_debugRenderer.DrawBox({ m_editorState.cubePosition[0], m_editorState.cubePosition[1], m_editorState.cubePosition[2] },
                                 { 0.5f * m_editorState.cubeScale[0], 0.5f * m_editorState.cubeScale[1], 0.5f * m_editorState.cubeScale[2] },
                                 { 1.0f, 1.0f, 0.0f, 0.6f });

        // Draw selection box for selected entity (oriented to match rotation)
        int sel = m_editorState.selectedEntity;
        if (sel >= 0 && sel < m_editorState.scene.GetEntityCount()) {
            const auto& e = m_editorState.scene.GetEntity(sel);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(e.rotation[0]),
                XMConvertToRadians(e.rotation[1]),
                XMConvertToRadians(e.rotation[2]));
            XMFLOAT3X3 rotMat;
            XMStoreFloat3x3(&rotMat, R);
            m_debugRenderer.DrawRotatedBox(
                { e.position[0], e.position[1], e.position[2] },
                { 0.5f * e.scale[0], 0.5f * e.scale[1], 0.5f * e.scale[2] },
                rotMat,
                { 0.2f, 0.8f, 1.0f, 0.8f });
        }

        // Health bars disabled — entities still have health/damage but no visual bar

        // Nav grid debug visualization
        m_navGrid.DebugDraw(m_debugRenderer);

        // AI agent debug visualization
        m_aiSystem.DebugDraw(m_debugRenderer, m_navGrid);

        // Physics collision debug visualization
        m_physicsWorld.DebugDraw(m_debugRenderer);

        // Weapon debug visualization
        m_weaponSystem.DebugDraw(m_debugRenderer);
    }

    // ---- Culling Debug Visualization ----
    if (m_editorState.showCullingDebug && m_editorState.cullingEnabled) {
        int count = m_editorState.scene.GetEntityCount();
        for (int i = 0; i < count; i++) {
            const auto& e = m_editorState.scene.GetEntity(i);
            if (!e.visible) continue;
            const auto& bounds = m_sceneCuller.GetBounds(i);
            XMFLOAT4 color;
            if (m_sceneCuller.IsVisible(i)) {
                color = { 0.0f, 1.0f, 0.0f, 0.5f }; // Green = rendered
            } else {
                // Check if distance culled using per-category distance
                uint8_t cat = static_cast<uint8_t>(e.cullCategory);
                float baseDist = m_editorState.streamDistance;
                float multipliers[5] = {
                    m_editorState.cullDistBuilding, m_editorState.cullDistProp,
                    m_editorState.cullDistDetail, m_editorState.cullDistFoliage, 0.0f
                };
                float catRange = (cat < 4) ? baseDist * multipliers[cat] : 1e18f;
                float dx = bounds.center.x - m_camera.GetPosition().x;
                float dy = bounds.center.y - m_camera.GetPosition().y;
                float dz = bounds.center.z - m_camera.GetPosition().z;
                float dist2 = dx * dx + dy * dy + dz * dz;
                float range = catRange + bounds.boundingSphereRadius;
                if (m_editorState.streamingEnabled && dist2 > range * range)
                    color = { 1.0f, 1.0f, 0.0f, 0.4f }; // Yellow = distance culled
                else
                    color = { 1.0f, 0.0f, 0.0f, 0.5f }; // Red = frustum culled
            }
            m_debugRenderer.DrawBox(bounds.center, bounds.halfExtents, color);
        }
    }

    m_debugRenderer.Flush(ctx);

    // ---- Editor ImGui ----
    if (m_editorVisible) {
        m_editorUI.BeginFrame();
        m_editorPanels.Draw(m_editorState, m_renderer, m_camera,
                            m_timer.DeltaTime(), m_timer.FPS(), m_timer.TotalTime());

        // Draw HUD overlay (inside ImGui frame) when in character mode
        if (m_characterMode) {
            m_hud.Draw(m_weaponSystem, m_character, m_camera.GetYaw(), m_width, m_height, m_timer.FPS());
            m_inventoryUI.Draw(m_inventory, m_interactionSystem,
                               m_character, m_weaponSystem, m_input, m_width, m_height);
        }

        // Always draw interact icon billboards (visible in editor and character mode)
        m_hud.DrawWorldInteractIcons(m_camera, m_editorState.scene, m_width, m_height);

        DrawNetOverlay();

        m_editorUI.EndFrame();
    } else if (m_characterMode) {
        // Editor hidden but we still need ImGui for HUD
        m_editorUI.BeginFrame();
        m_hud.Draw(m_weaponSystem, m_character, m_camera.GetYaw(), m_width, m_height, m_timer.FPS());
        m_hud.DrawWorldInteractIcons(m_camera, m_editorState.scene, m_width, m_height);
        m_inventoryUI.Draw(m_inventory, m_interactionSystem,
                           m_character, m_weaponSystem, m_input, m_width, m_height);
        DrawNetOverlay();
        m_editorUI.EndFrame();
    } else {
        // Fly-cam mode with editor hidden (e.g. dedicated server spectator)
        m_editorUI.BeginFrame();
        DrawNetOverlay();
        m_editorUI.EndFrame();
    }

    m_renderer.EndFrame();
}

// ==================== Net Overlay ====================

void Application::DrawNetOverlay() {
    if (!m_showNetOverlay) return;

    auto role = m_networkManager.GetRole();
    if (role == NetworkRole::Server || role == NetworkRole::ListenServer) {
        DrawServerOverlay();
    } else if (role == NetworkRole::Client) {
        DrawClientOverlay();
    } else {
        // Offline — small hint
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::SetNextWindowPos(ImVec2(10.0f, static_cast<float>(m_height) - 10.0f),
                                ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.4f);
        if (ImGui::Begin("##NetOffline", nullptr, flags)) {
            ImGui::TextDisabled("NET: Offline  |  Alt+H=Host  Alt+J=Join");
        }
        ImGui::End();
    }
}

// ==================== Server Overlay ====================

void Application::DrawServerOverlay() {
    auto stats = m_networkManager.GetStats();

    // ---- Top-left server panel ----
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    float panelW = 340.0f;
    float panelH = 330.0f;
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);

    if (ImGui::Begin("Server Dashboard", nullptr, flags)) {
        // Header
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "SERVER: %s",
                           m_networkManager.serverName.c_str());
        ImGui::Separator();

        // Tick info
        ImGui::Text("Tick:       %u", stats.serverTick);
        ImGui::Text("Tick Rate:  %.0f Hz", stats.tickRate);
        ImGui::Text("Frame Time: %.2f ms", m_timer.DeltaTime() * 1000.0f);
        ImGui::Text("FPS:        %d", m_timer.FPS());

        ImGui::Separator();

        // Bandwidth
        ImGui::Text("Bandwidth In:  %.2f KB/s", stats.bandwidthInKBps);
        ImGui::Text("Bandwidth Out: %.2f KB/s", stats.bandwidthOutKBps);

        ImGui::Separator();

        // Connected players
        ImGui::Text("Players: %d", stats.connectedPlayers);

        if (m_networkManager.IsOnline() && stats.connectedPlayers > 0) {
            auto& transport = m_networkManager.GetTransport();
            auto connections = transport.GetConnections();
            for (auto cid : connections) {
                auto info = transport.GetConnectionInfo(cid);
                ImGui::BulletText("[%u] %s  RTT: %.0fms  %.1fKB sent",
                                  cid, info.address.c_str(), info.rttMs,
                                  static_cast<float>(info.bytesSent) / 1024.0f);
            }
        }

        ImGui::Separator();

        // Scene stats
        ImGui::Text("Entities: %d", m_editorState.scene.GetEntityCount());

        ImGui::Separator();

        // Push level button
        if (ImGui::Button("Push Level to Clients", ImVec2(-1, 0))) {
            m_networkManager.BroadcastLevelData(m_editorState.scene);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Send current scene to all connected clients (Ctrl+P)");

        ImGui::Separator();
        ImGui::TextDisabled("Ctrl+P = Push level | Ctrl+L = Stop server | Alt+O = New instance");
    }
    ImGui::End();
}

// ==================== Client Overlay ====================

void Application::DrawClientOverlay() {
    auto stats = m_networkManager.GetStats();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(10.0f, static_cast<float>(m_height) - 10.0f),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);

    if (ImGui::Begin("##ClientOverlay", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                           m_networkManager.serverName.c_str());
        ImGui::SameLine();
        ImGui::Text("| Players: %d | Ping: %.0fms",
                    stats.connectedPlayers, stats.avgRttMs);
    }
    ImGui::End();
}

// ==================== Shutdown ====================

void Application::Shutdown() {
    m_input.SetCursorLocked(false);
    m_gpuParticles.Shutdown();
    m_networkManager.Shutdown();
    m_hud.Shutdown();
    m_weaponSystem.Shutdown();
    m_physicsWorld.Shutdown();
    m_fractureSystem.Shutdown();
    m_debrisManager.Shutdown();
    m_aiSystem.Shutdown();
    m_navGrid.Shutdown();
    ResourceManager::Get().Shutdown();
    m_levelEditor.Shutdown();
    m_editorUI.Shutdown();
    m_debugRenderer.Shutdown();
    m_postProcess.Shutdown();
    m_ssao.Shutdown();
    m_fsrUpscaler.Shutdown();
    m_shadowMap.Shutdown();
    m_defaultWhite.Release();
    m_groundMesh.Release();
    m_rotorMesh.Release();
    m_sphereMesh.Release();
    m_cubeMesh.Release();
    m_renderer.Shutdown();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    LOG_INFO("Application shutdown");
}

} // namespace WT
