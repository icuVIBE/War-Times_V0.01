#include "LevelEditorWindow.h"
#include "EditorPanels.h"      // for EditorState
#include "LevelFile.h"
#include "PCG/LevelGenerator.h"
#include "Core/ResourceManager.h"
#include "Util/Log.h"
#include "Util/MathHelpers.h"
#include <windowsx.h>   // GET_X_LPARAM, GET_Y_LPARAM
#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---- Design System palette ----
static constexpr ImVec4 kAccent     = {0.35f, 0.58f, 1.00f, 1.0f};
static constexpr ImVec4 kAccentDim  = {0.26f, 0.42f, 0.72f, 1.0f};
static constexpr ImVec4 kTextDim    = {0.52f, 0.55f, 0.62f, 1.0f};
static constexpr ImVec4 kTextMuted  = {0.38f, 0.40f, 0.46f, 1.0f};
static constexpr ImVec4 kPanelBg    = {0.078f, 0.082f, 0.098f, 0.97f};
static constexpr ImVec4 kTabActive  = {0.18f, 0.22f, 0.32f, 1.0f};
static constexpr ImVec4 kTabHover   = {0.15f, 0.18f, 0.26f, 1.0f};
static constexpr ImVec4 kTabInact   = {0.09f, 0.10f, 0.12f, 1.0f};

// Axis colors
static constexpr ImVec4 kAxisX = {0.90f, 0.22f, 0.22f, 1.0f};
static constexpr ImVec4 kAxisY = {0.22f, 0.82f, 0.22f, 1.0f};
static constexpr ImVec4 kAxisZ = {0.22f, 0.42f, 0.90f, 1.0f};

namespace WT {

// ==================== Static WndProc ====================
static LevelEditorWindow* g_levelEditor = nullptr;

LRESULT CALLBACK LevelEditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_levelEditor)
        return g_levelEditor->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT LevelEditorWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Forward to ImGui first
    if (m_imguiReady) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
            ImGui::SetCurrentContext(prevCtx);
            return 0;
        }
        // Check if ImGui wants the mouse â€” suppress 3D viewport interaction
        m_imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;

        // Fix stale WantCaptureMouse: if mouse is clearly in viewport area
        // (right of panel), override to allow 3D interaction immediately.
        if (m_imguiWantsMouse) {
            int curX = m_mouseX;
            if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                msg == WM_MBUTTONDOWN || msg == WM_MOUSEWHEEL)
                curX = GET_X_LPARAM(lParam);
            float panelRight = m_panelVisible ? m_panelWidth : 28.0f;
            if (curX >= (int)panelRight)
                m_imguiWantsMouse = false;
        }
        ImGui::SetCurrentContext(prevCtx);
    }

    switch (msg) {
        case WM_CLOSE:
            ShowWindow(m_hwnd, SW_HIDE);
            m_open = false;
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 0 && h > 0 && m_swapChain) {
                m_width  = w;
                m_height = h;
                m_rtv.Reset();
                m_dsv.Reset();
                m_backBuffer.Reset();
                m_depthBuffer.Reset();
                m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTargets();
            }
            return 0;
        }

        // ---- Mouse ----
        case WM_LBUTTONDOWN:
            if (!m_imguiWantsMouse) {
                m_leftDragging = true;
                m_lastMouse.x = GET_X_LPARAM(lParam);
                m_lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;

        case WM_LBUTTONUP:
            m_leftDragging = false;
            m_isDragging = false;
            m_activeAxis = -1;
            if (m_autoSwitchedTool) {
                m_currentTool = m_toolBeforeGizmoDrag;
                m_axisConstraint = AxisConstraint::None;
                m_autoSwitchedTool = false;
            }
            ReleaseCapture();
            return 0;

        case WM_MBUTTONDOWN:
            if (!m_imguiWantsMouse) {
                m_orbiting = true;
                m_lastMouse.x = GET_X_LPARAM(lParam);
                m_lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;

        case WM_MBUTTONUP:
            m_orbiting = false;
            ReleaseCapture();
            return 0;

        case WM_RBUTTONDOWN:
            if (!m_imguiWantsMouse) {
                m_rightDragging = true;
                m_lastMouse.x = GET_X_LPARAM(lParam);
                m_lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;

        case WM_RBUTTONUP:
            m_rightDragging = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            float dx = static_cast<float>(mx - m_lastMouse.x);
            float dy = static_cast<float>(my - m_lastMouse.y);
            m_mouseX = mx;
            m_mouseY = my;

            if (m_orbiting || m_rightDragging) {
                m_camYaw   += dx * 0.005f;
                m_camPitch += dy * 0.005f;
                m_camPitch = Clamp(m_camPitch, -HALF_PI + 0.05f, HALF_PI - 0.05f);
            }
            m_lastMouse.x = mx;
            m_lastMouse.y = my;
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (!m_imguiWantsMouse) {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                float amount = (delta > 0) ? 2.0f : -2.0f;
                if (m_keyShift) amount *= 3.0f;
                float cosP = cosf(m_camPitch);
                m_camX += cosP * sinf(m_camYaw) * amount;
                m_camY += -sinf(m_camPitch) * amount;
                m_camZ += cosP * cosf(m_camYaw) * amount;
            }
            return 0;
        }

        // ---- Keyboard ----
        case WM_KEYDOWN:
            switch (wParam) {
                case 'W': m_keyW = true; break;
                case 'A': m_keyA = true; break;
                case 'S': m_keyS = true; break;
                case 'D': m_keyD = true; break;
                case VK_SPACE:   m_keySpace = true; break;
                case VK_CONTROL: m_keyCtrl = true; break;
                case VK_SHIFT:   m_keyShift = true; break;
                // Tool hotkeys
                case 'Q': m_currentTool = LevelEditTool::Select; m_axisConstraint = AxisConstraint::None; break;
                case 'G': m_currentTool = LevelEditTool::Move;   m_axisConstraint = AxisConstraint::XZ; break;
                case 'R': m_currentTool = LevelEditTool::Rotate; m_axisConstraint = AxisConstraint::Y; break;
                case 'T': m_currentTool = LevelEditTool::Scale;  m_axisConstraint = AxisConstraint::None; break;
                case 'P': m_currentTool = LevelEditTool::Place;  m_axisConstraint = AxisConstraint::None; break;
                case VK_OEM_3: m_gridSnap = !m_gridSnap; break;
                // Axis constraints (while tool is active)
                case 'X': if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Scale || m_currentTool == LevelEditTool::Rotate) {
                              m_axisConstraint = (m_axisConstraint == AxisConstraint::X) ? AxisConstraint::None : AxisConstraint::X;
                          } break;
                case 'Y': if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Scale || m_currentTool == LevelEditTool::Rotate) {
                              m_axisConstraint = (m_axisConstraint == AxisConstraint::Y) ? AxisConstraint::None : AxisConstraint::Y;
                          } break;
                case 'Z': if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Scale || m_currentTool == LevelEditTool::Rotate) {
                              m_axisConstraint = (m_axisConstraint == AxisConstraint::Z) ? AxisConstraint::None : AxisConstraint::Z;
                          } break;
                case 'F': m_focusKeyPressed = true; break;
                case VK_TAB: m_panelVisible = !m_panelVisible; break;
                case VK_DELETE: m_deleteKeyPressed = true; break;
                case VK_ESCAPE:
                    if (m_isDragging) { m_isDragging = false; m_leftDragging = false; m_activeAxis = -1; }
                    break;
            }
            return 0;

        case WM_KEYUP:
            switch (wParam) {
                case 'W': m_keyW = false; break;
                case 'A': m_keyA = false; break;
                case 'S': m_keyS = false; break;
                case 'D': m_keyD = false; break;
                case VK_SPACE:   m_keySpace = false; break;
                case VK_CONTROL: m_keyCtrl = false; break;
                case VK_SHIFT:   m_keyShift = false; break;
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ==================== Init ====================
bool LevelEditorWindow::Init(ID3D11Device* sharedDevice, HINSTANCE hInstance, int width, int height) {
    m_device = sharedDevice;
    m_hInst  = hInstance;
    g_levelEditor = this;

    if (!CreateEditorWindow(hInstance, width, height)) return false;
    if (!CreateSwapChain())  return false;
    if (!CreateRenderTargets()) return false;

    // Init debug renderer for grid/selection lines
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/") + 1);
    m_debugRenderer.Init(m_device, exeDir + L"shaders/");

    // Levels directory â€” try source tree first (../../levels/ from exe),
    // fallback to exe-relative levels/
    char exeDirBuf[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, exeDir.c_str(), -1, exeDirBuf, MAX_PATH, nullptr, nullptr);
    std::string exeDirStr(exeDirBuf);

    // Try source directory first (works when running from build/bin/)
    std::filesystem::path srcLevels = std::filesystem::path(exeDirStr) / ".." / ".." / "levels";
    std::filesystem::path exeLevels = std::filesystem::path(exeDirStr) / "levels";

    try {
        srcLevels = std::filesystem::weakly_canonical(srcLevels);
    } catch (...) {}

    if (std::filesystem::exists(srcLevels) && std::filesystem::is_directory(srcLevels)) {
        m_levelsDirectory = srcLevels.string() + "\\";
        LOG_INFO("Levels directory (source): %s", m_levelsDirectory.c_str());
    } else if (std::filesystem::exists(exeLevels) && std::filesystem::is_directory(exeLevels)) {
        m_levelsDirectory = exeLevels.string() + "\\";
        LOG_INFO("Levels directory (exe-local): %s", m_levelsDirectory.c_str());
    } else {
        // Create in exe directory as last resort
        m_levelsDirectory = exeLevels.string() + "\\";
        std::filesystem::create_directories(exeLevels);
        LOG_WARN("Levels directory created: %s", m_levelsDirectory.c_str());
    }

    // Init ImGui for this window
    if (!InitImGui()) {
        LOG_ERROR("Failed to init ImGui for Level Editor window");
    }

    m_open = false;
    ShowWindow(m_hwnd, SW_HIDE);

    LOG_INFO("Level Editor window created (%dx%d) with ImGui panel", width, height);
    return true;
}

bool LevelEditorWindow::CreateEditorWindow(HINSTANCE hInstance, int width, int height) {
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground  = nullptr;
    wc.lpszClassName  = L"WT_LevelEditorClass";
    wc.hIcon          = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm        = LoadIcon(nullptr, IDI_APPLICATION);

    RegisterClassEx(&wc);

    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    m_hwnd = CreateWindowEx(
        0, L"WT_LevelEditorClass",
        L"War Times \u2014 Level Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create Level Editor window");
        return false;
    }

    m_width  = width;
    m_height = height;
    return true;
}

