// Thok's ice beam damages enemies (in the original game it never does).
//
// A beam weapon (bullet with SPEED 0 and NEWBEAM, e.g. Vezok's VEZOK_PLAYER and Thok's THOK_PLAYER)
// keeps one beam alive only while it fires every frame: a frame without a shot "releases" the beam
// (BeamUpdate 0x4aeab0) and the next shot starts a new one in another of the 8 beam slots
// (BeamFire 0x4ae310), evicting the oldest. Damage is dealt in ticks every 0.15 s per beam
// ([0x5d8c14], 0x4b34f0, DAMAGE x 3). Vezok's weapon has "weaponFireDelay 0 0", Thok's
// (CHAR_PIRAKA_ICE) "weaponFireDelay 0 0.01", so Thok doesn't fire every frame (every other one at
// ~120 fps), the 8 slots cycle faster than one tick and no beam ever deals damage. The bullets themselves are identical
// (damage 6, damage type, range).
//
// VisualSetup (0x452cb0) copies a character's weapon (fire delay at +0x104/+0x108, bullet name at
// +0x24) to the player whenever the character is set up or switched to. Before it runs, the mod sets
// the fire delay of a beam weapon to 0. In the original game data only Thok's weapon is affected.

#include "beam_fix.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"

#include <cstring>

namespace
{
// Weapon descriptor (char definition + slot * 0x130 + 0x2b0), as passed to VisualSetup.
constexpr uintptr_t kWeaponBulletName = 0x24;    // const char*
constexpr uintptr_t kWeaponFireDelay = 0x104;    // float, and +0x108 (second value)
// Bullet type (BulletTypes.cfg), stride 0x15c.
constexpr uintptr_t kBulletTypeSize = 0x15c;
constexpr uintptr_t kBulletName = 0x00;          // const char*
constexpr uintptr_t kBulletSpeed = 0xb4;         // float
constexpr uintptr_t kBulletBeam = 0x104;         // NEWBEAM given

using VisualSetup_t = void(__thiscall*)(void* visual, uintptr_t weapon);
VisualSetup_t o_VisualSetup = nullptr;

template <typename T>
T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

uintptr_t FindBulletType(const char* name)
{
    const int count = At<int>(addr::BulletTypeCount, 0);
    for (int i = 0; i < count; ++i)
    {
        const uintptr_t bullet = addr::BulletTypes + i * kBulletTypeSize;
        const char* bulletName = At<const char*>(bullet, kBulletName);
        if (bulletName && _stricmp(bulletName, name) == 0)
            return bullet;
    }
    return 0;
}

void __fastcall hk_VisualSetup(void* visual, void*, uintptr_t weapon)
{
    if (weapon && At<float>(weapon, kWeaponFireDelay) > 0.0f)
    {
        const char* name = At<const char*>(weapon, kWeaponBulletName);
        const uintptr_t bullet = name ? FindBulletType(name) : 0;
        if (bullet && At<uint32_t>(bullet, kBulletBeam) && At<float>(bullet, kBulletSpeed) == 0.0f)
        {
            Log("BeamFix: %s fires every frame (fire delay %.3f -> 0)", name, At<float>(weapon, kWeaponFireDelay));
            At<float>(weapon, kWeaponFireDelay) = 0.0f;
            At<float>(weapon, kWeaponFireDelay + 4) = 0.0f;
        }
    }
    o_VisualSetup(visual, weapon);
}
} // namespace

bool InstallBeamFix()
{
    if (!AddressesFound("BeamFix", {&addr::VisualSetup, &addr::BulletTypes, &addr::BulletTypeCount}))
        return false;
    const HookSpec hook = {"VisualSetup", addr::VisualSetup, reinterpret_cast<void*>(&hk_VisualSetup),
                           reinterpret_cast<void**>(&o_VisualSetup)};
    const bool ok = HooksInit() && InstallHook(hook);
    Log("BeamFix: %s", ok ? "installed" : "FAILED");
    return ok;
}
