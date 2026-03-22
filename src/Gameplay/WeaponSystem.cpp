#include "WeaponSystem.h"
#include "Core/Input.h"
#include "Graphics/Camera.h"
#include "Graphics/DebugRenderer.h"
#include "Physics/PhysicsWorld.h"
#include "AI/AIAgent.h"
#include "Util/MathHelpers.h"
#include "Util/Log.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace WT {

// ============================================================
// Init / Shutdown
// ============================================================

void WeaponSystem::Init() {
    // ============================================================
    // Standard hand/arm scale (shared across all weapons)
    // ============================================================
    const float HAND_SX = 0.05f,  HAND_SY = 0.06f,  HAND_SZ = 0.05f;
    const float ARM_SX  = 0.055f, ARM_SY  = 0.06f,  ARM_SZ  = 0.12f;

    // ---- Rifle (default) ----
    auto& rifle = m_weaponDefs[static_cast<int>(WeaponType::Rifle)];
    rifle.type          = WeaponType::Rifle;
    rifle.fireRate      = 0.12f;
    rifle.damage        = 25.0f;
    rifle.range         = 200.0f;
    rifle.maxAmmo       = 30;
    rifle.reserveAmmo   = 90;
    rifle.reloadTime    = 2.0f;
    rifle.recoilPitch   = 1.5f;
    rifle.recoilYaw     = 0.3f;
    rifle.recoilRecovery = 6.0f;
    rifle.spread        = 0.5f;
    rifle.adsSpreadMult = 0.3f;
    rifle.pelletsPerShot = 1;
    rifle.automatic     = true;
    rifle.barrelLength  = 0.6f;
    rifle.barrelWidth   = 0.06f;
    rifle.stockLength   = 0.25f;
    rifle.bodyWidth     = 0.1f;
    rifle.bodyHeight    = 0.12f;
    rifle.gunModelName  = "Gun/AssaultRifle1";
    rifle.gunTextureName = "Guns/Palette -Gun";
    rifle.modelScale    = 1.0f;
    rifle.modelOffsetX  = -0.14f;
    rifle.modelOffsetY  = 0.07f;
    rifle.modelOffsetZ  = -0.19f;
    rifle.modelRotX     = 3.0f;
    rifle.modelRotY     = 3.0f;
    rifle.armOffsetX    = -0.14f;  // Match model offset for initial positioning
    rifle.armOffsetY    = 0.07f;
    rifle.armOffsetZ    = -0.19f;
    // Rifle hands — two-handed grip, relative to vmBase + armOffset
    rifle.rightHandFwd   = -0.02f;
    rifle.rightHandRight =  0.01f;
    rifle.rightHandDown  =  0.07f;
    rifle.rightHandScaleX = HAND_SX; rifle.rightHandScaleY = HAND_SY; rifle.rightHandScaleZ = HAND_SZ;
    rifle.rightArmFwd    = -0.10f;
    rifle.rightArmRight  =  0.02f;
    rifle.rightArmDown   =  0.05f;
    rifle.rightArmPitch  =  12.0f;
    rifle.rightArmScaleX = ARM_SX; rifle.rightArmScaleY = ARM_SY; rifle.rightArmScaleZ = ARM_SZ;
    rifle.leftHandFwd    =  0.15f;
    rifle.leftHandRight  =  0.00f;
    rifle.leftHandDown   =  0.04f;
    rifle.leftHandScaleX = HAND_SX; rifle.leftHandScaleY = HAND_SY; rifle.leftHandScaleZ = HAND_SZ;
    rifle.leftArmFwd     =  0.06f;
    rifle.leftArmRight   = -0.02f;
    rifle.leftArmDown    =  0.02f;
    rifle.leftArmPitch   = -5.0f;
    rifle.leftArmScaleX  = ARM_SX; rifle.leftArmScaleY = ARM_SY; rifle.leftArmScaleZ = ARM_SZ;

    // Rifle muzzle flash: sharp directional burst — bright core + elongated forward spike + side flares
    rifle.flashDuration     = 0.045f;
    rifle.flashLayerCount   = 4;
    // Layer 0: Hot white core (small, bright)
    rifle.flashLayers[0] = { 0.025f, 0.025f, 0.03f,  0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.95f,  1.2f, 0.3f };
    // Layer 1: Forward spike (elongated along barrel)
    rifle.flashLayers[1] = { 0.015f, 0.015f, 0.08f,  0.03f, 0.0f, 0.0f, 0.0f,   1.0f, 0.85f, 0.3f,  1.0f, 0.6f };
    // Layer 2: Side flare (horizontal, rotated 90 deg)
    rifle.flashLayers[2] = { 0.04f, 0.012f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };
    // Layer 3: Vertical flare (cross pattern with horizontal)
    rifle.flashLayers[3] = { 0.012f, 0.04f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };

    // ---- Pistol ----
    auto& pistol = m_weaponDefs[static_cast<int>(WeaponType::Pistol)];
    pistol.type          = WeaponType::Pistol;
    pistol.fireRate      = 0.2f;
    pistol.damage        = 35.0f;
    pistol.range         = 100.0f;
    pistol.maxAmmo       = 12;
    pistol.reserveAmmo   = 36;
    pistol.reloadTime    = 1.5f;
    pistol.recoilPitch   = 3.0f;
    pistol.recoilYaw     = 0.5f;
    pistol.recoilRecovery = 8.0f;
    pistol.spread        = 0.8f;
    pistol.adsSpreadMult = 0.4f;
    pistol.pelletsPerShot = 1;
    pistol.automatic     = false;
    pistol.barrelLength  = 0.3f;
    pistol.barrelWidth   = 0.04f;
    pistol.stockLength   = 0.0f;
    pistol.bodyWidth     = 0.08f;
    pistol.bodyHeight    = 0.14f;
    pistol.gunModelName  = "Gun/Pistol1";
    pistol.gunTextureName = "Guns/Palette -Gun";
    pistol.modelScale    = 1.0f;
    pistol.modelOffsetX  = -0.13f;
    pistol.modelOffsetY  =  0.07f;
    pistol.modelOffsetZ  = -0.13f;
    pistol.modelRotX     = 4.0f;
    pistol.modelRotY     = 0.0f;
    pistol.armOffsetX    = -0.13f;
    pistol.armOffsetY    =  0.07f;
    pistol.armOffsetZ    = -0.13f;
    // Pistol hands — right hand on grip, left hand wraps under for support
    pistol.rightHandFwd   = -0.01f;
    pistol.rightHandRight =  0.01f;
    pistol.rightHandDown  =  0.05f;
    pistol.rightHandScaleX = HAND_SX; pistol.rightHandScaleY = HAND_SY; pistol.rightHandScaleZ = HAND_SZ;
    pistol.rightArmFwd    = -0.09f;
    pistol.rightArmRight  =  0.03f;
    pistol.rightArmDown   =  0.03f;
    pistol.rightArmPitch  =  15.0f;
    pistol.rightArmScaleX = ARM_SX; pistol.rightArmScaleY = ARM_SY; pistol.rightArmScaleZ = ARM_SZ;
    pistol.leftHandFwd    =  0.01f;
    pistol.leftHandRight  = -0.02f;
    pistol.leftHandDown   =  0.05f;
    pistol.leftHandScaleX = HAND_SX; pistol.leftHandScaleY = HAND_SY; pistol.leftHandScaleZ = HAND_SZ;
    pistol.leftArmFwd     = -0.05f;
    pistol.leftArmRight   = -0.04f;
    pistol.leftArmDown    =  0.03f;
    pistol.leftArmPitch   =  5.0f;
    pistol.leftArmScaleX  = ARM_SX; pistol.leftArmScaleY = ARM_SY; pistol.leftArmScaleZ = ARM_SZ;

    // Pistol muzzle flash: punchy round burst — bright core + wide round bloom
    pistol.flashDuration     = 0.04f;
    pistol.flashLayerCount   = 3;
    // Layer 0: Hot white-yellow core
    pistol.flashLayers[0] = { 0.02f, 0.02f, 0.02f,   0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.9f,   1.3f, 0.2f };
    // Layer 1: Round orange bloom (equal XY, shallow Z)
    pistol.flashLayers[1] = { 0.035f, 0.035f, 0.01f,  0.0f, 0.0f, 0.0f, 45.0f,  1.0f, 0.7f, 0.15f,  1.0f, 0.8f };
    // Layer 2: Tiny forward spit
    pistol.flashLayers[2] = { 0.01f, 0.01f, 0.04f,    0.015f, 0.0f, 0.0f, 0.0f,  1.0f, 0.9f, 0.4f,   1.5f, 0.5f };

    // ---- Rifle3 ----
    auto& rifle3 = m_weaponDefs[static_cast<int>(WeaponType::Rifle3)];
    rifle3.type          = WeaponType::Rifle3;
    rifle3.fireRate      = 0.11f;
    rifle3.damage        = 26.0f;
    rifle3.range         = 185.0f;
    rifle3.maxAmmo       = 30;
    rifle3.reserveAmmo   = 90;
    rifle3.reloadTime    = 2.1f;
    rifle3.recoilPitch   = 1.6f;
    rifle3.recoilYaw     = 0.35f;
    rifle3.recoilRecovery = 6.5f;
    rifle3.spread        = 0.45f;
    rifle3.adsSpreadMult = 0.28f;
    rifle3.pelletsPerShot = 1;
    rifle3.automatic     = true;
    rifle3.barrelLength  = 0.6f;
    rifle3.barrelWidth   = 0.06f;
    rifle3.stockLength   = 0.25f;
    rifle3.bodyWidth     = 0.1f;
    rifle3.bodyHeight    = 0.12f;
    rifle3.gunModelName  = "Gun/Rifle3";
    rifle3.gunTextureName = "Guns/Rifle3";
    rifle3.modelScale    = 1.0f;
    rifle3.modelOffsetX  = -0.14f;
    rifle3.modelOffsetY  = 0.07f;
    rifle3.modelOffsetZ  = -0.19f;
    rifle3.modelRotX     = 3.0f;
    rifle3.modelRotY     = 3.0f;
    rifle3.armOffsetX    = -0.14f;
    rifle3.armOffsetY    = 0.07f;
    rifle3.armOffsetZ    = -0.19f;
    // Rifle3 hands — two-handed grip
    rifle3.rightHandFwd   = -0.02f;
    rifle3.rightHandRight =  0.01f;
    rifle3.rightHandDown  =  0.07f;
    rifle3.rightHandScaleX = HAND_SX; rifle3.rightHandScaleY = HAND_SY; rifle3.rightHandScaleZ = HAND_SZ;
    rifle3.rightArmFwd    = -0.10f;
    rifle3.rightArmRight  =  0.02f;
    rifle3.rightArmDown   =  0.05f;
    rifle3.rightArmPitch  =  12.0f;
    rifle3.rightArmScaleX = ARM_SX; rifle3.rightArmScaleY = ARM_SY; rifle3.rightArmScaleZ = ARM_SZ;
    rifle3.leftHandFwd    =  0.15f;
    rifle3.leftHandRight  =  0.00f;
    rifle3.leftHandDown   =  0.04f;
    rifle3.leftHandScaleX = HAND_SX; rifle3.leftHandScaleY = HAND_SY; rifle3.leftHandScaleZ = HAND_SZ;
    rifle3.leftArmFwd     =  0.06f;
    rifle3.leftArmRight   = -0.02f;
    rifle3.leftArmDown    =  0.02f;
    rifle3.leftArmPitch   = -5.0f;
    rifle3.leftArmScaleX  = ARM_SX; rifle3.leftArmScaleY = ARM_SY; rifle3.leftArmScaleZ = ARM_SZ;

    // Rifle3 muzzle flash: sharp directional burst
    rifle3.flashDuration     = 0.045f;
    rifle3.flashLayerCount   = 4;
    rifle3.flashLayers[0] = { 0.025f, 0.025f, 0.03f,  0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.95f,  1.2f, 0.3f };
    rifle3.flashLayers[1] = { 0.015f, 0.015f, 0.08f,  0.03f, 0.0f, 0.0f, 0.0f,   1.0f, 0.85f, 0.3f,  1.0f, 0.6f };
    rifle3.flashLayers[2] = { 0.04f, 0.012f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };
    rifle3.flashLayers[3] = { 0.012f, 0.04f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };

    // ---- SG553 ----
    auto& sg553 = m_weaponDefs[static_cast<int>(WeaponType::SG553)];
    sg553.type          = WeaponType::SG553;
    sg553.fireRate      = 0.1f;
    sg553.damage        = 28.0f;
    sg553.range         = 180.0f;
    sg553.maxAmmo       = 30;
    sg553.reserveAmmo   = 90;
    sg553.reloadTime    = 2.2f;
    sg553.recoilPitch   = 1.8f;
    sg553.recoilYaw     = 0.4f;
    sg553.recoilRecovery = 5.5f;
    sg553.spread        = 0.4f;
    sg553.adsSpreadMult = 0.25f;
    sg553.pelletsPerShot = 1;
    sg553.automatic     = true;
    sg553.barrelLength  = 0.55f;
    sg553.barrelWidth   = 0.06f;
    sg553.stockLength   = 0.22f;
    sg553.bodyWidth     = 0.1f;
    sg553.bodyHeight    = 0.12f;
    sg553.gunModelName  = "Gun/Sg553";
    sg553.gunTextureName = "Guns/Sg553";
    sg553.modelScale    = 1.0f;
    sg553.modelOffsetX  = -0.14f;
    sg553.modelOffsetY  = 0.07f;
    sg553.modelOffsetZ  = -0.19f;
    sg553.modelRotX     = 3.0f;
    sg553.modelRotY     = 3.0f;
    sg553.armOffsetX    = -0.14f;
    sg553.armOffsetY    = 0.07f;
    sg553.armOffsetZ    = -0.19f;
    sg553.rightHandFwd   = -0.02f;
    sg553.rightHandRight =  0.01f;
    sg553.rightHandDown  =  0.07f;
    sg553.rightHandScaleX = HAND_SX; sg553.rightHandScaleY = HAND_SY; sg553.rightHandScaleZ = HAND_SZ;
    sg553.rightArmFwd    = -0.10f;
    sg553.rightArmRight  =  0.02f;
    sg553.rightArmDown   =  0.05f;
    sg553.rightArmPitch  =  12.0f;
    sg553.rightArmScaleX = ARM_SX; sg553.rightArmScaleY = ARM_SY; sg553.rightArmScaleZ = ARM_SZ;
    sg553.leftHandFwd    =  0.15f;
    sg553.leftHandRight  =  0.00f;
    sg553.leftHandDown   =  0.04f;
    sg553.leftHandScaleX = HAND_SX; sg553.leftHandScaleY = HAND_SY; sg553.leftHandScaleZ = HAND_SZ;
    sg553.leftArmFwd     =  0.06f;
    sg553.leftArmRight   = -0.02f;
    sg553.leftArmDown    =  0.02f;
    sg553.leftArmPitch   = -5.0f;
    sg553.leftArmScaleX  = ARM_SX; sg553.leftArmScaleY = ARM_SY; sg553.leftArmScaleZ = ARM_SZ;
    sg553.flashDuration     = 0.05f;
    sg553.flashLayerCount   = 4;
    sg553.flashLayers[0] = { 0.025f, 0.025f, 0.03f,  0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.95f,  1.2f, 0.3f };
    sg553.flashLayers[1] = { 0.015f, 0.015f, 0.08f,  0.03f, 0.0f, 0.0f, 0.0f,   1.0f, 0.85f, 0.3f,  1.0f, 0.6f };
    sg553.flashLayers[2] = { 0.04f, 0.012f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };
    sg553.flashLayers[3] = { 0.012f, 0.04f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };

    // ---- M4A1 ----
    auto& m4a1 = m_weaponDefs[static_cast<int>(WeaponType::M4A1)];
    m4a1.type          = WeaponType::M4A1;
    m4a1.fireRate      = 0.09f;
    m4a1.damage        = 23.0f;
    m4a1.range         = 190.0f;
    m4a1.maxAmmo       = 30;
    m4a1.reserveAmmo   = 90;
    m4a1.reloadTime    = 1.9f;
    m4a1.recoilPitch   = 1.3f;
    m4a1.recoilYaw     = 0.25f;
    m4a1.recoilRecovery = 7.0f;
    m4a1.spread        = 0.45f;
    m4a1.adsSpreadMult = 0.28f;
    m4a1.pelletsPerShot = 1;
    m4a1.automatic     = true;
    m4a1.barrelLength  = 0.5f;
    m4a1.barrelWidth   = 0.06f;
    m4a1.stockLength   = 0.24f;
    m4a1.bodyWidth     = 0.1f;
    m4a1.bodyHeight    = 0.12f;
    m4a1.gunModelName  = "Gun/m4a1";
    m4a1.gunTextureName = "Guns/m4a1";
    m4a1.modelScale    = 1.0f;
    m4a1.modelOffsetX  = -0.14f;
    m4a1.modelOffsetY  = 0.07f;
    m4a1.modelOffsetZ  = -0.19f;
    m4a1.modelRotX     = 3.0f;
    m4a1.modelRotY     = 3.0f;
    m4a1.armOffsetX    = -0.14f;
    m4a1.armOffsetY    = 0.07f;
    m4a1.armOffsetZ    = -0.19f;
    m4a1.rightHandFwd   = -0.02f;
    m4a1.rightHandRight =  0.01f;
    m4a1.rightHandDown  =  0.07f;
    m4a1.rightHandScaleX = HAND_SX; m4a1.rightHandScaleY = HAND_SY; m4a1.rightHandScaleZ = HAND_SZ;
    m4a1.rightArmFwd    = -0.10f;
    m4a1.rightArmRight  =  0.02f;
    m4a1.rightArmDown   =  0.05f;
    m4a1.rightArmPitch  =  12.0f;
    m4a1.rightArmScaleX = ARM_SX; m4a1.rightArmScaleY = ARM_SY; m4a1.rightArmScaleZ = ARM_SZ;
    m4a1.leftHandFwd    =  0.15f;
    m4a1.leftHandRight  =  0.00f;
    m4a1.leftHandDown   =  0.04f;
    m4a1.leftHandScaleX = HAND_SX; m4a1.leftHandScaleY = HAND_SY; m4a1.leftHandScaleZ = HAND_SZ;
    m4a1.leftArmFwd     =  0.06f;
    m4a1.leftArmRight   = -0.02f;
    m4a1.leftArmDown    =  0.02f;
    m4a1.leftArmPitch   = -5.0f;
    m4a1.leftArmScaleX  = ARM_SX; m4a1.leftArmScaleY = ARM_SY; m4a1.leftArmScaleZ = ARM_SZ;
    m4a1.flashDuration     = 0.04f;
    m4a1.flashLayerCount   = 4;
    m4a1.flashLayers[0] = { 0.02f, 0.02f, 0.025f,  0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.95f,  1.0f, 0.25f };
    m4a1.flashLayers[1] = { 0.012f, 0.012f, 0.07f,  0.025f, 0.0f, 0.0f, 0.0f,  1.0f, 0.85f, 0.3f,  0.9f, 0.5f };
    m4a1.flashLayers[2] = { 0.035f, 0.01f, 0.012f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.3f, 0.35f };
    m4a1.flashLayers[3] = { 0.01f, 0.035f, 0.012f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.3f, 0.35f };

    // Start with rifle
    SwitchWeapon(WeaponType::Rifle);

    LOG_INFO("WeaponSystem initialized (%d weapon types)", static_cast<int>(WeaponType::Count));
}