bool LevelEditorWindow::CreateSwapChain() {
    ComPtr<IDXGIDevice> dxgiDevice;
    m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount  = 2;
    sd.BufferDesc.Width  = m_width;
    sd.BufferDesc.Height = m_height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc   = { 1, 0 };
    sd.Windowed     = TRUE;
    sd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
        if (FAILED(hr)) {
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            sd.BufferCount = 1;
            hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
        }
    }

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Level Editor swap chain");
        return false;
    }
    return true;
}

bool LevelEditorWindow::CreateRenderTargets() {
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer));
    if (FAILED(hr)) return false;
    hr = m_device->CreateRenderTargetView(m_backBuffer.Get(), nullptr, &m_rtv);
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = m_width; dd.Height = m_height;
    dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc = { 1, 0 };
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_device->CreateTexture2D(&dd, nullptr, &m_depthBuffer);
    if (FAILED(hr)) return false;
    hr = m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, &m_dsv);
    return SUCCEEDED(hr);
}

// ==================== ImGui Context ====================
bool LevelEditorWindow::InitImGui() {
    // Save the main context
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();

    // Create a new ImGui context for this window
    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style â€” match main editor
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(8, 6);
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(6, 3);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 14.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 2.0f;

    // Colors â€” match main editor dark theme
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]           = ImVec4(0.09f, 0.09f, 0.11f, 0.97f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.10f, 0.13f, 0.98f);
    c[ImGuiCol_Border]             = ImVec4(0.18f, 0.20f, 0.26f, 0.65f);
    c[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
    c[ImGuiCol_TitleBg]            = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.06f, 0.06f, 0.08f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.30f, 0.33f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.35f, 0.38f, 0.48f, 1.00f);
    c[ImGuiCol_CheckMark]          = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]         = ImVec4(0.35f, 0.50f, 0.80f, 1.00f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.45f, 0.62f, 0.95f, 1.00f);
    c[ImGuiCol_Button]             = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);
    c[ImGuiCol_Header]             = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.18f, 0.22f, 0.32f, 1.00f);
    c[ImGuiCol_Separator]          = ImVec4(0.18f, 0.20f, 0.26f, 0.50f);
    c[ImGuiCol_Tab]                = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.25f, 0.32f, 0.48f, 0.80f);
    c[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.24f, 0.38f, 1.00f);
    c[ImGuiCol_TextSelectedBg]     = ImVec4(0.25f, 0.40f, 0.65f, 0.35f);

    // Get the device context from the shared device
    ID3D11DeviceContext* ctx = nullptr;
    m_device->GetImmediateContext(&ctx);

    if (!ImGui_ImplWin32_Init(m_hwnd)) {
        LOG_ERROR("ImGui_ImplWin32_Init failed for Level Editor");
        ImGui::SetCurrentContext(mainCtx);
        return false;
    }
    ImGui_ImplDX11_Init(m_device, ctx);

    if (ctx) ctx->Release();

    m_imguiReady = true;

    // Restore main context
    ImGui::SetCurrentContext(mainCtx);

    LOG_INFO("Level Editor ImGui context initialized");
    return true;
}

void LevelEditorWindow::ShutdownImGui() {
    if (!m_imguiReady) return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::SetCurrentContext(prevCtx);
    ImGui::DestroyContext(m_imguiCtx);
    m_imguiCtx = nullptr;
    m_imguiReady = false;

    LOG_INFO("Level Editor ImGui context shutdown");
}

// ==================== Open/Close ====================
void LevelEditorWindow::SetOpen(bool open) {
    m_open = open;
    ShowWindow(m_hwnd, open ? SW_SHOW : SW_HIDE);
    if (open) SetForegroundWindow(m_hwnd);
}

// ==================== Camera ====================
XMMATRIX LevelEditorWindow::GetViewMatrix() const {
    float cosP = cosf(m_camPitch);
    XMFLOAT3 fwd = { cosP * sinf(m_camYaw), -sinf(m_camPitch), cosP * cosf(m_camYaw) };
    XMVECTOR eye    = XMVectorSet(m_camX, m_camY, m_camZ, 0);
    XMVECTOR target = eye + XMLoadFloat3(&fwd);
    return XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0));
}

XMMATRIX LevelEditorWindow::GetProjectionMatrix() const {
    float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_camFOV), aspect, 0.1f, 500.0f);
}

void LevelEditorWindow::HandleCameraInput(float dt) {
    if (!m_rightDragging && !m_orbiting) return;

    float speed = m_camSpeed * dt;
    if (m_keyShift) speed *= 3.0f;

    float cosP = cosf(m_camPitch);
    XMFLOAT3 fwd = { cosP * sinf(m_camYaw), 0, cosP * cosf(m_camYaw) };
    float len = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    if (len > 0.001f) { fwd.x /= len; fwd.z /= len; }
    XMFLOAT3 right = { cosf(m_camYaw), 0, -sinf(m_camYaw) };

    if (m_keyW) { m_camX += fwd.x * speed; m_camZ += fwd.z * speed; }
    if (m_keyS) { m_camX -= fwd.x * speed; m_camZ -= fwd.z * speed; }
    if (m_keyA) { m_camX -= right.x * speed; m_camZ -= right.z * speed; }
    if (m_keyD) { m_camX += right.x * speed; m_camZ += right.z * speed; }
    if (m_keySpace) m_camY += speed;
    if (m_keyCtrl)  m_camY -= speed;
}

