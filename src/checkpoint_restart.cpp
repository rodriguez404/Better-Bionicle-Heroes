// "Restart from Checkpoint" in the in-level pause menu.
//
// The pause menu ("PauseMenu" in gui/game_global.guic: Resume, Options, Quit, Quit to Windows) gets a
// second entry that opens a confirmation page like the Quit one ("Restart from last checkpoint?",
// Yes / No, Yes highlighted). Yes does what the game does when the last mask is lost (the end of
// PlayerDeathUpdate 0x499150): RespawnAtCheckpoint 0x499040(-1, 0) = party and masks from the
// checkpoint, position, the checkpoint's script event, constructions after it reset; then the level
// scripts' "OnPlayerDeath", full health and the death state cleared. Without a checkpoint reached yet
// the game uses the level's start position (0x4721f0). No death animation and no studs lost.
// The texts are the mod's (the game has no such question), in the game's languages.

#include "checkpoint_restart.h"

#include "addresses.h"
#include "game.h"
#include "gui_script.h"
#include "log.h"

using namespace game;

namespace
{
using RespawnAtCheckpoint_t = void(__thiscall*)(void* player, int checkpoint, int keepParty);
using FirePlayerDeath_t = void(__thiscall*)(void* events);
using ResumeGame_t = void(__cdecl*)(void* element);
const GameFn<RespawnAtCheckpoint_t> RespawnAtCheckpoint{addr::RespawnAtCheckpoint};
const GameFn<FirePlayerDeath_t> FirePlayerDeath{addr::FirePlayerDeath};
const GameFn<ResumeGame_t> ResumeGame{addr::ResumeGame};

struct Texts
{
    int language;
    const wchar_t* button;
    const wchar_t* question;
};

// In the wording of the game's own texts (its "checkpoint" and question style per language).
const Texts kTexts[] = {
    {1, L"Restart from Checkpoint", L"Restart from last checkpoint?"},
    {18, L"Restart from Checkpoint", L"Restart from last checkpoint?"},
    {2, L"Reprendre au point de passage", L"Reprendre au dernier point de passage ?"},
    {3, L"Reiniciar desde punto de control", L"¿Reiniciar desde el último punto de control?"},
    {4, L"Ab Kontrollpunkt neu starten", L"Ab dem letzten Kontrollpunkt neu starten?"},
    {5, L"Riparti dal checkpoint", L"Ripartire dall'ultimo checkpoint?"},
    {8, L"Genstart fra checkpoint", L"Vil du genstarte fra sidste checkpoint?"},
    {11, L"Wróć do punktu kontrolnego", L"Chcesz wrócić do ostatniego punktu kontrolnego?"},
    {12, L"Перезапуск с чек-пойнта",
     L"Хотите начать с послед"
     L"него чек-пойнта?"},
    {0, L"最後のチェックポイント",
     L"最後のチェックポイントからはじめますか？"},
};

const Texts& CurrentTexts()
{
    const int language = GuiLanguage();
    for (const Texts& t : kTexts)
        if (t.language == language)
            return t;
    return kTexts[0];
}

void __cdecl ButtonText(void*, const wchar_t** text)
{
    if (text)
        *text = CurrentTexts().button;
}

void __cdecl QuestionText(void*, const wchar_t** text)
{
    if (text)
        *text = CurrentTexts().question;
}

void RestartFromCheckpoint()
{
    const uintptr_t player = Player();
    if (!player)
        return;
    void* playerPtr = reinterpret_cast<void*>(player);
    // As at the end of PlayerDeathUpdate when no mask is left (0x4993de..0x4994b8).
    RespawnAtCheckpoint(playerPtr, -1, 0);
    At<int>(player, kPlayerPendingSlot) = -1;
    if (void* events = At<void*>(0, addr::ScriptEvents))
        FirePlayerDeath(events);
    At<float>(player, kPlayerHealth) = static_cast<float>(At<int>(player, kPlayerMaxHealth));
    float& lock = At<float>(player, kPlayerSlotLock + At<int>(player, kPlayerSlot) * 4);
    if (lock != 0.0f)
        lock = 0.0f;
    At<float>(player, kPlayerDeathFade) = 1.0f;
    At<float>(player, kPlayerDeathTimer) = 0.0f;
    At<int16_t>(player, 0x1e52) = 0;
    At<int16_t>(player, 0x1e50) = 0;
    At<uint8_t>(player, 0xa2) = 0;
    At<uint32_t>(player, 0x84) |= 0x1000;
    Log("CheckpointRestart: restarted from the last checkpoint");
}

// "Yes": restart, then close the menu and unpause like Resume (as the game's unused
// ResetToCheckPoint command 0x455f80 does).
void __cdecl RestartCommand(void* element)
{
    RestartFromCheckpoint();
    ResumeGame(element);
}
} // namespace