void WeaponSystem::Shutdown() {
    m_viewmodelParts.clear();
    LOG_INFO("WeaponSystem shutdown");
}

// ============================================================
// Weapon Presets — Save / Load
// ============================================================

static std::string GetPresetFilename(WeaponType type) {
    switch (type) {
        case WeaponType::Rifle:   return "rifle.wpn";
        case WeaponType::Pistol:  return "pistol.wpn";
        case WeaponType::Rifle3:  return "rifle3.wpn";
        case WeaponType::SG553:   return "sg553.wpn";
        case WeaponType::M4A1:    return "m4a1.wpn";
        default:                  return "unknown.wpn";
    }
}

bool WeaponSystem::SaveWeaponPreset(WeaponType type) {
    if (m_presetsDir.empty()) return false;
    namespace fs = std::filesystem;

    fs::create_directories(m_presetsDir);
    std::string filename = GetPresetFilename(type);
    fs::path filepath = fs::path(m_presetsDir) / filename;
    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to save weapon preset: %s", filepath.string().c_str());
        return false;
    }

    const auto& d = m_weaponDefs[static_cast<int>(type)];

    file << "# War Times Weapon Preset\n";
    file << "# " << WeaponTypeName(type) << "\n\n";

    // Combat stats
    file << "fire_rate = " << d.fireRate << "\n";
    file << "damage = " << d.damage << "\n";
    file << "range = " << d.range << "\n";
    file << "max_ammo = " << d.maxAmmo << "\n";
    file << "reserve_ammo = " << d.reserveAmmo << "\n";
    file << "reload_time = " << d.reloadTime << "\n";
    file << "recoil_pitch = " << d.recoilPitch << "\n";
    file << "recoil_yaw = " << d.recoilYaw << "\n";
    file << "recoil_recovery = " << d.recoilRecovery << "\n";
    file << "spread = " << d.spread << "\n";
    file << "ads_spread_mult = " << d.adsSpreadMult << "\n";
    file << "pellets_per_shot = " << d.pelletsPerShot << "\n";
    file << "automatic = " << (d.automatic ? 1 : 0) << "\n\n";

    // Damage falloff
    file << "damage_dropoff_start = " << d.damageDropoffStart << "\n";
    file << "damage_dropoff_end = " << d.damageDropoffEnd << "\n";
    file << "damage_min_mult = " << d.damageMinMult << "\n\n";

    // Spread modifiers
    file << "move_spread_mult = " << d.moveSpreadMult << "\n";
    file << "sprint_spread_mult = " << d.sprintSpreadMult << "\n";
    file << "jump_spread_mult = " << d.jumpSpreadMult << "\n";
    file << "crouch_spread_mult = " << d.crouchSpreadMult << "\n";
    file << "first_shot_spread_mult = " << d.firstShotSpreadMult << "\n";
    file << "spread_recovery_rate = " << d.spreadRecoveryRate << "\n\n";

    // Recoil pattern
    file << "first_shot_recoil_mult = " << d.firstShotRecoilMult << "\n";
    file << "recoil_pattern_length = " << d.recoilPatternLength << "\n";
    if (d.recoilPatternLength > 0) {
        file << "recoil_pattern =";
        for (int i = 0; i < d.recoilPatternLength; i++)
            file << " " << d.recoilPattern[i];
        file << "\n";
    }

    // Equip
    file << "equip_time = " << d.equipTime << "\n\n";

    // Gun model
    file << "gun_model = " << d.gunModelName << "\n";
    file << "gun_texture = " << d.gunTextureName << "\n";
    file << "model_scale = " << d.modelScale << "\n";
    file << "model_offset = " << d.modelOffsetX << " " << d.modelOffsetY << " " << d.modelOffsetZ << "\n";
    file << "model_rotation = " << d.modelRotX << " " << d.modelRotY << " " << d.modelRotZ << "\n";
    file << "arm_offset = " << d.armOffsetX << " " << d.armOffsetY << " " << d.armOffsetZ << "\n";
    file << "barrel_length = " << d.barrelLength << "\n";
    file << "laser_offset = " << d.laserOffsetX << " " << d.laserOffsetY << " " << d.laserOffsetZ << "\n\n";

    // Grip sockets
    file << "right_grip = " << d.rightGripSocket.x << " " << d.rightGripSocket.y << " " << d.rightGripSocket.z << "\n";
    file << "right_grip_rot = " << d.rightGripRotation.x << " " << d.rightGripRotation.y << " " << d.rightGripRotation.z << "\n";
    file << "left_grip = " << d.leftGripSocket.x << " " << d.leftGripSocket.y << " " << d.leftGripSocket.z << "\n";
    file << "left_grip_rot = " << d.leftGripRotation.x << " " << d.leftGripRotation.y << " " << d.leftGripRotation.z << "\n";
    file << "elbow_pole = " << d.elbowPoleOffset.x << " " << d.elbowPoleOffset.y << " " << d.elbowPoleOffset.z << "\n\n";

    // Muzzle flash
    file << "flash_duration = " << d.flashDuration << "\n";
    file << "flash_offset = " << d.flashOffsetX << " " << d.flashOffsetY << " " << d.flashOffsetZ << "\n";
    file << "flash_layer_count = " << d.flashLayerCount << "\n";
    for (int i = 0; i < d.flashLayerCount; i++) {
        const auto& fl = d.flashLayers[i];
        file << "flash_layer_" << i << " = "
             << fl.scaleX << " " << fl.scaleY << " " << fl.scaleZ << " "
             << fl.offsetFwd << " " << fl.offsetUp << " " << fl.offsetRight << " "
             << fl.rollDeg << " " << fl.r << " " << fl.g << " " << fl.b << " "
             << fl.fadeSpeed << " " << fl.growSpeed << "\n";
    }

    file.close();
    LOG_INFO("Saved weapon preset: %s", filepath.string().c_str());
    return true;
}