// ==================== Screen-to-World ====================
XMFLOAT3 LevelEditorWindow::ScreenToWorldPlane(int mx, int my, float planeY) {
    XMMATRIX invVP = XMMatrixInverse(nullptr, GetViewMatrix() * GetProjectionMatrix());
    float ndcX = (2.0f * mx / m_width) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_height);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), invVP);
    XMVECTOR rayDir = XMVector3Normalize(farPt - nearPt);

    XMFLOAT3 o, d;
    XMStoreFloat3(&o, nearPt);
    XMStoreFloat3(&d, rayDir);

    if (fabsf(d.y) < 0.0001f) return { o.x, planeY, o.z };
    float t = (planeY - o.y) / d.y;
    return { o.x + d.x * t, planeY, o.z + d.z * t };
}

// Project screen point onto an axis line through origin for constrained movement
XMFLOAT3 LevelEditorWindow::ScreenToWorldAxis(int mx, int my, XMFLOAT3 origin, AxisConstraint axis) {
    XMMATRIX invVP = XMMatrixInverse(nullptr, GetViewMatrix() * GetProjectionMatrix());
    float ndcX = (2.0f * mx / m_width) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_height);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), invVP);
    XMVECTOR rayO   = nearPt;
    XMVECTOR rayD   = XMVector3Normalize(farPt - nearPt);

    // Axis direction
    XMVECTOR axisDir;
    switch (axis) {
        case AxisConstraint::X:  axisDir = XMVectorSet(1, 0, 0, 0); break;
        case AxisConstraint::Y:  axisDir = XMVectorSet(0, 1, 0, 0); break;
        case AxisConstraint::Z:  axisDir = XMVectorSet(0, 0, 1, 0); break;
        default:                 return ScreenToWorldPlane(mx, my, origin.y);
    }

    XMVECTOR lineO = XMLoadFloat3(&origin);

    // Closest point on axis line to the ray (parametric)
    XMVECTOR w0 = lineO - rayO;
    float a = XMVectorGetX(XMVector3Dot(axisDir, axisDir));
    float b = XMVectorGetX(XMVector3Dot(axisDir, rayD));
    float c2 = XMVectorGetX(XMVector3Dot(rayD, rayD));
    float d1 = XMVectorGetX(XMVector3Dot(axisDir, w0));
    float e1 = XMVectorGetX(XMVector3Dot(rayD, w0));

    float denom = a * c2 - b * b;
    float sc = (fabsf(denom) > 0.0001f) ? (b * e1 - c2 * d1) / denom : 0.0f;

    XMVECTOR pt = lineO + axisDir * sc;
    XMFLOAT3 result;
    XMStoreFloat3(&result, pt);
    return result;
}

// ==================== Picking ====================
int LevelEditorWindow::PickEntity(const EditorState& state, int mx, int my) {
    XMMATRIX invVP = XMMatrixInverse(nullptr, GetViewMatrix() * GetProjectionMatrix());
    float ndcX = (2.0f * mx / m_width) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_height);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), invVP);
    XMFLOAT3 origin, dir;
    XMStoreFloat3(&origin, nearPt);
    XMStoreFloat3(&dir, XMVector3Normalize(farPt - nearPt));

    int closest = -1;
    float closestDist = 999999.0f;

    for (int i = 0; i < state.scene.GetEntityCount(); i++) {
        const auto& e = state.scene.GetEntity(i);
        if (!e.visible) continue;

        // Use mesh bounds for custom meshes, entity scale for cubes
        XMFLOAT3 half = { e.scale[0] * 0.5f, e.scale[1] * 0.5f, e.scale[2] * 0.5f };
        XMFLOAT3 center = { e.position[0], e.position[1], e.position[2] };
        if (e.meshType == MeshType::Custom && !e.meshName.empty()) {
            Mesh* mesh = ResourceManager::Get().GetMesh(e.meshName);
            if (mesh && mesh->HasBounds()) {
                auto bc = mesh->GetBoundsCenter();
                auto bh = mesh->GetBoundsHalfExtent();
                // Bounds center/half are in local space, scale them
                half = { bh.x * e.scale[0], bh.y * e.scale[1], bh.z * e.scale[2] };
                // Rotate local bounds center offset into world space
                XMMATRIX R = XMMatrixRotationRollPitchYaw(
                    XMConvertToRadians(e.rotation[0]),
                    XMConvertToRadians(e.rotation[1]),
                    XMConvertToRadians(e.rotation[2]));
                XMVECTOR off = XMVectorSet(bc.x * e.scale[0], bc.y * e.scale[1], bc.z * e.scale[2], 0);
                off = XMVector3TransformNormal(off, R);
                XMFLOAT3 rOff;
                XMStoreFloat3(&rOff, off);
                center = { e.position[0] + rOff.x, e.position[1] + rOff.y, e.position[2] + rOff.z };
            }
        }

        // Build inverse rotation to transform ray into entity local space (OBB test)
        XMMATRIX invR = XMMatrixTranspose(XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(e.rotation[0]),
            XMConvertToRadians(e.rotation[1]),
            XMConvertToRadians(e.rotation[2])));

        // Transform ray origin and direction into local space
        XMVECTOR localOriginV = XMVector3TransformCoord(
            XMVectorSet(origin.x - center.x, origin.y - center.y, origin.z - center.z, 1), invR);
        XMVECTOR localDirV = XMVector3TransformNormal(
            XMVectorSet(dir.x, dir.y, dir.z, 0), invR);
        XMFLOAT3 localOrigin, localDir;
        XMStoreFloat3(&localOrigin, localOriginV);
        XMStoreFloat3(&localDir, localDirV);

        // Standard AABB slab test in local space
        float tmin = -999999.0f, tmax = 999999.0f;
        auto slab = [&](float rO, float rD, float halfE) -> bool {
            if (fabsf(rD) < 0.00001f) return (rO >= -halfE && rO <= halfE);
            float t1 = (-halfE - rO) / rD, t2 = (halfE - rO) / rD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
            return tmin <= tmax && tmax >= 0;
        };
        if (!slab(localOrigin.x, localDir.x, half.x)) continue;
        if (!slab(localOrigin.y, localDir.y, half.y)) continue;
        if (!slab(localOrigin.z, localDir.z, half.z)) continue;

        float dist = (tmin >= 0) ? tmin : tmax;
        if (dist < closestDist) { closestDist = dist; closest = i; }
    }
    return closest;
}

// Pick which gizmo axis the mouse is near (returns 0=X, 1=Y, 2=Z, -1=none)
int LevelEditorWindow::PickGizmoAxis(const EditorState& state, int mx, int my) {
    if (state.selectedEntity < 0 || state.selectedEntity >= state.scene.GetEntityCount())
        return -1;

    const auto& e = state.scene.GetEntity(state.selectedEntity);
    XMFLOAT3 pos = { e.position[0], e.position[1], e.position[2] };

    // Offset by mesh bounds center for custom meshes (rotated into world space)
    if (e.meshType == MeshType::Custom) {
        auto mesh = ResourceManager::Get().GetMesh(e.meshName);
        if (mesh && mesh->HasBounds()) {
            auto bc = mesh->GetBoundsCenter();
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(e.rotation[0]),
                XMConvertToRadians(e.rotation[1]),
                XMConvertToRadians(e.rotation[2]));
            XMVECTOR off = XMVectorSet(bc.x * e.scale[0], bc.y * e.scale[1], bc.z * e.scale[2], 0);
            off = XMVector3TransformNormal(off, R);
            XMFLOAT3 rOff;
            XMStoreFloat3(&rOff, off);
            pos.x += rOff.x;
            pos.y += rOff.y;
            pos.z += rOff.z;
        }
    }

    // Distance-based gizmo scaling (consistent screen size)
    float dist = sqrtf((pos.x - m_camX) * (pos.x - m_camX) +
                       (pos.y - m_camY) * (pos.y - m_camY) +
                       (pos.z - m_camZ) * (pos.z - m_camZ));
    float gl = m_gizmoLength * (std::max)(dist * 0.07f, 0.5f);

    XMMATRIX vp = GetViewMatrix() * GetProjectionMatrix();

    // Project entity origin and axis endpoints to screen space
    auto projectToScreen = [&](XMFLOAT3 worldPt) -> XMFLOAT2 {
        XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&worldPt), vp);
        XMFLOAT3 ndc;
        XMStoreFloat3(&ndc, p);
        return { (ndc.x * 0.5f + 0.5f) * m_width, (0.5f - ndc.y * 0.5f) * m_height };
    };

    XMFLOAT2 screenOrigin = projectToScreen(pos);
    XMFLOAT3 axisEnds[3] = {
        { pos.x + gl, pos.y, pos.z },
        { pos.x, pos.y + gl, pos.z },
        { pos.x, pos.y, pos.z + gl }
    };

    float hitDist = 15.0f;  // pixels (increased for easier picking)
    int bestAxis = -1;
    float bestDist = hitDist;

    for (int a = 0; a < 3; a++) {
        XMFLOAT2 screenEnd = projectToScreen(axisEnds[a]);

        // Distance from point to line segment
        float dx = screenEnd.x - screenOrigin.x;
        float dy = screenEnd.y - screenOrigin.y;
        float segLen2 = dx * dx + dy * dy;
        if (segLen2 < 1.0f) continue;

        float t = ((mx - screenOrigin.x) * dx + (my - screenOrigin.y) * dy) / segLen2;
        t = Clamp(t, 0.0f, 1.0f);

        float px = screenOrigin.x + t * dx - mx;
        float py = screenOrigin.y + t * dy - my;
        float d = sqrtf(px * px + py * py);

        if (d < bestDist) {
            bestDist = d;
            bestAxis = a;
        }
    }
    return bestAxis;
}