bool InstallCheckpointRestart()
{
    if (!AddressesFound("CheckpointRestart", {&addr::RespawnAtCheckpoint, &addr::FirePlayerDeath, &addr::ScriptEvents,
                                              &addr::ResumeGame, &addr::PlayerPtr}))
        return false;

    GuiAddCommand("BBH_RestartCheckpoint", &RestartCommand);
    GuiAddText("BBH_CheckpointButton", &ButtonText);
    GuiAddText("BBH_CheckpointQuestion", &QuestionText);

    // Second entry of the in-level pause menu, right after Resume.
    GuiScriptInsertAfter("gui\\game_global", {"Name PauseMenu", "Text \"RESUME\"", "End"},
                    {
                        "Begin Button",
                        "Position 0,0",
                        "Size 240,30",
                        "Alpha ? GetPauseTextFadeAlpha",
                        "Animation DECELERATE 0.1",
                        "Style PAUSE_ENABLED,PAUSE_SELECTED,PAUSE_DISABLED,PAUSE_DSELECTED",
                        "State ? DisableWhileAutoSaving",
                        "Text ? BBH_CheckpointButton",
                        "Align center",
                        "VAlign center",
                        "Margin 10 0",
                        "Action changepage BBH_CheckpointSure",
                        "OnDraw StashPauseTextLength",
                        "End",
                    });

    // The confirmation page, laid out like the game's QuitSure page. It must come before the pause
    // menu, whose button opens it (names are resolved as the script is read).
    GuiScriptInsertBefore("gui\\game_global", {"Begin Window", "Name PauseMenu"},
                    {
                        "Begin Window",
                        "Name BBH_CheckpointSure",
                        "Position 0,0",
                        "Size 480,272",
                        "ZOrder 0.04",
                        "Animation DECELERATE 0.3",
                        "OnActivate addpage PauseTitle",
                        "OnClosing closepage PauseTitle",
                        "Begin Window 360",
                        "Position 50,105",
                        "Size 540,30",
                        "Alpha 1 0",
                        "Animation DECELERATE 0.1",
                        "Style PAUSE_SELECTED",
                        "Text ? BBH_CheckpointQuestion",
                        "Align center",
                        "VAlign center",
                        "Margin 10 0",
                        "End",
                        "Begin List",
                        "Position 200,223",
                        "Size 240,3",
                        "Space 1",
                        "OnActivate HighlightTop",
                        "Begin Button",
                        "Position 0,0",
                        "Size 240,30",
                        "Alpha 1 0",
                        "Animation DECELERATE 0.1",
                        "Style PAUSE_ENABLED,PAUSE_SELECTED,PAUSE_DISABLED,PAUSE_DSELECTED",
                        "State ? DisableWhileAutoSaving",
                        "Text \"YES\"",
                        "Align center",
                        "VAlign center",
                        "Margin 10 0",
                        "Action BBH_RestartCheckpoint",
                        "End",
                        "Begin Button",
                        "Position 0,0",
                        "Size 240,30",
                        "Alpha 1 0",
                        "Animation DECELERATE 0.1",
                        "Style PAUSE_ENABLED,PAUSE_SELECTED,PAUSE_DEFAULT,PAUSE_DSELECTED",
                        "Text \"NO\"",
                        "Align center",
                        "VAlign center",
                        "Margin 10 0",
                        "Action Backout",
                        "End",
                        "End",
                        "End",
                    });
    Log("CheckpointRestart: prepared");
    return true;
}