bool WeaponSystem::LoadWeaponPreset(WeaponType type) {
    if (m_presetsDir.empty()) return false;
    namespace fs = std::filesystem;

    std::string filename = GetPresetFilename(type);
    fs::path filepath = fs::path(m_presetsDir) / filename;
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    auto& d = m_weaponDefs[static_cast<int>(type)];
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Trim whitespace
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());

        std::istringstream ss(val);

        if      (key == "fire_rate")        ss >> d.fireRate;
        else if (key == "damage")           ss >> d.damage;
        else if (key == "range")            ss >> d.range;
        else if (key == "max_ammo")         ss >> d.maxAmmo;
        else if (key == "reserve_ammo")     ss >> d.reserveAmmo;
        else if (key == "reload_time")      ss >> d.reloadTime;
        else if (key == "recoil_pitch")     ss >> d.recoilPitch;
        else if (key == "recoil_yaw")       ss >> d.recoilYaw;
        else if (key == "recoil_recovery")  ss >> d.recoilRecovery;
        else if (key == "spread")           ss >> d.spread;
        else if (key == "ads_spread_mult")  ss >> d.adsSpreadMult;
        else if (key == "pellets_per_shot") ss >> d.pelletsPerShot;
        else if (key == "automatic")        { int v; ss >> v; d.automatic = (v != 0); }
        else if (key == "damage_dropoff_start") ss >> d.damageDropoffStart;
        else if (key == "damage_dropoff_end")   ss >> d.damageDropoffEnd;
        else if (key == "damage_min_mult")      ss >> d.damageMinMult;
        else if (key == "move_spread_mult")     ss >> d.moveSpreadMult;
        else if (key == "sprint_spread_mult")   ss >> d.sprintSpreadMult;
        else if (key == "jump_spread_mult")     ss >> d.jumpSpreadMult;
        else if (key == "crouch_spread_mult")   ss >> d.crouchSpreadMult;
        else if (key == "first_shot_spread_mult") ss >> d.firstShotSpreadMult;
        else if (key == "spread_recovery_rate") ss >> d.spreadRecoveryRate;
        else if (key == "first_shot_recoil_mult") ss >> d.firstShotRecoilMult;
        else if (key == "recoil_pattern_length") ss >> d.recoilPatternLength;
        else if (key == "recoil_pattern") {
            for (int i = 0; i < d.recoilPatternLength && i < WeaponDef::kMaxRecoilPattern; i++)
                ss >> d.recoilPattern[i];
        }
        else if (key == "equip_time")           ss >> d.equipTime;
        else if (key == "gun_model")        d.gunModelName = val;
        else if (key == "gun_texture")      d.gunTextureName = val;
        else if (key == "model_scale")      ss >> d.modelScale;
        else if (key == "model_offset")     ss >> d.modelOffsetX >> d.modelOffsetY >> d.modelOffsetZ;
        else if (key == "model_rotation")   ss >> d.modelRotX >> d.modelRotY >> d.modelRotZ;
        else if (key == "arm_offset")        ss >> d.armOffsetX >> d.armOffsetY >> d.armOffsetZ;
        else if (key == "barrel_length")     ss >> d.barrelLength;
        else if (key == "laser_offset")      ss >> d.laserOffsetX >> d.laserOffsetY >> d.laserOffsetZ;
        else if (key == "right_grip")       ss >> d.rightGripSocket.x >> d.rightGripSocket.y >> d.rightGripSocket.z;
        else if (key == "right_grip_rot")   ss >> d.rightGripRotation.x >> d.rightGripRotation.y >> d.rightGripRotation.z;
        else if (key == "left_grip")        ss >> d.leftGripSocket.x >> d.leftGripSocket.y >> d.leftGripSocket.z;
        else if (key == "left_grip_rot")    ss >> d.leftGripRotation.x >> d.leftGripRotation.y >> d.leftGripRotation.z;
        else if (key == "elbow_pole")       ss >> d.elbowPoleOffset.x >> d.elbowPoleOffset.y >> d.elbowPoleOffset.z;
        else if (key == "flash_duration")       ss >> d.flashDuration;
        else if (key == "flash_offset")         ss >> d.flashOffsetX >> d.flashOffsetY >> d.flashOffsetZ;
        else if (key == "flash_muzzle_offset")  { float v; ss >> v; d.flashOffsetZ = v; } // legacy
        else if (key == "flash_layer_count")    ss >> d.flashLayerCount;
        else if (key.rfind("flash_layer_", 0) == 0) {
            int idx = std::stoi(key.substr(12));
            if (idx >= 0 && idx < WeaponDef::kMaxFlashLayers) {
                auto& fl = d.flashLayers[idx];
                ss >> fl.scaleX >> fl.scaleY >> fl.scaleZ
                   >> fl.offsetFwd >> fl.offsetUp >> fl.offsetRight
                   >> fl.rollDeg >> fl.r >> fl.g >> fl.b
                   >> fl.fadeSpeed >> fl.growSpeed;
            }
        }
    }

    LOG_INFO("Loaded weapon preset: %s", filepath.string().c_str());
    return true;
}

