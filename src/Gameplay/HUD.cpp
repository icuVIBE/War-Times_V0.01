#include "HUD.h"
#include "WeaponSystem.h"
#include "Core/Character.h"
#include "Core/ResourceManager.h"
#include "Core/Entity.h"
#include "Graphics/Texture.h"
#include "Graphics/Camera.h"
#include "Util/Log.h"

// ImGui for 2D overlay drawing
#include "imgui.h"

#include <cstdio>
#include <cmath>

namespace WT {

void HUD::Init() {
    LOG_INFO("HUD initialized");
}

void HUD::Shutdown() {
    LOG_INFO("HUD shutdown");
}

void HUD::LoadUITextures() {
    m_uiTexturesLoaded = true;
    auto& rm = ResourceManager::Get();

    // Animated HP bar frames (3-frame cycle)
    m_hpFrame[0]  = rm.GetTexture("UI/hp_frame1");
    m_hpFrame[1]  = rm.GetTexture("UI/hp_frame2");
    m_hpFrame[2]  = rm.GetTexture("UI/hp_frame3");
    m_hpFillSheet = rm.GetTexture("UI/hp_fill_sheet");

    int hpCount = (m_hpFrame[0]?1:0)+(m_hpFrame[1]?1:0)+(m_hpFrame[2]?1:0);
    LOG_INFO("HUD: loaded %d/3 HP frame sprites", hpCount);
    if (m_hpFillSheet) LOG_INFO("HUD: loaded HP fill sheet sprite");

    // Per-weapon crosshair sprites
    m_xhairPistol = rm.GetTexture("UI/xhair_pistol");
    m_xhairRifle  = rm.GetTexture("UI/xhair_rifle");
    m_xhairSg553  = rm.GetTexture("UI/xhair_sg553");
    m_xhairHeavy  = rm.GetTexture("UI/xhair_heavy");
    int xhCount = (m_xhairPistol?1:0)+(m_xhairRifle?1:0)+(m_xhairSg553?1:0)+(m_xhairHeavy?1:0);
    LOG_INFO("HUD: loaded %d/4 crosshair sprites", xhCount);

    // Animated ammo border (3-frame sprite sheet 384x128, each frame 128x128)
    m_ammoBorderSheet = rm.GetTexture("UI/ammo_border_sheet");
    if (m_ammoBorderSheet) LOG_INFO("HUD: loaded ammo border sprite sheet");

    // Animated interact icon (3-frame sprite sheet 96x32, each frame 32x32)
    m_interactIconSheet = rm.GetTexture("UI/interact_icon_sheet");
    if (m_interactIconSheet) LOG_INFO("HUD: loaded interact icon sprite sheet");
}

// ============================================================
// Main Draw
// ============================================================

void HUD::Draw(const WeaponSystem& weapon, const Character& character,
               float playerYaw, int screenWidth, int screenHeight, int fps) {
    if (!m_uiTexturesLoaded) LoadUITextures();

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);
    float cx = sw * 0.5f;
    float cy = sh * 0.5f;

    // Create a fullscreen transparent ImGui window for HUD overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(sw, sh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBringToFrontOnFocus |
                              ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##HUD", nullptr, flags)) {
        if (m_settings.showCrosshair) DrawCrosshair(weapon, cx, cy);
        if (m_settings.showHitMarker) DrawHitMarker(weapon, cx, cy);
        if (m_settings.showReloadBar && weapon.IsReloading()) DrawReloadBar(weapon, cx, cy);
        if (m_settings.showAmmo) DrawAmmoCounter(weapon, sw, sh);
        // Convert yaw from radians to degrees for compass
        float yawDeg = playerYaw * (180.0f / 3.14159265f);
        if (m_settings.showCompass) DrawCompass(yawDeg, sw);
        if (m_settings.showHealthBar) DrawHealthBar(character, sw, sh);
        DrawDamageVignette(character, sw, sh);
        if (m_settings.showFps) DrawFpsCounter(fps, sw);
        if (!m_interactPrompt.empty()) DrawInteractPrompt(cx, cy);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// ============================================================
// FPS Counter
// ============================================================

void HUD::DrawFpsCounter(int fps, float screenW) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col;
    if (fps >= 60)      col = IM_COL32(50, 255, 50, 200);   // green
    else if (fps >= 30) col = IM_COL32(255, 255, 50, 200);  // yellow
    else                col = IM_COL32(255, 50, 50, 200);   // red

