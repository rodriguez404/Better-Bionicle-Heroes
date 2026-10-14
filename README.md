# Better Bionicle Heroes

Gameplay mod for **Bionicle Heroes** (PC, 2006), loaded as an `.asi` plugin.

## Changes

Every change can be turned on or off in `BetterBionicleHeroes.ini` (next to the game's
`Bionicle.exe`): set the key to `1` (on) or `0` (off) and restart the game. All changes are **on**
by default.

### Vezon no longer **Replaces** Zone Boss

- **Vezon is added, not swapped in.** So with Vezon unlocked, you can have up to 8 playable characters now (6 Toa + Zone Boss + Vezon).
- **Due to this, Vezon no longer uses the bosses' powers.** The zone boss's constructions belong to the boss
  again: Vezon keeps his own black ones.

### Skippable cutscenes

- **In-level cutscenes can be skipped** with **Esc** or **Space**.

### Pause menu

- **Esc closes the pause menu** and resumes the game, like "Resume". In submenus Esc still goes
  one level up.

### Shop

- **Purchase confirmations start on YES**, so buying takes one key press less. Other YES/NO
  prompts (like quitting the game) still start on NO.

### Diagnostics

- **Hang log.** If the game freezes, the mod writes where it is stuck to
  `BetterBionicleHeroes.log` (wait ~20 s before closing the game). It does nothing otherwise.
  — `[Debug] HangWatchdog`

## Requirements

- Bionicle Heroes for PC. The mod finds the game code it changes by byte signatures, so it works on
  the same game builds as BionicleHeroesGFX; checked with the NoCD `Bionicle.exe` and
  `bionicle_win2k.exe` (1.0.0.32). On a build where something is not recognised, only the affected
  changes stay off, and `BetterBionicleHeroes.log` says which.

## Installation

Extract the release archive into the game folder (next to `Bionicle.exe`). That is all: the archive
includes [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (`dinput8.dll`),
which loads the mod.

Works together with [BionicleHeroesGFX](https://github.com/ermaccer/BionicleHeroesGFX) and other
`.asi` mods: they use the same loader, so install them in any order and let either `dinput8.dll`
overwrite the other.

To uninstall, delete `BetterBionicleHeroes.asi`, `BetterBionicleHeroes.ini`,
`BetterBionicleHeroes-README.md` and the `BetterBionicleHeroes-licenses` folder (keep `dinput8.dll`
if another mod needs it). The mod never writes to save games.

## Settings at a glance

`BetterBionicleHeroes.ini`, `1` = on, `0` = off:

```ini
[VezonExtra]
ExtraCharacter = 1        ; Vezon joins next to the zone boss instead of replacing him
RestrictVezonPowers = 1   ; Vezon can't use the bosses' constructions (needs ExtraCharacter = 1)
InstantSwap = 1           ; no hitch when switching between the boss and Vezon

[CutsceneSkip]
Enabled = 1               ; in-level cutscenes can be skipped with Esc
ExtraKeys = Space         ; more skip keys, comma-separated (empty = Esc only)

[PauseMenu]
EscResumes = 1            ; Esc on the pause menu's top level resumes the game

[Shop]
ConfirmDefaultYes = 1     ; the shop's "buy?" confirmation starts on YES

[Debug]
HangWatchdog = 1          ; log where the game is stuck if it freezes
```

## Building

Visual Studio Build Tools with the C++ workload and CMake (x86 / Win32 target):

```
build.cmd                 :: build\Release\BetterBionicleHeroes.asi
package.cmd 1.0.0         :: dist\BetterBionicleHeroes-1.0.0.zip (release archive)
```

`package.cmd` needs the x86 `dinput8.dll` of Ultimate ASI Loader in
`third_party\ultimate-asi-loader\` (not kept in git).

`build\Release\sigtest.exe <path to a game exe>` checks whether every signature is found in that
exe, without running the game.

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu (BSD 2-Clause), vendored in
  `third_party/minhook`.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT),
  shipped in release archives.
- [BionicleHeroesGFX](https://github.com/ermaccer/BionicleHeroesGFX) by ermaccer, for showing the
  way in.