void WeaponSystem::LoadAllPresets() {
    int loaded = 0;
    for (int i = 0; i < static_cast<int>(WeaponType::Count); i++) {
        if (LoadWeaponPreset(static_cast<WeaponType>(i)))
            loaded++;
    }
    if (loaded > 0)
        LOG_INFO("Loaded %d weapon presets from disk", loaded);
}

// ============================================================
// Switch Weapon
// ============================================================

void WeaponSystem::SwitchWeapon(WeaponType type) {
    if (type == m_currentWeapon) return; // Don't re-equip same weapon
    m_currentWeapon = type;
    const auto& def = GetCurrentDef();
    m_currentAmmo  = def.maxAmmo;
    m_reserveAmmo  = def.reserveAmmo;
    m_reloading    = false;
    m_reloadTimer  = 0.0f;
    m_fireTimer    = 0.0f;
    m_muzzleFlashTimer = 0.0f;
    m_recoilPitchAccum = 0.0f;
    m_recoilYawAccum   = 0.0f;
    m_triggerHeld  = false;
    m_equipTimer   = def.equipTime; // Equip delay before can fire
    m_burstShotCount = 0;
    m_currentSpreadAccum = 0.0f;
    LOG_INFO("Switched to %s (%.2fs equip)", WeaponTypeName(type), def.equipTime);
}

// ============================================================
// Update
// ============================================================