// ==================== Tool Input ====================
void LevelEditorWindow::HandleToolInput(EditorState& state) {
    if (m_imguiWantsMouse) return;
    if (!m_leftDragging) return;

    switch (m_currentTool) {
        case LevelEditTool::Select: {
            if (!m_isDragging) {
                // If clicking on a gizmo axis, auto-switch to Move tool
                if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_toolBeforeGizmoDrag = m_currentTool;
                        m_autoSwitchedTool = true;
                        m_currentTool = LevelEditTool::Move;
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                        auto& e = state.scene.GetEntity(state.selectedEntity);
                        m_dragEntityOrigPos = { e.position[0], e.position[1], e.position[2] };
                        m_dragStart = ScreenToWorldAxis(m_mouseX, m_mouseY, m_dragEntityOrigPos, m_axisConstraint);
                        m_isDragging = true;
                        break;
                    }
                }
                state.selectedEntity = PickEntity(state, m_mouseX, m_mouseY);
                m_isDragging = true;
            }
            break;
        }
        case LevelEditTool::Move: {
            if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                auto& e = state.scene.GetEntity(state.selectedEntity);

                if (!m_isDragging) {
                    // Check if clicking on a gizmo axis handle
                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                    }

                    m_dragEntityOrigPos = { e.position[0], e.position[1], e.position[2] };

                    if (m_axisConstraint == AxisConstraint::X || m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::Z) {
                        m_dragStart = ScreenToWorldAxis(m_mouseX, m_mouseY, m_dragEntityOrigPos, m_axisConstraint);
                    } else {
                        m_dragStart = ScreenToWorldPlane(m_mouseX, m_mouseY, e.position[1]);
                    }
                    m_isDragging = true;
                }

                XMFLOAT3 wp;
                if (m_axisConstraint == AxisConstraint::X || m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::Z) {
                    wp = ScreenToWorldAxis(m_mouseX, m_mouseY, m_dragEntityOrigPos, m_axisConstraint);
                } else {
                    wp = ScreenToWorldPlane(m_mouseX, m_mouseY, e.position[1]);
                }

                float deltaX = wp.x - m_dragStart.x;
                float deltaY = wp.y - m_dragStart.y;
                float deltaZ = wp.z - m_dragStart.z;

                // Fine control
                if (m_keyCtrl) { deltaX *= 0.1f; deltaY *= 0.1f; deltaZ *= 0.1f; }

                float nx = m_dragEntityOrigPos.x;
                float ny = m_dragEntityOrigPos.y;
                float nz = m_dragEntityOrigPos.z;

                switch (m_axisConstraint) {
                    case AxisConstraint::X:  nx += deltaX; break;
                    case AxisConstraint::Y:  ny += deltaY; break;
                    case AxisConstraint::Z:  nz += deltaZ; break;
                    case AxisConstraint::XZ: nx += deltaX; nz += deltaZ; break;
                    default:                 nx += deltaX; ny += deltaY; nz += deltaZ; break;
                }

                if (m_gridSnap) {
                    if (m_axisConstraint != AxisConstraint::Y) {
                        nx = roundf(nx / m_gridSnapSize) * m_gridSnapSize;
                        nz = roundf(nz / m_gridSnapSize) * m_gridSnapSize;
                    }
                    if (m_axisConstraint == AxisConstraint::Y) {
                        ny = roundf(ny / m_gridSnapSize) * m_gridSnapSize;
                    }
                }

                e.position[0] = nx;
                e.position[1] = ny;
                e.position[2] = nz;
                m_unsavedChanges = true;
            }
            break;
        }
        case LevelEditTool::Rotate: {
            if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                auto& e = state.scene.GetEntity(state.selectedEntity);
                if (!m_isDragging) {
                    m_dragStart = { (float)m_mouseX, (float)m_mouseY, 0 };
                    m_dragEntityOrigRot[0] = e.rotation[0];
                    m_dragEntityOrigRot[1] = e.rotation[1];
                    m_dragEntityOrigRot[2] = e.rotation[2];

                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                    }
                    m_isDragging = true;
                }

                float mouseDelta = (float)m_mouseX - m_dragStart.x;
                float rotDelta = mouseDelta * 0.5f;

                if (m_rotationSnapOn) {
                    rotDelta = roundf(rotDelta / m_rotationSnap) * m_rotationSnap;
                }
                if (m_keyCtrl) rotDelta *= 0.1f;

                switch (m_axisConstraint) {
                    case AxisConstraint::X:
                        e.rotation[0] = m_dragEntityOrigRot[0] + rotDelta;
                        break;
                    case AxisConstraint::Z:
                        e.rotation[2] = m_dragEntityOrigRot[2] + rotDelta;
                        break;
                    case AxisConstraint::Y:
                    default:
                        e.rotation[1] = m_dragEntityOrigRot[1] + rotDelta;
                        break;
                }
                m_unsavedChanges = true;
            }
            break;
        }
        case LevelEditTool::Scale: {
            if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                auto& e = state.scene.GetEntity(state.selectedEntity);
                if (!m_isDragging) {
                    m_dragStart = { (float)m_mouseX, (float)m_mouseY, 0 };
                    memcpy(m_dragEntityOrigScale, e.scale, sizeof(float) * 3);

                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                    }
                    m_isDragging = true;
                }

                float rawFactor = 1.0f + (m_dragStart.y - (float)m_mouseY) * 0.005f;
                if (rawFactor < 0.05f) rawFactor = 0.05f;
                if (m_keyCtrl) rawFactor = 1.0f + (rawFactor - 1.0f) * 0.2f;

                float factor = rawFactor;
                if (m_scaleSnapOn) {
                    factor = roundf(rawFactor / m_scaleSnap) * m_scaleSnap;
                    if (factor < m_scaleSnap) factor = m_scaleSnap;
                }

                if (m_uniformScale && m_axisConstraint == AxisConstraint::None) {
                    e.scale[0] = m_dragEntityOrigScale[0] * factor;
                    e.scale[1] = m_dragEntityOrigScale[1] * factor;
                    e.scale[2] = m_dragEntityOrigScale[2] * factor;
                } else {
                    switch (m_axisConstraint) {
                        case AxisConstraint::X:
                            e.scale[0] = m_dragEntityOrigScale[0] * factor;
                            e.scale[1] = m_dragEntityOrigScale[1];
                            e.scale[2] = m_dragEntityOrigScale[2];
                            break;
                        case AxisConstraint::Y:
                            e.scale[0] = m_dragEntityOrigScale[0];
                            e.scale[1] = m_dragEntityOrigScale[1] * factor;
                            e.scale[2] = m_dragEntityOrigScale[2];
                            break;
                        case AxisConstraint::Z:
                            e.scale[0] = m_dragEntityOrigScale[0];
                            e.scale[1] = m_dragEntityOrigScale[1];
                            e.scale[2] = m_dragEntityOrigScale[2] * factor;
                            break;
                        default:
                            e.scale[0] = m_dragEntityOrigScale[0] * factor;
                            e.scale[1] = m_dragEntityOrigScale[1] * factor;
                            e.scale[2] = m_dragEntityOrigScale[2] * factor;
                            break;
                    }
                }
                m_unsavedChanges = true;
            }
            break;
        }
        case LevelEditTool::Place: {
            if (!m_isDragging) {
                XMFLOAT3 wp = ScreenToWorldPlane(m_mouseX, m_mouseY, 0.0f);
                if (m_gridSnap) {
                    wp.x = roundf(wp.x / m_gridSnapSize) * m_gridSnapSize;
                    wp.z = roundf(wp.z / m_gridSnapSize) * m_gridSnapSize;
                }
                int idx = state.scene.AddEntity("", m_placeMeshType);
                auto& e = state.scene.GetEntity(idx);
                if (m_placeMeshType == MeshType::Custom) {
                    e.meshName = m_placeMeshName;
                    e.name = m_placeMeshName + "_" + std::to_string(idx);
                }
                e.position[0] = wp.x;
                e.position[1] = 0.5f;
                e.position[2] = wp.z;
                memcpy(e.color, m_placeColor, sizeof(float) * 4);
                state.selectedEntity = idx;
                m_isDragging = true;
                m_unsavedChanges = true;
            }
            break;
        }
        default: break;
    }
}