    char buf[32];
    snprintf(buf, sizeof(buf), "%d FPS", fps);
    ImFont* font = ImGui::GetFont();
    float fontSize = 18.0f;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
    float x = screenW - textSize.x - 12.0f;
    float y = 10.0f;
    // Drop shadow
    dl->AddText(font, fontSize, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 150), buf);
    dl->AddText(font, fontSize, ImVec2(x, y), col, buf);
}

// ============================================================
// Crosshair
// ============================================================

void HUD::DrawCrosshair(const WeaponSystem& weapon, float cx, float cy) {
    const auto& ws = weapon.GetSettings();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float size = ws.crosshairSize;
    float gap  = ws.crosshairGap;
    float thick = ws.crosshairThickness;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(ws.crosshairColor[0], ws.crosshairColor[1],
               ws.crosshairColor[2], ws.crosshairColor[3] * m_settings.hudOpacity));

    // Expand gap when firing (dynamic crosshair)
    if (weapon.IsFiring()) {
        gap += 4.0f;
        size += 3.0f;
    }
    if (weapon.IsADS()) {
        gap *= 0.3f;
        size *= 0.6f;
    }

    // Per-weapon sprite crosshair
    Texture* xhTex = nullptr;
    switch (weapon.GetCurrentWeapon()) {
        case WeaponType::Pistol: xhTex = m_xhairPistol; break;
        case WeaponType::Rifle:  xhTex = m_xhairRifle;  break;
        case WeaponType::Rifle3: xhTex = m_xhairHeavy;  break;
        case WeaponType::SG553:  xhTex = m_xhairSg553;  break;
        case WeaponType::M4A1:   xhTex = m_xhairSg553;  break;
        default:                 xhTex = m_xhairRifle;  break;
    }

    if (xhTex && xhTex->IsValid()) {
        // Draw size: proportional to crosshair settings, minimum 32px
        float drawSize = (size + gap) * 2.5f;
        if (drawSize < 32.0f) drawSize = 32.0f;

        // Keep aspect ratio from the source sprite
        float aspect = (float)xhTex->GetWidth() / (float)xhTex->GetHeight();
        float drawW = drawSize * aspect;
        float drawH = drawSize;

        dl->AddImage((ImTextureID)xhTex->GetSRV(),
                     ImVec2(cx - drawW * 0.5f, cy - drawH * 0.5f),
                     ImVec2(cx + drawW * 0.5f, cy + drawH * 0.5f),
                     ImVec2(0, 0), ImVec2(1, 1), col);
        return;
    }

    // Fallback: line-based crosshair
    dl->AddLine(ImVec2(cx, cy - gap - size), ImVec2(cx, cy - gap), col, thick);
    dl->AddLine(ImVec2(cx, cy + gap), ImVec2(cx, cy + gap + size), col, thick);
    dl->AddLine(ImVec2(cx - gap - size, cy), ImVec2(cx - gap, cy), col, thick);
    dl->AddLine(ImVec2(cx + gap, cy), ImVec2(cx + gap + size, cy), col, thick);

    if (ws.crosshairDot) {
        dl->AddRectFilled(ImVec2(cx - 1.0f, cy - 1.0f), ImVec2(cx + 1.0f, cy + 1.0f), col);
    }
}

// ============================================================
// Hit Marker
// ============================================================