void WeaponSystem::Update(float dt, Input& input, Camera& camera,
                           const Character& character, bool editorWantsMouse,
                           PhysicsWorld* physics, AISystem* aiSystem) {
    const auto& def = GetCurrentDef();

    // Clear per-frame flags
    m_justFired = false;
    m_lastHitWasKill = false;
    m_lastHitWasHeadshot = false;

    // ---- Timers ----
    if (m_fireTimer > 0.0f) m_fireTimer -= dt;
    if (m_muzzleFlashTimer > 0.0f) m_muzzleFlashTimer -= dt;
    if (m_hitMarkerTimer > 0.0f) m_hitMarkerTimer -= dt;
    if (m_equipTimer > 0.0f) m_equipTimer -= dt;

    // ---- Burst shot tracking (reset after pause) ----
    m_burstResetTimer += dt;
    if (m_burstResetTimer > def.fireRate * 2.5f) {
        m_burstShotCount = 0; // Reset if not firing for a while
    }

    // ---- Spread recovery ----
    if (m_currentSpreadAccum > 0.0f) {
        m_currentSpreadAccum -= def.spreadRecoveryRate * dt;
        if (m_currentSpreadAccum < 0.0f) m_currentSpreadAccum = 0.0f;
    }

    // ---- Reloading ----
    if (m_reloading) {
        m_reloadTimer -= dt;
        if (m_reloadTimer <= 0.0f) {
            int needed = def.maxAmmo - m_currentAmmo;
            int available = (m_reserveAmmo >= needed) ? needed : m_reserveAmmo;
            m_currentAmmo += available;
            m_reserveAmmo -= available;
            m_reloading = false;
            m_reloadTimer = 0.0f;
        }
    }

    // ---- ADS (right mouse) ----
    m_adsActive = !editorWantsMouse && input.IsRightMouseDown();

    // ---- Sprint tracking ----
    m_sprintActive = character.IsSprinting();

    // ---- Cache character state for spread modifiers ----
    m_character_grounded  = character.IsGrounded();
    m_character_crouching = character.IsCrouching();
    {
        XMFLOAT3 vel = character.GetVelocity();
        float hSpeedSq = vel.x * vel.x + vel.z * vel.z;
        m_character_moving = hSpeedSq > 0.5f; // Moving if horizontal speed > ~0.7 m/s
    }

    // ---- Weapon switching (1-5 keys) ----
    if (input.IsKeyPressed('1')) SwitchWeapon(WeaponType::Rifle);
    if (input.IsKeyPressed('2')) SwitchWeapon(WeaponType::Pistol);
    if (input.IsKeyPressed('3')) SwitchWeapon(WeaponType::Rifle3);
    if (input.IsKeyPressed('4')) SwitchWeapon(WeaponType::SG553);
    if (input.IsKeyPressed('5')) SwitchWeapon(WeaponType::M4A1);

    // ---- Reload (R key) ----
    if (input.IsKeyPressed('R') && !m_reloading && m_currentAmmo < def.maxAmmo && m_reserveAmmo > 0) {
        Reload();
    }

    // ---- Firing (blocked during equip, reload, sprint) ----
    if (!editorWantsMouse && !m_reloading && m_equipTimer <= 0.0f && !m_sprintActive) {
        bool wantFire = false;
        if (def.automatic) {
            wantFire = input.IsLeftMouseDown();
        } else {
            // Semi-auto: fire only on press (not hold)
            if (input.IsLeftMousePressed()) {
                wantFire = true;
                m_triggerHeld = true;
            }
            if (!input.IsLeftMouseDown()) {
                m_triggerHeld = false;
            }
        }

        if (wantFire && m_fireTimer <= 0.0f) {
            if (m_currentAmmo > 0) {
                Fire(camera, physics, aiSystem);
            } else if (m_reserveAmmo > 0) {
                // Auto-reload when empty
                Reload();
            }
        }
    }

    // ---- Recoil recovery ----
    UpdateRecoil(dt);

    // ---- Update viewmodel transforms ----
    UpdateViewmodel(dt, camera, character);
}

// ============================================================
// Fire
// ============================================================

void WeaponSystem::Fire(Camera& camera, PhysicsWorld* physics, AISystem* aiSystem) {
    const auto& def = GetCurrentDef();

    m_currentAmmo--;
    m_fireTimer = def.fireRate;
    m_muzzleFlashTimer = def.flashLayerCount > 0 ? def.flashDuration : m_settings.muzzleFlashDuration;
    m_justFired = true;
    m_burstResetTimer = 0.0f;
    m_burstShotCount++;

    // Reset last hit
    m_lastHit = {};

    // Calculate fire direction from camera center
    XMFLOAT3 origin = camera.GetPosition();
    XMFLOAT3 forward = camera.GetForward();

    // Cache muzzle info for external FX (GPU particles, etc.)
    {
        XMVECTOR fwd = XMLoadFloat3(&forward);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd));
        XMVECTOR up = XMVector3Cross(fwd, right);

        float baseX = m_settings.viewmodelOffsetX;
        float baseY = m_settings.viewmodelOffsetY;
        float baseZ = m_settings.viewmodelOffsetZ;

        float anchorX = baseX, anchorY = baseY, anchorZ = baseZ;
        if (!def.gunModelName.empty()) {
            anchorX += def.modelOffsetX;
            anchorY += def.modelOffsetY;
            anchorZ += def.modelOffsetZ;
        }

        float tipX = anchorX + def.flashOffsetX;
        float tipY = anchorY + def.flashOffsetY - def.bodyHeight * 0.15f;
        float tipZ = anchorZ + def.barrelLength + def.flashOffsetZ;

        XMVECTOR muzzle = XMLoadFloat3(&origin)
            + fwd * tipZ + right * tipX + up * tipY;
        XMStoreFloat3(&m_lastMuzzlePos, muzzle);
    }
    m_lastFireDir = forward;

    // ---- Compute effective spread ----
    float spreadDeg = def.spread;

    // First shot accuracy bonus
    if (m_burstShotCount <= 1) {
        spreadDeg *= def.firstShotSpreadMult;
    }

    // ADS reduction
    if (m_adsActive) spreadDeg *= def.adsSpreadMult;

    // Stance/movement modifiers (stacking)
    if (!m_character_grounded) {
        spreadDeg *= def.jumpSpreadMult;
    } else if (m_character_crouching) {
        spreadDeg *= def.crouchSpreadMult;
    } else if (m_character_moving) {
        spreadDeg *= def.moveSpreadMult;
    }

    // Accumulated spread from sustained fire
    spreadDeg += m_currentSpreadAccum;
    m_currentSpreadAccum += def.spread * 0.15f; // Each shot adds spread

    // Fire pellets
    for (int p = 0; p < def.pelletsPerShot; p++) {
        float spreadRad = spreadDeg * DEG_TO_RAD;
        float rndYaw   = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spreadRad;
        float rndPitch = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spreadRad;

        XMVECTOR fwd = XMLoadFloat3(&forward);
        XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd);
        right = XMVector3Normalize(right);
        XMVECTOR up = XMVector3Cross(fwd, right);

        XMVECTOR spreadDir = fwd + right * rndYaw + up * rndPitch;
        spreadDir = XMVector3Normalize(spreadDir);

        XMFLOAT3 dir;
        XMStoreFloat3(&dir, spreadDir);

        auto hitResult = DoRaycast(origin, dir, def.range, physics, aiSystem);

        if (hitResult.hit) {
            if (!m_lastHit.hit || hitResult.distance < m_lastHit.distance) {
                m_lastHit = hitResult;
            }
        }
    }

    // ---- Apply damage with distance falloff ----
    if (m_lastHit.hit && m_lastHit.agentIndex >= 0 && aiSystem) {
        auto& agent = aiSystem->GetAgent(m_lastHit.agentIndex);
        float dmg = def.damage * def.pelletsPerShot;

        // Distance falloff
        float dist = m_lastHit.distance;
        if (dist > def.damageDropoffStart) {
            float falloffRange = def.damageDropoffEnd - def.damageDropoffStart;
            float t = (falloffRange > 0.01f) ? (dist - def.damageDropoffStart) / falloffRange : 1.0f;
            if (t > 1.0f) t = 1.0f;
            float mult = 1.0f - t * (1.0f - def.damageMinMult);
            dmg *= mult;
        }

        if (m_lastHit.isHeadshot) {
            dmg *= 2.0f;
            m_lastHitWasHeadshot = true;
            LOG_INFO("HEADSHOT on '%s'! (%.1f dmg at %.1fm)", agent.name.c_str(), dmg, dist);
        }

        if (agent.type == AIAgentType::Drone && m_lastHit.rotorIndex >= 0) {
            bool destroyed = agent.TakeRotorDamage(m_lastHit.rotorIndex, dmg);
            if (destroyed) {
                LOG_INFO("Rotor %d destroyed on '%s'! (%d rotors remaining)",
                         m_lastHit.rotorIndex, agent.name.c_str(), agent.AliveRotorCount());
            }
            m_hitMarkerTimer = m_settings.hitMarkerDuration;
        } else {
            bool killed = agent.TakeDamage(dmg);
            m_hitMarkerTimer = m_settings.hitMarkerDuration;
            if (killed) {
                m_lastHitWasKill = true;
                LOG_INFO("Agent '%s' eliminated!", agent.name.c_str());
            }
        }
    }

    // ---- Apply recoil (pattern-based or random) ----
    float pitchKick = def.recoilPitch;

    // First shot reduced recoil
    if (m_burstShotCount <= 1) {
        pitchKick *= def.firstShotRecoilMult;
    }

    // Pattern-based recoil: use per-shot multiplier if available
    if (def.recoilPatternLength > 0) {
        int patIdx = (m_burstShotCount - 1) % def.recoilPatternLength;
        pitchKick *= def.recoilPattern[patIdx];
    }

    m_recoilPitchAccum += pitchKick;

    // Horizontal recoil: predictable drift + random jitter
    float patternYaw = 0.0f;
    if (def.recoilPatternLength > 0 && m_burstShotCount > 3) {
        // After 3rd shot, drift slightly right then left (S-pattern)
        int phase = ((m_burstShotCount - 1) / 3) % 2;
        patternYaw = (phase == 0) ? def.recoilYaw * 0.5f : -def.recoilYaw * 0.5f;
    }
    float rndRecoilYaw = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * def.recoilYaw * 0.5f;
    m_recoilYawAccum += patternYaw + rndRecoilYaw;
}

