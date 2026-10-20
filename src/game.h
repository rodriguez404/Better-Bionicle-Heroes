#pragma once

// Game addresses and structure layouts. Addresses are found at startup by signatures (addresses.h);
// the hex values in the comments are the ones in the NoCD Bionicle.exe (SHA-256 E1DAA3DC...C4A8) where
// they were found in Ghidra. [rt] = also confirmed at runtime. Structure offsets are the same in all
// supported builds (the signatures cover the code that uses them).

#include "addresses.h"

#include <cstddef>
#include <cstdint>

namespace game
{
// Character ids (table at 0x5f97a0).
constexpr int kCharHakann = 6;
constexpr int kCharReidak = 11;
constexpr int kCharVezon = 12;
constexpr int kCharNone = 13;
constexpr int kPirakaSlot = 6;
constexpr int kSlotCount = 7;

// Globals.
inline const uintptr_t& kPlayerPtr = addr::PlayerPtr;  // 0x652cbc Player*                                   [rt]
inline const uintptr_t& kCharMgrPtr = addr::CharMgrPtr;  // 0x64ce84 character manager*                        [rt]
inline const uintptr_t& kLevelsPtr = addr::LevelsPtr;  // 0x64e0f0 +0xc current level, +0xf38 level->zone    [rt]
inline const uintptr_t& kSaveObject = addr::SaveObject;  // 0x652d38 save/progress object (thiscall `this`)
inline const uintptr_t& kCharDefTable = addr::CharDefTable;  // 0x63a7c0 per-char definition, stride 0x690
constexpr uintptr_t kCharDefStride = 0x690;
constexpr uintptr_t kCharDefVisual = 0x2b0;      // visual descriptor inside a char definition

// Player fields.
constexpr uintptr_t kPlayerSlot = 0x12a4;        // int current slot                          [rt]
constexpr uintptr_t kPlayerSlotChar = 0x12ac;    // int[7] slot -> char id                    [rt]
constexpr uintptr_t kPlayerSlotLock = 0x12cc;    // float[7], 0 = available                    [rt]
constexpr uintptr_t kPlayerSwitchCooldown = 0x1404;
constexpr uintptr_t kPlayerFlags690 = 0x690;
constexpr uintptr_t kPlayerSlotVisual = 0x17f0;  // void*[7]                                  [rt]
constexpr uintptr_t kPlayerSwitchSound = 0x1e90;
constexpr uintptr_t kPlayerParty = 0x1eec;       // int[7] rotating ring, [0] = current       [rt]

// Character manager fields.
constexpr uintptr_t kMgrToy = 0x10;              // `this` for LoadPirakaModel                [rt]
constexpr uintptr_t kMgrLoadedPiraka = 0x20;     // char id in the Piraka buffer              [rt]
constexpr uintptr_t kMgrToyBusy = 0x30;          // nonzero while a load is in progress       [rt]
constexpr uintptr_t kMgrSwitchState = 0x3068;    // switching disabled while in [0x16, 0x19]

// Construction piece fields.
constexpr uintptr_t kPieceType = 0x1d5;          // int8: 0-5 Toa, 6 Piraka, 7 gold, 8-13 boss (charId+2), 14 Vezon [rt]

// HUD object (also drives the "switch to the needed mask" prompt).
inline const uintptr_t& kHudPtr = addr::HudPtr;  // 0x6509b8
constexpr uintptr_t kHudHintSlot = 0xb0;         // int16 slot the hint asks for (types > 6 clamp to 6), -1 none
constexpr uintptr_t kHudHintType = 0xb4;         // int16 raw type of the construction nearby
constexpr uintptr_t kPlayerHintCooldown = 0x140c;  // float, nonzero right after a hint switch

// HUD update 0x466af0: "can the current character use this?" checks, patched to call our code.
inline const uintptr_t& kHintCheckLowTypes = addr::HintCheckLowTypes;  // 0x466ca5
constexpr size_t kHintCheckLowTypesLen = 13;
inline const uintptr_t& kHintCheckBossTypes = addr::HintCheckBossTypes;  // 0x466d70
constexpr size_t kHintCheckBossTypesLen = 14;

// The HUD update reads the construction nearby from the player: [[player+0x1430]+0x48] = piece.
constexpr uintptr_t kPlayerNearConstruction = 0x1430;
constexpr uintptr_t kNearConstructionPiece = 0x48;

// Deaths. PlayerDeathStart runs once (wheel icon out, death length +0x1298: 2 s, or 5 s when no other
// character is left), then PlayerDeathUpdate every frame until the timer ends: the slot is locked
// with 20.0 and the game switches to the party's next character, or, with nobody left, rebuilds the
// whole party from the save (BuildParty). A locked slot comes back by PlayerRestoreSlot (mask
// pickups, which never exist for slot 6) or BuildParty.
constexpr uintptr_t kPlayerDeathTimer = 0x1480;  // float, 0 = not dying
constexpr uintptr_t kPlayerDeathFade = 0x147c;   // float, 1 when not dying
constexpr uintptr_t kPlayerPendingSlot = 0x12a8; // int, slot whose death is running (-1)
constexpr uintptr_t kPlayerHealth = 0x12c8;      // float
constexpr uintptr_t kPlayerMaxHealth = 0xb10;    // int
constexpr uintptr_t kPlayerMaskDropped = 0x12e8; // uint8[7], the lost mask's pickup is out in the level
constexpr float kMaskLostLock = 20.0f;           // 0x605ae8, lock value of a slot whose character died
inline const uintptr_t& kPlayerDeathStart = addr::PlayerDeathStart;  // 0x493910 void __fastcall(player)
inline const uintptr_t& kPlayerDeathUpdate = addr::PlayerDeathUpdate;  // 0x499150 void __thiscall(player, pad)
inline const uintptr_t& kPlayerRestoreSlot = addr::PlayerRestoreSlot;  // 0x4974b0 void __thiscall(player, slot)
inline const uintptr_t& kPlayerBuildParty = addr::PlayerBuildParty;  // 0x4971a0 void __thiscall(player, int)
inline const uintptr_t& kHudUpdate = addr::HudUpdate;  // 0x466af0 void __thiscall(hud, a, b), every frame
using PlayerDeathStart_t = void(__fastcall*)(void* player);
using PlayerDeathUpdate_t = void(__thiscall*)(void* player, void* pad);
using PlayerRestoreSlot_t = void(__thiscall*)(void* player, int slot);
using PlayerBuildParty_t = void(__thiscall*)(void* player, int arg);
using HudUpdate_t = void(__thiscall*)(void* hud, void* a, void* b);

// Pad fields.
constexpr uintptr_t kPadPressed = 0x3c;          // bit0/bit1 = next character, bit2 = previous

// Functions.
using GetLevelPirakaCharId_t = int(__cdecl*)(int level);
using ZoneToPirakaCharId_t = int(__cdecl*)(int zone);
using LoadPirakaModel_t = int(__thiscall*)(void* toy, int charId);
using VisualReset_t = void(__thiscall*)(void* visual);
using VisualSetup_t = void(__thiscall*)(void* visual, const void* descriptor);
using PlayerSetSlot_t = void(__thiscall*)(void* player, int slot);
using PlayerPartyStep_t = int(__thiscall*)(void* player);
using PlayerSwitchInput_t = void(__thiscall*)(void* player, void* pad);
using PlayerInitSlotChars_t = void(__thiscall*)(void* player);
using ConstructionUpdate_t = void(__thiscall*)(void* self, void* group, uint8_t* piece, void* pad);
using SaveUnlockCheck_t = int(__thiscall*)(void* save, int pirakaIndex);
using SwitchFx_t = void(__thiscall*)(void* visual);
using PlaySound_t = void(__cdecl*)(void* sound, int flags);
using IsSwitchBlocked_t = int(__cdecl*)();

inline const uintptr_t& kGetLevelPirakaCharId = addr::GetLevelPirakaCharId;  // 0x425340
inline const uintptr_t& kZoneToPirakaCharId = addr::ZoneToPirakaCharId;  // 0x4252e0
inline const uintptr_t& kLoadPirakaModel = addr::LoadPirakaModel;  // 0x4291d0 [rt]
inline const uintptr_t& kVisualReset = addr::VisualReset;  // 0x453f40 [rt]
inline const uintptr_t& kVisualSetup = addr::VisualSetup;  // 0x452cb0 [rt]
inline const uintptr_t& kPlayerSetSlot = addr::PlayerSetSlot;  // 0x494de0
inline const uintptr_t& kPartyNext = addr::PartyNext;  // 0x48ea20
inline const uintptr_t& kPartyPrev = addr::PartyPrev;  // 0x48ea70
inline const uintptr_t& kPlayerSwitchInput = addr::PlayerSwitchInput;  // 0x496220 [rt] runs every gameplay frame
inline const uintptr_t& kPlayerInitSlotChars = addr::PlayerInitSlotChars;  // 0x48e9b0
inline const uintptr_t& kConstructionInteract = addr::ConstructionInteract;  // 0x47ccc0
inline const uintptr_t& kConstructionBuilding = addr::ConstructionBuilding;  // 0x47a9c0
inline const uintptr_t& kSaveIsPirakaUnlocked = addr::SaveIsPirakaUnlocked;  // 0x49a430
inline const uintptr_t& kSaveIsPirakaUnlocked2 = addr::SaveIsPirakaUnlocked2;  // 0x49a470
inline const uintptr_t& kSwitchFx = addr::SwitchFx;  // 0x4538f0
inline const uintptr_t& kPlaySound = addr::PlaySound;  // 0x4c3dc0
inline const uintptr_t& kIsSwitchBlocked = addr::IsSwitchBlocked;  // 0x4ce8c0
inline const uintptr_t& kLevelLoadConfig = addr::LevelLoadConfig;  // 0x46d140 int __cdecl(int level, void* arena, void* extra)
using LevelLoadConfig_t = int(__cdecl*)(int level, void* arena, void* extra);

// Level_LoadConfig (0x46d140): "Vezon unlocked -> load Vezon" checks, patched at the call sites.
inline const uintptr_t& kLoadCfgVezonCheck1 = addr::LoadCfgVezonCheck1;  // 0x46d1fb call Save_IsPirakaUnlocked2(6)
inline const uintptr_t& kLoadCfgVezonCheck2 = addr::LoadCfgVezonCheck2;  // 0x46d20f call Save_IsPirakaUnlocked(6)

// Character wheel ("DrawMaskIcons" GUI callback, 0x457550). Layout/animation per slot lives in the
// player (+0x1f08 + slot*0x18); our 8-entry wheel mirrors its rules (decoded from 0x493080/0x457550).
inline const uintptr_t& kDrawMaskIcons = addr::DrawMaskIcons;  // 0x457550 void __cdecl(void* guiElement)
inline const uintptr_t& kDrawSprite = addr::DrawSprite;  // 0x4ba3a0 thiscall, 13 args (ret 0x34)
inline const uintptr_t& kSlotSaveVariant = addr::SlotSaveVariant;  // 0x49a900 int __thiscall(save, slot): Toa upgrade marker
inline const uintptr_t& kGuiPtr = addr::GuiPtr;  // 0x6a7548
inline const uintptr_t& kFrameDt = addr::FrameDt;  // 0x69aac0 float seconds
inline const uintptr_t& kSmoothRate = addr::SmoothRate;  // 0x69aa88 float
inline const uintptr_t& kWheelPulse = addr::WheelPulse;  // 0x6506b0 float 0..1
inline const uintptr_t& kWheelX = addr::WheelX;  // 0x6506b8 float screen origin
inline const uintptr_t& kWheelY = addr::WheelY;  // 0x6506bc
inline const uintptr_t& kWheelCountCopy = addr::WheelCountCopy;  // 0x6506c0
inline const uintptr_t& kWheelCount = addr::WheelCount;  // 0x6506c4 also read by the wheel frame (0x457ad0)
inline const uintptr_t& kSinTable = addr::SinTable;  // 0x360a300 float[0x8000]
inline const uintptr_t& kToaIcons = addr::ToaIcons;  // 0x64eed0 sprite*[6] by slot
inline const uintptr_t& kPirakaIcons = addr::PirakaIcons;  // 0x64eed8 sprite*[] by char id (6..12 used)
inline const uintptr_t& kUpgradeSprite = addr::UpgradeSprite;  // 0x64f388 sprite*
constexpr uintptr_t kPlayerWheel = 0x1f08;            // per slot 0x18: +4 state, +8 pos, +0xc t, +0x14 scale
constexpr uintptr_t kPlayerWheelStride = 0x18;
constexpr float kWheelStep = 35.0f;                   // 0x603d34
constexpr float kWheelIcon = 32.0f;                   // 0x5c86ac

using DrawMaskIcons_t = void(__cdecl*)(void* guiElement);
using DrawSprite_t = void(__thiscall*)(void* gui, float x, float y, float w, float h, float u0, float v0,
                                       float u1, float v1, int a9, int a10, int a11, uint32_t color,
                                       void* texture);
using SlotSaveVariant_t = int(__thiscall*)(void* save, int slot);

template <typename T>
inline T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

inline uintptr_t Player() { return *reinterpret_cast<uintptr_t*>(kPlayerPtr); }
inline uintptr_t CharMgr() { return *reinterpret_cast<uintptr_t*>(kCharMgrPtr); }
inline const void* CharVisualDescriptor(int charId)
{
    return reinterpret_cast<const void*>(kCharDefTable + charId * kCharDefStride + kCharDefVisual);
}
} // namespace game