void HUD::DrawHitMarker(const WeaponSystem& weapon, float cx, float cy) {
    if (!weapon.IsHitMarkerActive()) return;

    const auto& ws = weapon.GetSettings();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float s = ws.hitMarkerSize;
    float thick = 2.0f;

    // Color varies by hit type: kill = red X, headshot = yellow, normal = white
    ImU32 col;
    if (weapon.LastHitWasKill()) {
        col = IM_COL32(255, 50, 50, (int)(255 * m_settings.hudOpacity));
        s *= 1.3f;   // Bigger for kills
        thick = 3.0f;
    } else if (weapon.LastHitWasHeadshot()) {
        col = IM_COL32(255, 220, 50, (int)(255 * m_settings.hudOpacity));
        s *= 1.15f;
    } else {
        col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(ws.hitMarkerColor[0], ws.hitMarkerColor[1],
                   ws.hitMarkerColor[2], ws.hitMarkerColor[3] * m_settings.hudOpacity));
    }

    // Four diagonal lines forming an X
    dl->AddLine(ImVec2(cx - s, cy - s), ImVec2(cx - s * 0.4f, cy - s * 0.4f), col, thick);
    dl->AddLine(ImVec2(cx + s, cy - s), ImVec2(cx + s * 0.4f, cy - s * 0.4f), col, thick);
    dl->AddLine(ImVec2(cx - s, cy + s), ImVec2(cx - s * 0.4f, cy + s * 0.4f), col, thick);
    dl->AddLine(ImVec2(cx + s, cy + s), ImVec2(cx + s * 0.4f, cy + s * 0.4f), col, thick);

    // Kill confirmation: full X through center
    if (weapon.LastHitWasKill()) {
        dl->AddLine(ImVec2(cx - s * 0.3f, cy - s * 0.3f), ImVec2(cx + s * 0.3f, cy + s * 0.3f), col, thick);
        dl->AddLine(ImVec2(cx + s * 0.3f, cy - s * 0.3f), ImVec2(cx - s * 0.3f, cy + s * 0.3f), col, thick);
    }
}

// ============================================================
// Ammo Counter
// ============================================================

void HUD::DrawAmmoCounter(const WeaponSystem& weapon, float screenW, float screenH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float opacity = m_settings.hudOpacity;

    // Advance ammo border animation (same speed as HP bar)
    float dt = ImGui::GetIO().DeltaTime;
    m_ammoAnimTimer += dt;
    const float frameDuration = 1.0f / 1.5f;
    while (m_ammoAnimTimer >= frameDuration) {
        m_ammoAnimTimer -= frameDuration;
        m_ammoAnimFrame = (m_ammoAnimFrame + 1) % 3;
    }

    // Animated border sprite (3 frames, 128x128 each in a 384x128 sheet)
    // Scale 2x so the ~78x43 visible box becomes ~156x86 — enough for ammo text
    const float scale = 2.0f;
    const float borderW = 128.0f * scale;  // 256
    const float borderH = 128.0f * scale;  // 256
    // Align bottom with HP bar (HP bar bottom = screenH - 15)
    float borderX = screenW - borderW - 40.0f;   // push left so visible box is well inside screen
    float borderY = screenH - borderH + 60.0f;   // nudge up (border box starts at ~40px into sprite)

    if (m_ammoBorderSheet && m_ammoBorderSheet->IsValid()) {
        float u0 = m_ammoAnimFrame / 3.0f;
        float u1 = (m_ammoAnimFrame + 1) / 3.0f;
        ImTextureID borderTex = (ImTextureID)m_ammoBorderSheet->GetSRV();
        ImU32 borderCol = IM_COL32(255, 255, 255, (int)(255 * opacity));
        dl->AddImage(borderTex,
                     ImVec2(borderX, borderY),
                     ImVec2(borderX + borderW, borderY + borderH),
                     ImVec2(u0, 0), ImVec2(u1, 1), borderCol);
    } else {
        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.4f * opacity));
        float fbX = screenW - 180.0f;
        float fbY = screenH - 70.0f;
        dl->AddRectFilled(ImVec2(fbX - 10, fbY - 10), ImVec2(fbX + 170, fbY + 55), bgCol, 4.0f);
    }

    // Content area — centered in the visible box region of the sprite
    // Visible box at ~(25,40)-(102,80) in 128x128 → scaled: (50,80)-(204,160) relative to borderX,borderY
    float contentX = borderX + 65.0f;
    float contentY = borderY + 90.0f;

    // Weapon name
    ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, opacity));
    dl->AddText(ImVec2(contentX, contentY), labelCol, WeaponTypeName(weapon.GetCurrentWeapon()));

    // Ammo count: current / max
    char ammoText[64];
    snprintf(ammoText, sizeof(ammoText), "%d / %d", weapon.GetCurrentAmmo(), weapon.GetMaxAmmo());

    ImU32 ammoCol;
    if (weapon.GetCurrentAmmo() == 0)
        ammoCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.2f, 0.2f, opacity));
    else if (weapon.GetCurrentAmmo() <= weapon.GetMaxAmmo() / 4)
        ammoCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.7f, 0.2f, opacity));
    else
        ammoCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, opacity));

    ImFont* font = ImGui::GetFont();
    dl->AddText(font, 28.0f, ImVec2(contentX, contentY + 15.0f), ammoCol, ammoText);

    // Reserve ammo
    char reserveText[32];
    snprintf(reserveText, sizeof(reserveText), "| %d", weapon.GetReserveAmmo());
    dl->AddText(font, 16.0f, ImVec2(contentX + 90.0f, contentY + 22.0f), labelCol, reserveText);

    // Reloading indicator
    if (weapon.IsReloading()) {
        ImU32 reloadCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.9f, 0.3f, opacity));
        dl->AddText(ImVec2(contentX + 20.0f, contentY - 5.0f), reloadCol, "RELOADING");
    }
}