// ============================================================
// Raycast (against physics world + AI agents)
// ============================================================

WeaponHitResult WeaponSystem::DoRaycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
                                         float range, PhysicsWorld* physics, AISystem* aiSystem) {
    WeaponHitResult result;
    float closestDist = range;

    // 1) Raycast physics world (static entities + ground)
    if (physics) {
        auto physHit = physics->Raycast(origin, direction, range);
        if (physHit.hit && physHit.depth < closestDist) {
            closestDist = physHit.depth;
            result.hit = true;
            result.distance = physHit.depth;
            result.hitNormal = physHit.normal;
            result.entityIndex = physHit.entityIndex;
            result.voxelCellIndex = physHit.voxelCellIndex;
            result.hitPosition = {
                origin.x + direction.x * physHit.depth,
                origin.y + direction.y * physHit.depth,
                origin.z + direction.z * physHit.depth
            };
        }
    }

    // 2) Raycast AI agents (manual AABB test)
    if (aiSystem) {
        // Compute inverse direction
        XMFLOAT3 invDir = {
            (fabsf(direction.x) > 1e-8f) ? 1.0f / direction.x : 1e8f,
            (fabsf(direction.y) > 1e-8f) ? 1.0f / direction.y : 1e8f,
            (fabsf(direction.z) > 1e-8f) ? 1.0f / direction.z : 1e8f
        };

        for (int i = 0; i < aiSystem->GetAgentCount(); i++) {
            const auto& agent = aiSystem->GetAgent(i);
            if (!agent.active || !agent.visible) continue;

            float bs = agent.settings.bodyScale;
            bool hitAgent = false;
            float hitT = closestDist;
            bool headshot = false;
            int hitRotorIdx = -1;

            if (agent.type == AIAgentType::Ground) {
                // Ground agent: torso AABB + head sphere
                float bodyW = bs * 0.7f;
                float bodyH = bs * 1.0f;
                float bodyD = bs * 0.5f;
                float headR = bs * 0.35f;

                // ---- Head sphere test (check first for priority) ----
                XMFLOAT3 headCenter = { agent.position.x,
                                        agent.position.y + bodyH + headR,
                                        agent.position.z };
                // Ray-sphere intersection
                float ocx = origin.x - headCenter.x;
                float ocy = origin.y - headCenter.y;
                float ocz = origin.z - headCenter.z;
                float a = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
                float b = 2.0f * (ocx * direction.x + ocy * direction.y + ocz * direction.z);
                float c = ocx * ocx + ocy * ocy + ocz * ocz - headR * headR;
                float disc = b * b - 4.0f * a * c;
                if (disc >= 0.0f) {
                    float sqrtDisc = sqrtf(disc);
                    float t0 = (-b - sqrtDisc) / (2.0f * a);
                    float t1 = (-b + sqrtDisc) / (2.0f * a);
                    float tHead = (t0 > 0.0f) ? t0 : t1;
                    if (tHead > 0.0f && tHead < hitT) {
                        hitT = tHead;
                        hitAgent = true;
                        headshot = true;
                    }
                }

                // ---- Torso AABB test ----
                AABB torsoBox = AABB::FromCenterHalf(
                    { agent.position.x, agent.position.y + bodyH * 0.5f, agent.position.z },
                    { bodyW * 0.5f, bodyH * 0.5f, bodyD * 0.5f }
                );

                float tMin = 0.0f;
                float tMax = hitT; // Only closer than current best

                float t1x = (torsoBox.min.x - origin.x) * invDir.x;
                float t2x = (torsoBox.max.x - origin.x) * invDir.x;
                if (t1x > t2x) { float tmp = t1x; t1x = t2x; t2x = tmp; }
                tMin = (t1x > tMin) ? t1x : tMin;
                tMax = (t2x < tMax) ? t2x : tMax;
                bool torsoHit = (tMin <= tMax);

                if (torsoHit) {
                    float t1y = (torsoBox.min.y - origin.y) * invDir.y;
                    float t2y = (torsoBox.max.y - origin.y) * invDir.y;
                    if (t1y > t2y) { float tmp = t1y; t1y = t2y; t2y = tmp; }
                    tMin = (t1y > tMin) ? t1y : tMin;
                    tMax = (t2y < tMax) ? t2y : tMax;
                    torsoHit = (tMin <= tMax);
                }
                if (torsoHit) {
                    float t1z = (torsoBox.min.z - origin.z) * invDir.z;
                    float t2z = (torsoBox.max.z - origin.z) * invDir.z;
                    if (t1z > t2z) { float tmp = t1z; t1z = t2z; t2z = tmp; }
                    tMin = (t1z > tMin) ? t1z : tMin;
                    tMax = (t2z < tMax) ? t2z : tMax;
                    torsoHit = (tMin <= tMax);
                }

                if (torsoHit && tMin > 0.0f && tMin < hitT) {
                    hitT = tMin;
                    hitAgent = true;
                    headshot = false; // Torso is closer, override headshot
                }
            } else {
                // Drone: test 4 rotor spheres first, then body sphere
                int hitRotor = -1;
                float bestRotorT = hitT;

                // Rotor hit test (4 small spheres at propeller tips)
                float halfScale = bs * 0.5f;
                float armLen = halfScale * 1.4f;
                float propY  = agent.position.y + halfScale * 0.3f;
                float rotorR = bs * 0.2f;  // Rotor hitbox radius
                float yawRad = XMConvertToRadians(agent.yaw);

                for (int p = 0; p < 4; p++) {
                    if (!agent.rotorAlive[p]) continue;
                    float angle = yawRad + (p * 1.5708f) + 0.7854f;
                    float rpx = std::sin(angle) * armLen;
                    float rpz = std::cos(angle) * armLen;
                    float rcx = agent.position.x + rpx;
                    float rcy = propY;
                    float rcz = agent.position.z + rpz;

                    float rocx = origin.x - rcx;
                    float rocy = origin.y - rcy;
                    float rocz = origin.z - rcz;
                    float ra = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
                    float rb = 2.0f * (rocx * direction.x + rocy * direction.y + rocz * direction.z);
                    float rc = rocx * rocx + rocy * rocy + rocz * rocz - rotorR * rotorR;
                    float rdisc = rb * rb - 4.0f * ra * rc;
                    if (rdisc >= 0.0f) {
                        float sqrtD = sqrtf(rdisc);
                        float rt0 = (-rb - sqrtD) / (2.0f * ra);
                        float rt1 = (-rb + sqrtD) / (2.0f * ra);
                        float rtS = (rt0 > 0.0f) ? rt0 : rt1;
                        if (rtS > 0.0f && rtS < bestRotorT) {
                            bestRotorT = rtS;
                            hitRotor = p;
                        }
                    }
                }

                if (hitRotor >= 0) {
                    hitT = bestRotorT;
                    hitAgent = true;
                    hitRotorIdx = hitRotor;
                } else {
                    // Body sphere hitbox (fallback)
                    float ocx = origin.x - agent.position.x;
                    float ocy = origin.y - agent.position.y;
                    float ocz = origin.z - agent.position.z;
                    float a = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
                    float b = 2.0f * (ocx * direction.x + ocy * direction.y + ocz * direction.z);
                    float c = ocx * ocx + ocy * ocy + ocz * ocz - halfScale * halfScale;
                    float disc = b * b - 4.0f * a * c;
                    if (disc >= 0.0f) {
                        float sqrtDisc = sqrtf(disc);
                        float t0 = (-b - sqrtDisc) / (2.0f * a);
                        float t1 = (-b + sqrtDisc) / (2.0f * a);
                        float tSphere = (t0 > 0.0f) ? t0 : t1;
                        if (tSphere > 0.0f && tSphere < hitT) {
                            hitT = tSphere;
                            hitAgent = true;
                        }
                    }
                }
            }

            if (hitAgent && hitT < closestDist) {
                closestDist = hitT;
                result.hit = true;
                result.distance = hitT;
                result.agentIndex = i;
                result.entityIndex = -1;
                result.isHeadshot = headshot;
                result.rotorIndex = hitRotorIdx;
                result.hitPosition = {
                    origin.x + direction.x * hitT,
                    origin.y + direction.y * hitT,
                    origin.z + direction.z * hitT
                };

                // Compute hit normal (approximate from sphere/box surface)
                if (headshot) {
                    float headCY = agent.position.y + bs * 1.0f + bs * 0.35f;
                    float nx = result.hitPosition.x - agent.position.x;
                    float ny = result.hitPosition.y - headCY;
                    float nz = result.hitPosition.z - agent.position.z;
                    float len = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (len > 0.001f) {
                        result.hitNormal = { nx / len, ny / len, nz / len };
                    }
                } else if (agent.type == AIAgentType::Drone) {
                    float nx = result.hitPosition.x - agent.position.x;
                    float ny = result.hitPosition.y - agent.position.y;
                    float nz = result.hitPosition.z - agent.position.z;
                    float len = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (len > 0.001f) {
                        result.hitNormal = { nx / len, ny / len, nz / len };
                    }
                } else {
                    // Torso AABB normal
                    float bodyW = bs * 0.7f;
                    float bodyH = bs * 1.0f;
                    float bodyD = bs * 0.5f;
                    XMFLOAT3 center = { agent.position.x, agent.position.y + bodyH * 0.5f, agent.position.z };
                    XMFLOAT3 half = { bodyW * 0.5f, bodyH * 0.5f, bodyD * 0.5f };
                    float dx = (result.hitPosition.x - center.x) / half.x;
                    float dy = (result.hitPosition.y - center.y) / half.y;
                    float dz = (result.hitPosition.z - center.z) / half.z;
                    float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
                    if (ax > ay && ax > az)
                        result.hitNormal = { dx > 0 ? 1.0f : -1.0f, 0, 0 };
                    else if (ay > az)
                        result.hitNormal = { 0, dy > 0 ? 1.0f : -1.0f, 0 };
                    else
                        result.hitNormal = { 0, 0, dz > 0 ? 1.0f : -1.0f };
                }
            }
        }
    }

    return result;
}