// ==================== Update ====================
void LevelEditorWindow::Update(float dt, EditorState& state) {
    if (!m_open) return;
    HandleCameraInput(dt);
    HandleToolInput(state);

    if (!m_leftDragging && !m_imguiWantsMouse) {
        m_hoveredAxis = PickGizmoAxis(state, m_mouseX, m_mouseY);
        if (m_currentTool == LevelEditTool::Select || m_hoveredAxis < 0)
            m_hoveredEntity = PickEntity(state, m_mouseX, m_mouseY);
    }

    // Selection pulse animation
    m_selectionPulse += dt * 3.0f;
    if (m_selectionPulse > 6.283185f) m_selectionPulse -= 6.283185f;

    // Delete key (single-fire: flag set in WM_KEYDOWN, consumed here)
    if (m_deleteKeyPressed) {
        m_deleteKeyPressed = false;
        if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
            state.scene.RemoveEntity(state.selectedEntity);
            if (state.selectedEntity >= state.scene.GetEntityCount())
                state.selectedEntity = state.scene.GetEntityCount() - 1;
            m_unsavedChanges = true;
        }
    }

    // F key: focus camera on selected entity
    if (m_focusKeyPressed) {
        m_focusKeyPressed = false;
        FocusOnEntity(state);
    }
}

// ==================== Render ====================
void LevelEditorWindow::Render(ID3D11DeviceContext* ctx, EditorState& state) {
    if (!m_open || !m_swapChain) return;

    // ---- Bind our render target ----
    float cc[4] = { 0.12f, 0.13f, 0.16f, 1.0f };
    ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
    ctx->ClearRenderTargetView(m_rtv.Get(), cc);
    ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_width; vp.Height = (float)m_height; vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    // ---- Per-frame CB ----
    if (m_res.cbPerFrame) {
        XMMATRIX v = GetViewMatrix(), p = GetProjectionMatrix(), vpp = v * p;
        CBPerFrame f = {};
        XMStoreFloat4x4(&f.View, XMMatrixTranspose(v));
        XMStoreFloat4x4(&f.Projection, XMMatrixTranspose(p));
        XMStoreFloat4x4(&f.ViewProjection, XMMatrixTranspose(vpp));
        XMStoreFloat4x4(&f.InvViewProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, vpp)));
        f.CameraPosition = { m_camX, m_camY, m_camZ };
        f.ScreenSize = { (float)m_width, (float)m_height };
        f.NearZ = 0.1f; f.FarZ = 500.0f;
        m_res.cbPerFrame->Update(ctx, f);
        m_res.cbPerFrame->BindBoth(ctx, 0);
    }

    // ---- Lighting CB (simple editor light) ----
    if (m_res.cbLighting) {
        CBLighting L = {};
        L.SunDirection = { 0.4f, -0.7f, 0.5f };
        L.SunIntensity = 1.2f;
        L.SunColor = { 1, 0.95f, 0.9f };
        L.AmbientColor = { 0.25f, 0.28f, 0.32f };
        L.AmbientIntensity = 1.0f;
        L.FogDensity = 0; L.FogStart = 500; L.FogEnd = 500;
        m_res.cbLighting->Update(ctx, L);
        m_res.cbLighting->BindBoth(ctx, 2);
    }

    // ---- Grid ----
    RenderGrid(ctx);

    // ---- Entities ----
    RenderEntities(ctx, state);

    // ---- Selection + Gizmo ----
    RenderSelectionHighlight(ctx, state);
    RenderGizmo(ctx, state);

    // ---- Debug flush ----
    m_debugRenderer.Flush(ctx);

    // ---- ImGui Outliner Panel ----
    if (m_imguiReady) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

        DrawOutlinerPanel(state);

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update for next frame — this is the most accurate read point
        m_imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;

        ImGui::SetCurrentContext(prevCtx);
    }

    // ---- Title bar info ----
    RenderToolbar(ctx, state);

    m_swapChain->Present(1, 0);
}

// ==================== Render Sub-passes ====================

void LevelEditorWindow::RenderGrid(ID3D11DeviceContext* ctx) {
    (void)ctx;
    float ext = (float)m_gridExtent;
    XMFLOAT4 gc = { 0.3f, 0.3f, 0.35f, 1.0f };
    XMFLOAT4 oc = { 0.5f, 0.5f, 0.55f, 1.0f };

    for (float i = -ext; i <= ext; i += m_gridSize) {
        XMFLOAT4 c = (fabsf(i) < 0.001f) ? oc : gc;
        m_debugRenderer.DrawLine({ i, 0, -ext }, { i, 0, ext }, c);
        m_debugRenderer.DrawLine({ -ext, 0, i }, { ext, 0, i }, c);
    }
    // Axis
    m_debugRenderer.DrawLine({ 0, 0.01f, 0 }, { ext * 0.3f, 0.01f, 0 }, { 0.8f, 0.2f, 0.2f, 1 });
    m_debugRenderer.DrawLine({ 0, 0.01f, 0 }, { 0, 0.01f, ext * 0.3f }, { 0.2f, 0.2f, 0.8f, 1 });
    m_debugRenderer.DrawLine({ 0, 0, 0 }, { 0, ext * 0.15f, 0 }, { 0.2f, 0.8f, 0.2f, 1 });
}