// ============================================================
// Reload Progress Bar
// ============================================================

void HUD::DrawReloadBar(const WeaponSystem& weapon, float cx, float cy) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;

    float barWidth  = 120.0f;
    float barHeight = 4.0f;
    float barY = cy + 30.0f;
    float barX = cx - barWidth * 0.5f;

    float progress = weapon.GetReloadProgress();

    // Background
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 0.6f * opacity));
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), bgCol, 2.0f);

    // Fill
    ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.9f, 0.3f, 0.9f * opacity));
    dl->AddRectFilled(ImVec2(barX, barY),
                       ImVec2(barX + barWidth * progress, barY + barHeight), fillCol, 2.0f);
}

// ============================================================
// Compass — top of screen, shows cardinal directions
// ============================================================

void HUD::DrawCompass(float playerYaw, float screenW) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;

    // Compass bar at very top of screen
    float barY      = 18.0f;
    float barHalfW  = 220.0f;   // Half-width of visible compass strip
    float barCenterX = screenW * 0.5f;

    // Background bar
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.35f * opacity));
    dl->AddRectFilled(ImVec2(barCenterX - barHalfW, barY - 12.0f),
                       ImVec2(barCenterX + barHalfW, barY + 14.0f), bgCol, 3.0f);

    // Compass directions and their yaw angles (0 = +Z = North)
    struct CompassMark {
        const char* label;
        float yaw;     // degrees
        bool major;    // cardinal vs intercardinal
    };
    static const CompassMark marks[] = {
        { "N",  0.0f,   true  },
        { "NE", 45.0f,  false },
        { "E",  90.0f,  true  },
        { "SE", 135.0f, false },
        { "S",  180.0f, true  },
        { "SW", 225.0f, false },
        { "W",  270.0f, true  },
        { "NW", 315.0f, false },
    };

    float pixelsPerDeg = barHalfW / 90.0f;  // 90 degrees fills half the bar

    ImU32 majorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f * opacity));
    ImU32 minorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 0.6f * opacity));
    ImU32 tickCol  = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 0.4f * opacity));
    ImU32 northCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.3f, 0.3f, 1.0f * opacity));

    // Draw tick marks every 15 degrees
    for (int deg = 0; deg < 360; deg += 15) {
        float diff = static_cast<float>(deg) - playerYaw;
        // Wrap to [-180, 180]
        while (diff > 180.0f)  diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;

        float screenX = barCenterX + diff * pixelsPerDeg;
        if (screenX < barCenterX - barHalfW || screenX > barCenterX + barHalfW) continue;

        float tickH = (deg % 90 == 0) ? 6.0f : (deg % 45 == 0) ? 4.0f : 2.0f;
        dl->AddLine(ImVec2(screenX, barY + 4.0f), ImVec2(screenX, barY + 4.0f + tickH), tickCol, 1.0f);
    }

    // Draw cardinal/intercardinal labels
    ImFont* font = ImGui::GetFont();
    for (const auto& m : marks) {
        float diff = m.yaw - playerYaw;
        while (diff > 180.0f)  diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;

        float screenX = barCenterX + diff * pixelsPerDeg;
        if (screenX < barCenterX - barHalfW + 10.0f || screenX > barCenterX + barHalfW - 10.0f) continue;

        ImU32 col = (m.label[0] == 'N' && m.label[1] == 0) ? northCol : (m.major ? majorCol : minorCol);
        float fontSize = m.major ? 16.0f : 12.0f;

        ImVec2 textSize = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, m.label);
        dl->AddText(font, fontSize, ImVec2(screenX - textSize.x * 0.5f, barY - 10.0f), col, m.label);
    }

    // Center indicator (small triangle pointing down)
    ImU32 indicatorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f * opacity));
    dl->AddTriangleFilled(
        ImVec2(barCenterX - 4.0f, barY - 13.0f),
        ImVec2(barCenterX + 4.0f, barY - 13.0f),
        ImVec2(barCenterX, barY - 8.0f),
        indicatorCol);

    // Bearing number
    float bearing = playerYaw;
    while (bearing < 0.0f)   bearing += 360.0f;
    while (bearing >= 360.0f) bearing -= 360.0f;
    char bearingText[16];
    snprintf(bearingText, sizeof(bearingText), "%03.0f", bearing);
    ImVec2 bSize = font->CalcTextSizeA(11.0f, 1000.0f, 0.0f, bearingText);
    dl->AddText(font, 11.0f, ImVec2(barCenterX - bSize.x * 0.5f, barY + 5.0f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 0.7f * opacity)), bearingText);
}