// ============================================================
// Reload
// ============================================================

void WeaponSystem::Reload() {
    const auto& def = GetCurrentDef();
    if (m_currentAmmo >= def.maxAmmo || m_reserveAmmo <= 0) return;
    m_reloading = true;
    m_reloadTimer = def.reloadTime;
    LOG_INFO("Reloading %s...", WeaponTypeName(m_currentWeapon));
}

float WeaponSystem::GetReloadProgress() const {
    if (!m_reloading) return 1.0f;
    const auto& def = GetCurrentDef();
    return 1.0f - (m_reloadTimer / def.reloadTime);
}

// ============================================================
// Recoil
// ============================================================

void WeaponSystem::UpdateRecoil(float dt) {
    const auto& def = GetCurrentDef();
    float recovery = def.recoilRecovery * dt;

    // Recover pitch
    if (m_recoilPitchAccum > 0.0f) {
        m_recoilPitchAccum -= recovery;
        if (m_recoilPitchAccum < 0.0f) m_recoilPitchAccum = 0.0f;
    }

    // Recover yaw
    if (fabsf(m_recoilYawAccum) > 0.01f) {
        float sign = (m_recoilYawAccum > 0.0f) ? 1.0f : -1.0f;
        m_recoilYawAccum -= sign * recovery * 0.5f;
        if (fabsf(m_recoilYawAccum) < 0.01f) m_recoilYawAccum = 0.0f;
    } else {
        m_recoilYawAccum = 0.0f;
    }
}

// ============================================================
// Viewmodel Update
// ============================================================