void LevelEditorWindow::RenderEntities(ID3D11DeviceContext* ctx, const EditorState& state) {
    if (!m_res.voxelShader || !m_res.cubeMesh || !m_res.cbPerObject) return;

    m_res.voxelShader->Bind(ctx);
    Texture* wt = ResourceManager::Get().GetTexture("_white");
    CBPerObject obj = {};

    for (int i = 0; i < state.scene.GetEntityCount(); i++) {
        const auto& e = state.scene.GetEntity(i);
        if (!e.visible) continue;

        XMMATRIX w = e.GetWorldMatrix();
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(w));
        XMStoreFloat4x4(&obj.WorldInvTranspose, XMMatrixInverse(nullptr, w));
        // Custom meshes: alpha=0 tells shader to use mesh UVs + vertex colors
        float colorAlpha = (e.meshType == MeshType::Custom) ? 0.0f : e.color[3];
        obj.ObjectColor = { e.color[0], e.color[1], e.color[2], colorAlpha };
        m_res.cbPerObject->Update(ctx, obj);
        m_res.cbPerObject->BindBoth(ctx, 1);

        if (e.meshType == MeshType::Cube) {
            Texture* ct = nullptr;
            bool ignorePlaceholderCubeTexture =
                e.textureName == "Walls/texture" ||
                e.textureName == "Floors/texture" ||
                e.textureName == "Props/texture";
            if (!e.textureName.empty() && !ignorePlaceholderCubeTexture)
                ct = ResourceManager::Get().GetTexture(e.textureName);
            (ct ? ct : wt)->Bind(ctx, 1);
            m_res.cubeMesh->Draw(ctx);
        } else if (e.meshType == MeshType::Custom) {
            Texture* mt = nullptr;
            if (!e.textureName.empty())
                mt = ResourceManager::Get().GetTexture(e.textureName);
            if (!mt)
                mt = ResourceManager::Get().GetTexture(e.meshName);
            (mt ? mt : wt)->Bind(ctx, 1);
            Mesh* cm = ResourceManager::Get().GetMesh(e.meshName);
            if (cm) cm->Draw(ctx);
        }
    }

    obj.ObjectColor = { 0, 0, 0, 0 };
    m_res.cbPerObject->Update(ctx, obj);
    m_res.cbPerObject->BindBoth(ctx, 1);

    // Ground
    if (m_res.groundShader && m_res.groundMesh) {
        m_res.groundShader->Bind(ctx);
        if (wt) wt->Bind(ctx, 1);
        XMMATRIX gw = XMMatrixTranslation(0, -0.01f, 0);
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(gw));
        XMStoreFloat4x4(&obj.WorldInvTranspose, XMMatrixInverse(nullptr, gw));
        m_res.cbPerObject->Update(ctx, obj);
        m_res.cbPerObject->BindBoth(ctx, 1);
        m_res.groundMesh->Draw(ctx);
    }
}

void LevelEditorWindow::RenderSelectionHighlight(ID3D11DeviceContext* ctx, const EditorState& state) {
    (void)ctx;

    // Helper: build rotation matrix from entity Euler angles
    auto entityRotMat = [](const Entity& ent) -> XMFLOAT3X3 {
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(ent.rotation[0]),
            XMConvertToRadians(ent.rotation[1]),
            XMConvertToRadians(ent.rotation[2]));
        XMFLOAT3X3 rm;
        XMStoreFloat3x3(&rm, R);
        return rm;
    };

    // Helper: get entity center and half-extents (accounts for mesh bounds)
    auto getEntityBounds = [](const Entity& ent, XMFLOAT3& outCenter, XMFLOAT3& outHalf) {
        outCenter = { ent.position[0], ent.position[1], ent.position[2] };
        outHalf = { ent.scale[0] * 0.52f, ent.scale[1] * 0.52f, ent.scale[2] * 0.52f };

        if (ent.meshType == MeshType::Custom && !ent.meshName.empty()) {
            Mesh* mesh = ResourceManager::Get().GetMesh(ent.meshName);
            if (mesh && mesh->HasBounds()) {
                auto bc = mesh->GetBoundsCenter();
                auto bh = mesh->GetBoundsHalfExtent();
                outHalf = { bh.x * ent.scale[0] * 1.04f, bh.y * ent.scale[1] * 1.04f, bh.z * ent.scale[2] * 1.04f };
                // Rotate bounds center offset into world space
                XMMATRIX R = XMMatrixRotationRollPitchYaw(
                    XMConvertToRadians(ent.rotation[0]),
                    XMConvertToRadians(ent.rotation[1]),
                    XMConvertToRadians(ent.rotation[2]));
                XMVECTOR off = XMVectorSet(bc.x * ent.scale[0], bc.y * ent.scale[1], bc.z * ent.scale[2], 0);
                off = XMVector3TransformNormal(off, R);
                XMFLOAT3 rOff;
                XMStoreFloat3(&rOff, off);
                outCenter.x += rOff.x;
                outCenter.y += rOff.y;
                outCenter.z += rOff.z;
            }
        }
    };

    // Hover highlight
    if (m_hoveredEntity >= 0 && m_hoveredEntity < state.scene.GetEntityCount()
        && m_hoveredEntity != state.selectedEntity) {
        const auto& e = state.scene.GetEntity(m_hoveredEntity);
        XMFLOAT3 center, half;
        getEntityBounds(e, center, half);
        m_debugRenderer.DrawRotatedBox(center, half, entityRotMat(e),
            { 0.5f, 0.7f, 1.0f, 0.6f });
    }

    // Selected highlight (pulsing)
    if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
        const auto& e = state.scene.GetEntity(state.selectedEntity);
        XMFLOAT3 center, half;
        getEntityBounds(e, center, half);

        // Pulsing alpha for selection highlight
        float pulse = 0.7f + 0.3f * sinf(m_selectionPulse);
        m_debugRenderer.DrawRotatedBox(center, half, entityRotMat(e),
            { 1.0f, 0.8f, 0.2f, pulse });
    }

    // Placement preview
    if (m_currentTool == LevelEditTool::Place && !m_leftDragging) {
        XMFLOAT3 wp = ScreenToWorldPlane(m_mouseX, m_mouseY, 0.0f);
        if (m_gridSnap) {
            wp.x = roundf(wp.x / m_gridSnapSize) * m_gridSnapSize;
            wp.z = roundf(wp.z / m_gridSnapSize) * m_gridSnapSize;
        }
        m_debugRenderer.DrawBox({ wp.x, 0.5f, wp.z }, { 0.52f, 0.52f, 0.52f },
                                { 0.3f, 0.9f, 0.3f, 0.5f });
    }
}