// ============================================================
// Health Bar — bottom-left of screen (animated 3-frame sprite)
// ============================================================

void HUD::DrawHealthBar(const Character& character, float screenW, float screenH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;

    float hpFrac = character.GetHealth() / character.GetMaxHealth();
    hpFrac = (hpFrac < 0.0f) ? 0.0f : (hpFrac > 1.0f ? 1.0f : hpFrac);

    // Advance HP bar animation (3 FPS for pixel-art feel)
    float dt = ImGui::GetIO().DeltaTime;
    m_hpAnimTimer += dt;
    const float frameDuration = 1.0f / 1.5f;  // 1.5 frames per second
    while (m_hpAnimTimer >= frameDuration) {
        m_hpAnimTimer -= frameDuration;
        m_hpAnimFrame = (m_hpAnimFrame + 1) % 3;
    }

    Texture* frame = m_hpFrame[m_hpAnimFrame];

    if (frame && frame->IsValid()) {
        // Scale for crisp pixel art
        const float scale  = 1.0f;
        const float frameW = frame->GetWidth()  * scale;
        const float frameH = frame->GetHeight() * scale;

        // Use fixed dimensions so text/position doesn't bounce with animation
        const float fixedH = 82.0f * scale;  // tallest frame height

        // Position: bottom-left with padding (fixed Y so text doesn't move)
        float x = 20.0f;
        float y = screenH - fixedH - 15.0f;

        // Center the current frame vertically within the fixed region
        float frameY = y + (fixedH - frameH) * 0.5f;

        // Fill region within the frame (measured from pixel art)
        const float fillL = 18.0f * scale;
        const float fillR = 210.0f * scale;
        const float fillT = 43.0f * scale;
        const float fillB = 59.0f * scale;

        // Draw red health fill
        if (hpFrac > 0.0f) {
            float fillWidth = (fillR - fillL) * hpFrac;
            ImU32 redCol = IM_COL32(200, 30, 30, (int)(255 * opacity));
            dl->AddRectFilled(
                ImVec2(x + fillL, frameY + fillT),
                ImVec2(x + fillL + fillWidth, frameY + fillB),
                redCol);
        }

        // Draw the animated frame on top (hard switch, no cross-fade for crisp pixel art)
        ImU32 frameCol = IM_COL32(255, 255, 255, (int)(255 * opacity));
        ImTextureID frameTex = (ImTextureID)frame->GetSRV();
        dl->AddImage(frameTex,
                     ImVec2(x, frameY),
                     ImVec2(x + frameW, frameY + frameH),
                     ImVec2(0, 0), ImVec2(1, 1), frameCol);

        // HP text centered on the bar
        ImFont* font = ImGui::GetFont();
        char hpText[32];
        snprintf(hpText, sizeof(hpText), "%.0f / %.0f", character.GetHealth(), character.GetMaxHealth());
        float fontSize = 18.0f;
        ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0, hpText);
        float textX = x + fillL + (fillR - fillL) * 0.5f - textSize.x * 0.5f;
        float textY = frameY + fillT + (fillB - fillT) * 0.5f - textSize.y * 0.5f;
        ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.9f, opacity));
        dl->AddText(font, fontSize, ImVec2(textX, textY), labelCol, hpText);
    } else {
        // Fallback: original rectangle-based health bar
        float barW = 200.0f;
        float barH = 12.0f;
        float x = 20.0f;
        float y = screenH - 40.0f;

        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f * opacity));
        dl->AddRectFilled(ImVec2(x - 2, y - 2), ImVec2(x + barW + 2, y + barH + 2), bgCol, 3.0f);

        float r, g;
        if (hpFrac > 0.5f) { r = 1.0f - (hpFrac - 0.5f) * 2.0f; g = 1.0f; }
        else               { r = 1.0f; g = hpFrac * 2.0f; }
        ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, 0.1f, 0.85f * opacity));
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW * hpFrac, y + barH), fillCol, 2.0f);

        ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 0.6f * opacity));
        dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + barW + 1, y + barH + 1), borderCol, 3.0f);

        ImFont* font = ImGui::GetFont();
        char hpText[32];
        snprintf(hpText, sizeof(hpText), "HP  %.0f / %.0f", character.GetHealth(), character.GetMaxHealth());
        ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.9f, opacity));
        dl->AddText(font, 13.0f, ImVec2(x, y - 17.0f), labelCol, hpText);
    }
}