void WeaponSystem::UpdateViewmodel(float dt, const Camera& camera, const Character& character) {
    m_viewmodelParts.clear();

    const auto& def = GetCurrentDef();
    const auto& s   = m_settings;

    // ---- Viewmodel sway (disabled) ----
    m_swayOffsetX = 0.0f;
    m_swayOffsetY = 0.0f;

    // ---- Base viewmodel position in camera space ----
    XMFLOAT3 camPos = camera.GetPosition();
    XMFLOAT3 camFwd = camera.GetForward();
    XMFLOAT3 camRight = camera.GetRight();
    XMFLOAT3 camUp = camera.GetUp();

    // Combine offsets
    float offX = s.viewmodelOffsetX + m_swayOffsetX;
    float offY = s.viewmodelOffsetY + m_swayOffsetY;
    float offZ = s.viewmodelOffsetZ;

    // Recoil kick (push gun back + up — subtle, matched to arm recoil)
    float recoilKick = m_recoilPitchAccum * 0.001f;
    offY += recoilKick * 0.3f;
    offZ -= recoilKick * 0.5f;

    // Reload animation: lower gun
    if (m_reloading) {
        float reloadProg = GetReloadProgress();
        // Dip down then back up
        float dip = sinf(reloadProg * PI) * 0.15f;
        offY -= dip;
    }

    // Compute world-space base position for viewmodel
    XMFLOAT3 vmBase = {
        camPos.x + camFwd.x * offZ + camRight.x * offX + camUp.x * offY,
        camPos.y + camFwd.y * offZ + camRight.y * offX + camUp.y * offY,
        camPos.z + camFwd.z * offZ + camRight.z * offX + camUp.z * offY
    };

    // Viewmodel rotation (follows camera yaw/pitch)
    float yawDeg = camera.GetYaw() * RAD_TO_DEG;
    float pitchDeg = camera.GetPitch() * RAD_TO_DEG;

    bool useGunModel = !def.gunModelName.empty();

    if (useGunModel) {
        // ---- Gun model mesh (single transform for the whole gun) ----
        // Position at vmBase with model-specific offsets
        XMFLOAT3 modelPos = {
            vmBase.x + camFwd.x * def.modelOffsetZ + camRight.x * def.modelOffsetX + camUp.x * def.modelOffsetY,
            vmBase.y + camFwd.y * def.modelOffsetZ + camRight.y * def.modelOffsetX + camUp.y * def.modelOffsetY,
            vmBase.z + camFwd.z * def.modelOffsetZ + camRight.z * def.modelOffsetX + camUp.z * def.modelOffsetY
        };

        m_viewmodelMesh.position = modelPos;
        m_viewmodelMesh.rotation = { pitchDeg + def.modelRotX, yawDeg + def.modelRotY, def.modelRotZ };
        m_viewmodelMesh.scale = { def.modelScale, def.modelScale, def.modelScale };
        m_viewmodelMesh.meshName = def.gunModelName;
        m_viewmodelMesh.textureName = def.gunTextureName;
        memcpy(m_viewmodelMesh.color, s.gunMetalColor, sizeof(float) * 4);
    } else {
        // Clear mesh info when using cubes
        m_viewmodelMesh.meshName.clear();
    }

    if (!useGunModel) {
        // ---- Cube-based gun parts (fallback when no model loaded) ----

        // Gun receiver (main body)
        {
            ViewmodelPart part;
            part.transform.position = vmBase;
            part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
            part.transform.scale = { def.bodyWidth, def.bodyHeight, def.barrelLength * 0.5f };
            memcpy(part.color, s.gunMetalColor, sizeof(float) * 4);
            m_viewmodelParts.push_back(part);
        }

        // Barrel (forward extension)
        {
            float barrelOffset = def.barrelLength * 0.5f + def.barrelLength * 0.25f;
            XMFLOAT3 barrelPos = {
                vmBase.x + camFwd.x * barrelOffset,
                vmBase.y + camFwd.y * barrelOffset - def.bodyHeight * 0.15f,
                vmBase.z + camFwd.z * barrelOffset
            };

            ViewmodelPart part;
            part.transform.position = barrelPos;
            part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
            part.transform.scale = { def.barrelWidth, def.barrelWidth, def.barrelLength * 0.5f };
            memcpy(part.color, s.gunMetalColor, sizeof(float) * 4);
            m_viewmodelParts.push_back(part);
        }

        // Stock (behind grip)
        if (def.stockLength > 0.01f) {
            float stockOffset = -(def.barrelLength * 0.25f + def.stockLength * 0.5f);
            XMFLOAT3 stockPos = {
                vmBase.x + camFwd.x * stockOffset,
                vmBase.y + camFwd.y * stockOffset,
                vmBase.z + camFwd.z * stockOffset
            };

            ViewmodelPart part;
            part.transform.position = stockPos;
            part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
            part.transform.scale = { def.bodyWidth * 0.8f, def.bodyHeight * 0.9f, def.stockLength * 0.5f };
            memcpy(part.color, s.gunWoodColor, sizeof(float) * 4);
            m_viewmodelParts.push_back(part);
        }
    }

    // ---- Arms/Hands (always rendered) ----
    {

    // Arm base offset (independent from gun model, tunable separately)
    float handBaseX = def.armOffsetX;
    float handBaseY = def.armOffsetY;
    float handBaseZ = def.armOffsetZ;

    // ---- Right hand (grip position) — per-weapon ----
    {
        float hx = handBaseX + def.rightHandRight;
        float hy = handBaseY - def.rightHandDown;
        float hz = handBaseZ + def.rightHandFwd;
        XMFLOAT3 handPos = {
            vmBase.x + camFwd.x * hz + camRight.x * hx + camUp.x * hy,
            vmBase.y + camFwd.y * hz + camRight.y * hx + camUp.y * hy,
            vmBase.z + camFwd.z * hz + camRight.z * hx + camUp.z * hy
        };

        ViewmodelPart part;
        part.transform.position = handPos;
        part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
        part.transform.scale = { def.rightHandScaleX, def.rightHandScaleY, def.rightHandScaleZ };
        memcpy(part.color, s.handColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    // ---- Right forearm — per-weapon ----
    {
        float ax = handBaseX + def.rightArmRight;
        float ay = handBaseY - def.rightArmDown;
        float az = handBaseZ + def.rightArmFwd;
        XMFLOAT3 armPos = {
            vmBase.x + camFwd.x * az + camRight.x * ax + camUp.x * ay,
            vmBase.y + camFwd.y * az + camRight.y * ax + camUp.y * ay,
            vmBase.z + camFwd.z * az + camRight.z * ax + camUp.z * ay
        };

        ViewmodelPart part;
        part.transform.position = armPos;
        part.transform.rotation = { pitchDeg + def.rightArmPitch, yawDeg, 0.0f };
        part.transform.scale = { def.rightArmScaleX, def.rightArmScaleY, def.rightArmScaleZ };
        memcpy(part.color, s.armColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    // ---- Left hand (forward grip / handguard) — per-weapon ----
    {
        float lhx = handBaseX + def.leftHandRight;
        float lhy = handBaseY - def.leftHandDown;
        float lhz = handBaseZ + def.leftHandFwd;
        XMFLOAT3 leftHandPos = {
            vmBase.x + camFwd.x * lhz + camRight.x * lhx + camUp.x * lhy,
            vmBase.y + camFwd.y * lhz + camRight.y * lhx + camUp.y * lhy,
            vmBase.z + camFwd.z * lhz + camRight.z * lhx + camUp.z * lhy
        };

        ViewmodelPart part;
        part.transform.position = leftHandPos;
        part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
        part.transform.scale = { def.leftHandScaleX, def.leftHandScaleY, def.leftHandScaleZ };
        memcpy(part.color, s.handColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    // ---- Left forearm — per-weapon ----
    {
        float lax = handBaseX + def.leftArmRight;
        float lay = handBaseY - def.leftArmDown;
        float laz = handBaseZ + def.leftArmFwd;
        XMFLOAT3 leftArmPos = {
            vmBase.x + camFwd.x * laz + camRight.x * lax + camUp.x * lay,
            vmBase.y + camFwd.y * laz + camRight.y * lax + camUp.y * lay,
            vmBase.z + camFwd.z * laz + camRight.z * lax + camUp.z * lay
        };

        ViewmodelPart part;
        part.transform.position = leftArmPos;
        part.transform.rotation = { pitchDeg + def.leftArmPitch, yawDeg, 0.0f };
        part.transform.scale = { def.leftArmScaleX, def.leftArmScaleY, def.leftArmScaleZ };
        memcpy(part.color, s.armColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    } // end arms/hands block

    // ---- Muzzle flash FX (per-weapon layered flash) ----
    if (m_muzzleFlashTimer > 0.0f) {
        float baseDist = def.barrelLength;

        // For mesh-based weapons, anchor muzzle tip at gun model position + barrel forward
        // For cube-based weapons, anchor at vmBase + barrel forward
        XMFLOAT3 muzzleAnchor = vmBase;
        if (useGunModel) {
            muzzleAnchor = {
                vmBase.x + camFwd.x * def.modelOffsetZ + camRight.x * def.modelOffsetX + camUp.x * def.modelOffsetY,
                vmBase.y + camFwd.y * def.modelOffsetZ + camRight.y * def.modelOffsetX + camUp.y * def.modelOffsetY,
                vmBase.z + camFwd.z * def.modelOffsetZ + camRight.z * def.modelOffsetX + camUp.z * def.modelOffsetY
            };
        }

        // Muzzle tip position with per-weapon flash XYZ offset
        XMFLOAT3 muzzleTip = {
            muzzleAnchor.x + camFwd.x * (baseDist + def.flashOffsetZ) + camRight.x * def.flashOffsetX + camUp.x * def.flashOffsetY,
            muzzleAnchor.y + camFwd.y * (baseDist + def.flashOffsetZ) + camRight.y * def.flashOffsetX + camUp.y * def.flashOffsetY - def.bodyHeight * 0.15f,
            muzzleAnchor.z + camFwd.z * (baseDist + def.flashOffsetZ) + camRight.z * def.flashOffsetX + camUp.z * def.flashOffsetY
        };

        float duration = def.flashDuration > 0.0f ? def.flashDuration : s.muzzleFlashDuration;
        float t = m_muzzleFlashTimer / duration; // 1.0 → 0.0

        if (def.flashLayerCount > 0) {
            // Per-weapon multi-layer flash
            for (int i = 0; i < def.flashLayerCount && i < WeaponDef::kMaxFlashLayers; ++i) {
                const auto& layer = def.flashLayers[i];
                float layerAlpha = t * layer.fadeSpeed;
                if (layerAlpha > 1.0f) layerAlpha = 1.0f;
                if (layerAlpha <= 0.0f) continue;

                float grow = 1.0f + (1.0f - t) * layer.growSpeed;
                float sx = layer.scaleX * grow;
                float sy = layer.scaleY * grow;
                float sz = layer.scaleZ * grow;

                // Position offset from muzzle tip
                XMFLOAT3 layerPos = {
                    muzzleTip.x + camFwd.x * layer.offsetFwd + camUp.x * layer.offsetUp + camRight.x * layer.offsetRight,
                    muzzleTip.y + camFwd.y * layer.offsetFwd + camUp.y * layer.offsetUp + camRight.y * layer.offsetRight,
                    muzzleTip.z + camFwd.z * layer.offsetFwd + camUp.z * layer.offsetUp + camRight.z * layer.offsetRight
                };

                ViewmodelPart part;
                part.transform.position = layerPos;
                part.transform.rotation = { pitchDeg, yawDeg, layer.rollDeg };
                part.transform.scale = { sx, sy, sz };
                part.color[0] = layer.r;
                part.color[1] = layer.g;
                part.color[2] = layer.b;
                part.color[3] = layerAlpha;
                m_viewmodelParts.push_back(part);
            }
        } else {
            // Fallback: single cube flash (legacy)
            float flashSize = s.muzzleFlashScale * (0.5f + t * 0.5f);
            ViewmodelPart part;
            part.transform.position = muzzleTip;
            part.transform.rotation = { pitchDeg, yawDeg, 45.0f };
            part.transform.scale = { flashSize, flashSize, flashSize * 0.5f };
            part.color[0] = s.muzzleFlashColor[0];
            part.color[1] = s.muzzleFlashColor[1];
            part.color[2] = s.muzzleFlashColor[2];
            part.color[3] = t;
            m_viewmodelParts.push_back(part);
        }
    }
}

// ============================================================
// Debug Drawing
// ============================================================

void WeaponSystem::DebugDraw(DebugRenderer& debug) const {
    if (!showDebug) return;

    // Draw last hit position
    if (m_lastHit.hit) {
        debug.DrawSphere(m_lastHit.hitPosition, 0.1f, { 1.0f, 0.0f, 0.0f, 0.8f }, 8);

        // Draw hit normal
        XMFLOAT3 normalEnd = {
            m_lastHit.hitPosition.x + m_lastHit.hitNormal.x * 0.5f,
            m_lastHit.hitPosition.y + m_lastHit.hitNormal.y * 0.5f,
            m_lastHit.hitPosition.z + m_lastHit.hitNormal.z * 0.5f
        };
        debug.DrawLine(m_lastHit.hitPosition, normalEnd, { 0.0f, 1.0f, 0.0f, 0.8f });
    }
}

} // namespace WT
