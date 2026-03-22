#pragma once

#include <DirectXMath.h>
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace WT {

using namespace DirectX;

class WeaponSystem;
class Character;
class Renderer;
class Texture;
class Scene;
class Camera;

// ============================================================
// HUD — 2D overlay for crosshair, ammo, hit markers
// Uses ImGui's draw list for 2D rendering on top of the scene.
// ============================================================

struct HUDSettings {
    bool  showCrosshair    = true;
    bool  showAmmo         = true;
    bool  showHitMarker    = true;
    bool  showReloadBar    = true;
    bool  showCompass      = true;
    bool  showHealthBar    = true;
    bool  showFps          = true;
    float hudOpacity       = 1.0f;
};

class HUD {
public:
    void Init();
    void Shutdown();

    // Draw HUD elements (call inside ImGui frame, after scene rendering)
    void Draw(const WeaponSystem& weapon, const Character& character,
              float playerYaw, int screenWidth, int screenHeight, int fps = 0);

    HUDSettings& GetSettings() { return m_settings; }
    const HUDSettings& GetSettings() const { return m_settings; }

    void SetInteractPrompt(const std::string& prompt) { m_interactPrompt = prompt; }
    void SetInteractTarget(int entityIndex) { m_interactTargetIndex = entityIndex; }

    // Draw animated interact icons above interactable entities (call inside ImGui frame)
    void DrawWorldInteractIcons(const Camera& camera, const Scene& scene,
                                int screenWidth, int screenHeight);

private:
    void LoadUITextures();
    void DrawCrosshair(const WeaponSystem& weapon, float cx, float cy);
    void DrawAmmoCounter(const WeaponSystem& weapon, float screenW, float screenH);
    void DrawHitMarker(const WeaponSystem& weapon, float cx, float cy);
    void DrawReloadBar(const WeaponSystem& weapon, float cx, float cy);
    void DrawCompass(float playerYaw, float screenW);
    void DrawHealthBar(const Character& character, float screenW, float screenH);
    void DrawDamageVignette(const Character& character, float screenW, float screenH);
    void DrawFpsCounter(int fps, float screenW);
    void DrawInteractPrompt(float cx, float cy);

    HUDSettings m_settings;

    // Interaction prompt (set externally by InteractionSystem)
    std::string m_interactPrompt;
    int m_interactTargetIndex = -1;  // Entity currently showing text prompt

    // UI sprite textures (loaded lazily from ResourceManager)
    bool     m_uiTexturesLoaded = false;

    // Animated HP bar (3-frame cycle)
    Texture* m_hpFrame[3]     = { nullptr, nullptr, nullptr };
    Texture* m_hpFillSheet    = nullptr;
    float    m_hpAnimTimer    = 0.0f;
    int      m_hpAnimFrame    = 0;

    // Animated ammo border (3-frame sprite sheet)
    Texture* m_ammoBorderSheet = nullptr;
    float    m_ammoAnimTimer   = 0.0f;
    int      m_ammoAnimFrame   = 0;

    // Per-weapon crosshair sprites
    Texture* m_xhairPistol = nullptr;   // small + (Pistol)
    Texture* m_xhairRifle  = nullptr;   // cross with dots (Rifle)
    Texture* m_xhairSg553  = nullptr;   // detailed reticle (SG553, M4A1)
    Texture* m_xhairHeavy  = nullptr;   // heavy tactical (Rifle3)

    // Animated interact icon billboard (3-frame sprite sheet 96x32, each frame 32x32)
    Texture* m_interactIconSheet = nullptr;
    float    m_interactIconTimer = 0.0f;
    int      m_interactIconFrame = 0;
};

} // namespace WT