// ============================================================
// Damage Vignette — red screen overlay when taking damage
// ============================================================

void HUD::DrawDamageVignette(const Character& character, float screenW, float screenH) {
    float flash = character.GetDamageFlash();
    if (flash <= 0.0f) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float alpha = flash * 0.4f;  // Max 40% opacity

    // Draw screen-edge vignette (4 gradient rects from edges)
    float edgeW = screenW * 0.15f;
    float edgeH = screenH * 0.15f;

    ImU32 colFull = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.0f, 0.0f, alpha));
    ImU32 colZero = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.0f, 0.0f, 0.0f));

    // Top edge
    dl->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(screenW, edgeH),
        colFull, colFull, colZero, colZero);
    // Bottom edge
    dl->AddRectFilledMultiColor(
        ImVec2(0, screenH - edgeH), ImVec2(screenW, screenH),
        colZero, colZero, colFull, colFull);
    // Left edge
    dl->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(edgeW, screenH),
        colFull, colZero, colZero, colFull);
    // Right edge
    dl->AddRectFilledMultiColor(
        ImVec2(screenW - edgeW, 0), ImVec2(screenW, screenH),
        colZero, colFull, colFull, colZero);
}

// ============================================================
// Interaction Prompt — centered below crosshair
// ============================================================

void HUD::DrawInteractPrompt(float cx, float cy) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;
    ImFont* font = ImGui::GetFont();

    const char* text = m_interactPrompt.c_str();
    float fontSize = 18.0f;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0, text);

    float tx = cx - textSize.x * 0.5f;
    float ty = cy + 50.0f;

    // Background pill
    float pad = 6.0f;
    ImU32 bgCol = IM_COL32(0, 0, 0, (int)(160 * opacity));
    dl->AddRectFilled(ImVec2(tx - pad, ty - pad),
                      ImVec2(tx + textSize.x + pad, ty + textSize.y + pad),
                      bgCol, 6.0f);

    // Text
    ImU32 textCol = IM_COL32(255, 255, 255, (int)(255 * opacity));
    dl->AddText(font, fontSize, ImVec2(tx, ty), textCol, text);
}