// ==================== Gizmo Rendering ====================
void LevelEditorWindow::RenderGizmo(ID3D11DeviceContext* ctx, const EditorState& state) {
    (void)ctx;
    if (state.selectedEntity < 0 || state.selectedEntity >= state.scene.GetEntityCount())
        return;

    const auto& e = state.scene.GetEntity(state.selectedEntity);
    XMFLOAT3 c = { e.position[0], e.position[1], e.position[2] };

    // Offset by mesh bounds center for custom meshes (rotated into world space)
    if (e.meshType == MeshType::Custom) {
        auto mesh = ResourceManager::Get().GetMesh(e.meshName);
        if (mesh && mesh->HasBounds()) {
            auto bc = mesh->GetBoundsCenter();
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(e.rotation[0]),
                XMConvertToRadians(e.rotation[1]),
                XMConvertToRadians(e.rotation[2]));
            XMVECTOR off = XMVectorSet(bc.x * e.scale[0], bc.y * e.scale[1], bc.z * e.scale[2], 0);
            off = XMVector3TransformNormal(off, R);
            XMFLOAT3 rOff;
            XMStoreFloat3(&rOff, off);
            c.x += rOff.x;
            c.y += rOff.y;
            c.z += rOff.z;
        }
    }

    // Distance-based gizmo scaling (consistent screen size)
    float dist = sqrtf((c.x - m_camX) * (c.x - m_camX) +
                       (c.y - m_camY) * (c.y - m_camY) +
                       (c.z - m_camZ) * (c.z - m_camZ));
    float gl = m_gizmoLength * (std::max)(dist * 0.07f, 0.5f);
    float arrowSize = gl * 0.12f;

    bool highlightX = (m_hoveredAxis == 0 || m_activeAxis == 0 || m_axisConstraint == AxisConstraint::X);
    bool highlightY = (m_hoveredAxis == 1 || m_activeAxis == 1 || m_axisConstraint == AxisConstraint::Y);
    bool highlightZ = (m_hoveredAxis == 2 || m_activeAxis == 2 || m_axisConstraint == AxisConstraint::Z);

    // X axis (Red)
    XMFLOAT4 xc = highlightX ? XMFLOAT4(1.0f, 0.5f, 0.2f, 1.0f) : XMFLOAT4(0.9f, 0.15f, 0.15f, 1.0f);
    m_debugRenderer.DrawLine(c, { c.x + gl, c.y, c.z }, xc);
    m_debugRenderer.DrawLine({ c.x + gl, c.y, c.z }, { c.x + gl - arrowSize, c.y + arrowSize * 0.5f, c.z }, xc);
    m_debugRenderer.DrawLine({ c.x + gl, c.y, c.z }, { c.x + gl - arrowSize, c.y - arrowSize * 0.5f, c.z }, xc);

    // Y axis (Green)
    XMFLOAT4 yc = highlightY ? XMFLOAT4(0.5f, 1.0f, 0.2f, 1.0f) : XMFLOAT4(0.15f, 0.85f, 0.15f, 1.0f);
    m_debugRenderer.DrawLine(c, { c.x, c.y + gl, c.z }, yc);
    m_debugRenderer.DrawLine({ c.x, c.y + gl, c.z }, { c.x + arrowSize * 0.5f, c.y + gl - arrowSize, c.z }, yc);
    m_debugRenderer.DrawLine({ c.x, c.y + gl, c.z }, { c.x - arrowSize * 0.5f, c.y + gl - arrowSize, c.z }, yc);

    // Z axis (Blue)
    XMFLOAT4 zc = highlightZ ? XMFLOAT4(0.2f, 0.5f, 1.0f, 1.0f) : XMFLOAT4(0.15f, 0.3f, 0.9f, 1.0f);
    m_debugRenderer.DrawLine(c, { c.x, c.y, c.z + gl }, zc);
    m_debugRenderer.DrawLine({ c.x, c.y, c.z + gl }, { c.x, c.y + arrowSize * 0.5f, c.z + gl - arrowSize }, zc);
    m_debugRenderer.DrawLine({ c.x, c.y, c.z + gl }, { c.x, c.y - arrowSize * 0.5f, c.z + gl - arrowSize }, zc);

    // Scale tool: cubes at axis tips
    if (m_currentTool == LevelEditTool::Scale) {
        float bs = arrowSize * 0.8f;
        m_debugRenderer.DrawBox({ c.x + gl, c.y, c.z }, { bs, bs, bs }, xc);
        m_debugRenderer.DrawBox({ c.x, c.y + gl, c.z }, { bs, bs, bs }, yc);
        m_debugRenderer.DrawBox({ c.x, c.y, c.z + gl }, { bs, bs, bs }, zc);
    }

    // Rotate tool: rotation rings
    if (m_currentTool == LevelEditTool::Rotate) {
        int segments = 32;
        float radius = gl * 0.8f;
        for (int i = 0; i < segments; i++) {
            float a0 = (float)i / segments * 6.283185f;
            float a1 = (float)(i + 1) / segments * 6.283185f;

            // Y rotation ring (XZ plane)
            if (m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::None) {
                m_debugRenderer.DrawLine(
                    { c.x + cosf(a0) * radius, c.y, c.z + sinf(a0) * radius },
                    { c.x + cosf(a1) * radius, c.y, c.z + sinf(a1) * radius },
                    highlightY ? XMFLOAT4(0.5f, 1.0f, 0.2f, 0.6f) : XMFLOAT4(0.15f, 0.7f, 0.15f, 0.4f));
            }
            if (m_axisConstraint == AxisConstraint::X) {
                m_debugRenderer.DrawLine(
                    { c.x, c.y + cosf(a0) * radius, c.z + sinf(a0) * radius },
                    { c.x, c.y + cosf(a1) * radius, c.z + sinf(a1) * radius },
                    highlightX ? XMFLOAT4(1.0f, 0.5f, 0.2f, 0.6f) : XMFLOAT4(0.7f, 0.15f, 0.15f, 0.4f));
            }
            if (m_axisConstraint == AxisConstraint::Z) {
                m_debugRenderer.DrawLine(
                    { c.x + cosf(a0) * radius, c.y + sinf(a0) * radius, c.z },
                    { c.x + cosf(a1) * radius, c.y + sinf(a1) * radius, c.z },
                    highlightZ ? XMFLOAT4(0.2f, 0.5f, 1.0f, 0.6f) : XMFLOAT4(0.15f, 0.3f, 0.7f, 0.4f));
            }
        }
    }

    // Constraint guide line during drag
    if (m_isDragging && (m_axisConstraint == AxisConstraint::X || m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::Z)) {
        float lineExt = 50.0f;
        XMFLOAT4 lc = { 0.5f, 0.5f, 0.5f, 0.3f };
        switch (m_axisConstraint) {
            case AxisConstraint::X:
                lc = { 0.8f, 0.2f, 0.2f, 0.3f };
                m_debugRenderer.DrawLine({ c.x - lineExt, c.y, c.z }, { c.x + lineExt, c.y, c.z }, lc);
                break;
            case AxisConstraint::Y:
                lc = { 0.2f, 0.8f, 0.2f, 0.3f };
                m_debugRenderer.DrawLine({ c.x, c.y - lineExt, c.z }, { c.x, c.y + lineExt, c.z }, lc);
                break;
            case AxisConstraint::Z:
                lc = { 0.2f, 0.2f, 0.8f, 0.3f };
                m_debugRenderer.DrawLine({ c.x, c.y, c.z - lineExt }, { c.x, c.y, c.z + lineExt }, lc);
                break;
            default: break;
        }
    }
}

void LevelEditorWindow::RenderToolbar(ID3D11DeviceContext* ctx, EditorState& state) {
    (void)ctx;
    std::wstring title = L"Level Editor [";
    const char* tn = LevelEditToolName(m_currentTool);
    title += std::wstring(tn, tn + strlen(tn)) + L"]";

    if (m_axisConstraint != AxisConstraint::None) {
        const char* an = AxisConstraintName(m_axisConstraint);
        title += L" ";
        for (const char* p = an; *p; ++p) title += static_cast<wchar_t>(*p);
    }

    if (!m_currentLevelPath.empty()) {
        std::string n = LevelFile::GetLevelName(m_currentLevelPath);
        title += L" - ";
        for (char c : n) title += static_cast<wchar_t>(c);
    } else {
        title += L" - Unsaved";
    }
    if (m_unsavedChanges) title += L" *";
    if (m_gridSnap) title += L" [Snap]";
    title += L"  |  Ent: " + std::to_wstring(state.scene.GetEntityCount());
    SetWindowTextW(m_hwnd, title.c_str());
}

// ==================== ImGui Outliner Panel (Tabbed) ====================