// ============================================================
// World-space animated interact icon billboard
// ============================================================

void HUD::DrawWorldInteractIcons(const Camera& camera, const Scene& scene,
                                  int screenWidth, int screenHeight) {
    if (!m_uiTexturesLoaded) LoadUITextures();
    if (!m_interactIconSheet || !m_interactIconSheet->IsValid()) return;

    // Advance animation (same 1.5 FPS pattern as HP bar / ammo border)
    float dt = ImGui::GetIO().DeltaTime;
    m_interactIconTimer += dt;
    const float frameTime = 1.0f / 1.5f;
    if (m_interactIconTimer >= frameTime) {
        m_interactIconTimer -= frameTime;
        m_interactIconFrame = (m_interactIconFrame + 1) % 3;
    }

    XMMATRIX view = camera.GetViewMatrix();
    XMMATRIX proj = camera.GetProjectionMatrix();
    XMMATRIX viewProj = view * proj;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    // UV coords for current frame (3 frames horizontal in 96x32 sheet)
    float u0 = m_interactIconFrame / 3.0f;
    float u1 = (m_interactIconFrame + 1) / 3.0f;

    // Icon size on screen (2x scale = 64x64)
    const float iconSize = 64.0f;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float opacity = m_settings.hudOpacity;
    ImU32 tintCol = IM_COL32(255, 255, 255, (int)(255 * opacity));

    for (int i = 0; i < scene.GetEntityCount(); i++) {
        const Entity& e = scene.GetEntity(i);
        if (e.interactType == InteractType::None) continue;
        if (!e.visible || e.IsDestroyed()) continue;

        // Skip entity that currently has the text prompt (avoid double-prompting)
        if (i == m_interactTargetIndex) continue;

        // Distance limit — only show icons within 15 units
        float dx = e.position[0] - camera.GetPosition().x;
        float dy = e.position[1] - camera.GetPosition().y;
        float dz = e.position[2] - camera.GetPosition().z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > 15.0f * 15.0f) continue;

        // Entity world position + offset above entity (half scale Y + 0.5)
        float worldY = e.position[1] + e.scale[1] * 0.5f + 0.5f;
        XMVECTOR worldPos = XMVectorSet(e.position[0], worldY, e.position[2], 1.0f);

        // Project to clip space
        XMVECTOR clipPos = XMVector4Transform(worldPos, viewProj);
        float w = XMVectorGetW(clipPos);
        if (w <= 0.0f) continue;  // Behind camera

        float ndcX = XMVectorGetX(clipPos) / w;
        float ndcY = XMVectorGetY(clipPos) / w;

        // Clamp to visible screen area
        if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) continue;

        // NDC to screen coords
        float sx = (ndcX * 0.5f + 0.5f) * sw;
        float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * sh;  // Y is flipped

        // Draw centered icon
        float halfIcon = iconSize * 0.5f;
        ImVec2 pMin(sx - halfIcon, sy - halfIcon);
        ImVec2 pMax(sx + halfIcon, sy + halfIcon);

        dl->AddImage(
            (ImTextureID)m_interactIconSheet->GetSRV(),
            pMin, pMax,
            ImVec2(u0, 0.0f), ImVec2(u1, 1.0f),
            tintCol);
    }
}

} // namespace WT