void LevelEditorWindow::DrawOutlinerPanel(EditorState& state) {
    // ── Collapse/expand toggle button (always visible, left edge) ──
    if (!m_panelVisible) {
        // Draw a small vertical tab on the left edge to re-open
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(28, (float)m_height), ImGuiCond_Always);
        ImGuiWindowFlags tabFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.065f, 0.08f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 4));
        if (ImGui::Begin("##PanelTab", nullptr, tabFlags)) {
            ImGui::SetCursorPosY((float)m_height * 0.5f - 30);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.20f, 0.34f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.30f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            if (ImGui::Button(">>\nTab", ImVec2(24, 60)))
                m_panelVisible = true;
            ImGui::PopStyleColor(3);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return;
    }

    // ── Main panel (LEFT side) ──
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_panelWidth, (float)m_height), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanelBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));

    if (ImGui::Begin("##LevelEditorOutliner", nullptr, flags)) {

        // ── Header bar ──
        {
            // Hide button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.17f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
            if (ImGui::Button("<<", ImVec2(28, 20)))
                m_panelVisible = false;
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide panel (Tab)");
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
            ImGui::Text("LEVEL EDITOR");
            ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 45);
            ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
            ImGui::Text("%d ent", state.scene.GetEntityCount());
            ImGui::PopStyleColor();
        }

        // ── Status message ──
        if (m_statusTimer > 0.0f) {
            m_statusTimer -= ImGui::GetIO().DeltaTime;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, (std::min)(m_statusTimer, 1.0f)));
            ImGui::TextWrapped("  %s", m_statusMessage.c_str());
            ImGui::PopStyleColor();
        }

        // ── Tool bar (always visible) ──
        ImGui::Spacing();
        DrawToolSection(state);

        // ── Thin accent line ──
        ImGui::Spacing();
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 2), IM_COL32(60, 100, 180, 120));
            ImGui::Dummy(ImVec2(0, 5));
        }

        // ── Tab bar ──
        const char* tabLabels[] = { "File", "Scene", "World", "Render", "Settings" };
        constexpr int tabCount = 5;
        float tabW = (ImGui::GetContentRegionAvail().x - (float)(tabCount - 1) * 2) / (float)tabCount;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        for (int t = 0; t < tabCount; t++) {
            bool active = (m_activeTab == t);
            ImVec4 bg  = active ? kTabActive : kTabInact;
            ImVec4 hov = active ? kTabActive : kTabHover;
            ImVec4 txt = active ? kAccent    : kTextMuted;

            ImGui::PushStyleColor(ImGuiCol_Button,        bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  hov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   kTabActive);
            ImGui::PushStyleColor(ImGuiCol_Text,           txt);
            if (ImGui::Button(tabLabels[t], ImVec2(tabW, 24)))
                m_activeTab = t;
            ImGui::PopStyleColor(4);
            if (t < tabCount - 1) ImGui::SameLine(0, 2);
        }
        ImGui::PopStyleVar();

        // ── Active underline ──
        {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            float startX = cp.x + (float)m_activeTab * (tabW + 2.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(startX, cp.y), ImVec2(startX + tabW, cp.y + 2),
                IM_COL32(90, 148, 255, 220));
            ImGui::Dummy(ImVec2(0, 5));
        }

        // ── Tab content (scrollable) ──
        ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None);

        switch (m_activeTab) {
        case 0: // File
            DrawLevelSection(state);
            DrawPCGSection(state);
            break;
        case 1: // Scene
            DrawEntitySection(state);
            DrawPrefabSection(state);
            DrawSceneSection(state);
            break;
        case 2: // World
            DrawLightingSection(state);
            DrawSkySection(state);
            DrawShadowsSection(state);
            DrawSSAOSection(state);
            break;
        case 3: // Render
            DrawPostProcessSection(state);
            DrawArtStyleSection(state);
            break;
        case 4: // Settings
            DrawGridSection();
            DrawPlacementSection();
            DrawCullingSection(state);
            DrawCharacterSection(state);
            break;
        }

        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// â”€â”€â”€ Draw*Section implementations are in LevelEditorSections.cpp â”€â”€â”€

// ==================== Level File Operations ====================
void LevelEditorWindow::SaveCurrentLevel(EditorState& state) {
    if (m_currentLevelPath.empty()) {
        // Check if Save As name buffer has text â€” use it instead of "untitled"
        if (m_levelNameBuf[0] != '\0') {
            m_currentLevelPath = m_levelsDirectory + std::string(m_levelNameBuf) + ".wtlevel";
            m_levelNameBuf[0] = '\0';
        } else {
            m_statusMessage = "Enter a name in Save As first!";
            m_statusTimer = 3.0f;
            LOG_WARN("Save: no level path and no Save As name entered");
            return;
        }
    }

    LOG_INFO("Saving level to: %s", m_currentLevelPath.c_str());

    if (LevelFile::Save(m_currentLevelPath, state.scene)) {
        m_unsavedChanges = false;
        m_statusMessage = "Saved: " + LevelFile::GetLevelName(m_currentLevelPath);
        m_statusTimer = 3.0f;
        LOG_INFO("Level saved: %s (%d entities)", m_currentLevelPath.c_str(), state.scene.GetEntityCount());
    } else {
        m_statusMessage = "SAVE FAILED!";
        m_statusTimer = 5.0f;
        LOG_ERROR("Level save failed: %s", m_currentLevelPath.c_str());
    }
}

void LevelEditorWindow::LoadLevel(const std::string& path, EditorState& state) {
    LOG_INFO("Loading level from: %s", path.c_str());
    if (LevelFile::Load(path, state.scene)) {
        m_currentLevelPath = path;
        state.selectedEntity = -1;
        m_unsavedChanges = false;
        state.physicsRebuildRequested = true;
        state.entityDirty = true;
        m_statusMessage = "Loaded: " + LevelFile::GetLevelName(path) + " (" + std::to_string(state.scene.GetEntityCount()) + " ent)";
        m_statusTimer = 3.0f;
        LOG_INFO("Level loaded: %s (%d entities)", path.c_str(), state.scene.GetEntityCount());
    } else {
        m_statusMessage = "LOAD FAILED!";
        m_statusTimer = 5.0f;
        LOG_ERROR("Level load failed: %s", path.c_str());
    }
}

void LevelEditorWindow::NewLevel(EditorState& state) {
    state.scene.Clear();
    state.selectedEntity = -1;
    m_currentLevelPath.clear();
    m_unsavedChanges = false;
    state.physicsRebuildRequested = true;
    state.entityDirty = true;
    m_statusMessage = "New level created";
    m_statusTimer = 2.0f;
    LOG_INFO("New level created");
}

void LevelEditorWindow::DeleteCurrentLevel(EditorState& state) {
    if (m_currentLevelPath.empty()) {
        m_statusMessage = "No level file to delete";
        m_statusTimer = 3.0f;
        return;
    }
    try {
        if (std::filesystem::exists(m_currentLevelPath)) {
            std::filesystem::remove(m_currentLevelPath);
            LOG_INFO("Deleted level: %s", m_currentLevelPath.c_str());
            m_statusMessage = "Deleted: " + LevelFile::GetLevelName(m_currentLevelPath);
            m_statusTimer = 3.0f;
            m_currentLevelPath.clear();
            m_unsavedChanges = false;
        } else {
            m_statusMessage = "File not found";
            m_statusTimer = 3.0f;
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to delete level: %s", ex.what());
        m_statusMessage = "DELETE FAILED!";
        m_statusTimer = 5.0f;
    }
}

// ==================== Focus On Entity ====================
void LevelEditorWindow::FocusOnEntity(const EditorState& state) {
    if (state.selectedEntity < 0 || state.selectedEntity >= state.scene.GetEntityCount())
        return;

    const auto& e = state.scene.GetEntity(state.selectedEntity);
    XMFLOAT3 center = { e.position[0], e.position[1], e.position[2] };

    // For custom meshes, offset by bounds center
    float maxExtent = (std::max)({ e.scale[0], e.scale[1], e.scale[2] });
    if (e.meshType == MeshType::Custom && !e.meshName.empty()) {
        Mesh* mesh = ResourceManager::Get().GetMesh(e.meshName);
        if (mesh && mesh->HasBounds()) {
            auto bc = mesh->GetBoundsCenter();
            auto bh = mesh->GetBoundsHalfExtent();
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(e.rotation[0]),
                XMConvertToRadians(e.rotation[1]),
                XMConvertToRadians(e.rotation[2]));
            XMVECTOR off = XMVectorSet(bc.x * e.scale[0], bc.y * e.scale[1], bc.z * e.scale[2], 0);
            off = XMVector3TransformNormal(off, R);
            XMFLOAT3 rOff;
            XMStoreFloat3(&rOff, off);
            center.x += rOff.x;
            center.y += rOff.y;
            center.z += rOff.z;
            maxExtent = (std::max)({ bh.x * e.scale[0], bh.y * e.scale[1], bh.z * e.scale[2] });
        }
    }

    // Place camera at a distance based on object size, looking at the center
    float dist = maxExtent * 3.0f + 2.0f;

    // Calculate direction from current camera yaw/pitch
    float cosP = cosf(m_camPitch);
    float fwdX = cosP * sinf(m_camYaw);
    float fwdY = -sinf(m_camPitch);
    float fwdZ = cosP * cosf(m_camYaw);

    m_camX = center.x - fwdX * dist;
    m_camY = center.y - fwdY * dist;
    m_camZ = center.z - fwdZ * dist;
}

// ==================== Shutdown ====================
void LevelEditorWindow::Shutdown() {
    ShutdownImGui();
    g_levelEditor = nullptr;
    m_debugRenderer.Shutdown();
    m_rtv.Reset(); m_dsv.Reset();
    m_backBuffer.Reset(); m_depthBuffer.Reset();
    m_swapChain.Reset();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    m_open = false;
    LOG_INFO("Level Editor window shutdown");
}

} // namespace WT
