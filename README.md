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
- **Each of them has his own mask.** If the zone boss dies, only he is gone and Vezon stays, and the
  other way round. Both come back with the rest of the team, as usual.

### Skippable cutscenes

- **In-level cutscenes can be skipped** with **Esc** or **Space**.
- **No startup movies.** The Eidos / Traveller's Tales logos and the story intro don't play at
  startup, and the LEGO Star Wars advert doesn't play on the idle title menu. Each can be turned back on
  separately.

### Pause menu

- **Esc closes the pause menu** and resumes the game, like "Resume". In submenus Esc still goes
  one level up.
- **Restart from Checkpoint.** A new pause menu entry in levels (after a Yes / No confirmation)
  takes the team back to the last checkpoint with every mask, like losing the last mask does, but
  without the death and without losing studs. Before the first checkpoint it is the level's start.

### Menus

- **Confirmations start on the first option**: YES when buying in the shop and when saving or
  loading a game, QUIT on the quit prompts, EXIT LEVEL after beating a boss. The game starts them all
  on the second option.

### Display

- **The video options list your monitor's resolutions** (up to your desktop resolution) instead of
  the fixed 640x480 … 1600x1200. A resolution set in `nuconfig.txt` now shows correctly instead of
  "640 x 480", so Accept no longer switches to it.
- **Fullscreen runs at your Windows refresh rate** (e.g. 144 Hz) instead of always 60 Hz.

### Bug fixes

- **Thok's ice beam damages enemies.** In the original game it never does: his weapon has a small
  fire delay, so it doesn't fire every frame, the beam keeps restarting and never reaches a damage
  tick. Now it fires every frame like Vezok's beam and deals the same damage.
- **Nuparu dig spots look right everywhere.** In some levels (e.g. the third ice level) the game's
  data gives the dig spot's earth mound and grass a tiny grey placeholder texture, so it is a lump of
  flat grey quads. The mod gives them the earth and grass of the other levels, taken from your own
  `bionicle.dat`.

### Diagnostics

- **Hang and crash log.** If the game freezes, the mod writes where it is stuck to
  `BetterBionicleHeroes.log` (wait ~20 s before closing the game); if it crashes, where it crashed.
  It does nothing otherwise. — `[Debug] HangWatchdog`

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

[Intro]
SkipLogos = 1             ; no Eidos / TT logo movies at startup
SkipIntro = 1             ; no story intro movie at startup
SkipAttract = 1           ; no LEGO Star Wars advert on the idle title menu

[PauseMenu]
EscResumes = 1            ; Esc on the pause menu's top level resumes the game
RestartCheckpoint = 1     ; "Restart from Checkpoint" in the pause menu of a level

[Menus]
ConfirmDefaultYes = 1     ; confirmations start on YES / QUIT / EXIT LEVEL (shop, save/load, quit)

[Display]
ResolutionList = 1        ; video options list the monitor's resolutions
AboveDesktop = 0          ; ... also those above the desktop resolution (DSR/VSR)
MatchRefreshRate = 1      ; fullscreen at the Windows refresh rate instead of 60 Hz
RefreshRate = 0           ; fixed refresh rate in Hz (0 = same as Windows)

[Fixes]
ThokBeamDamage = 1        ; Thok's ice beam damages enemies
DigSpotGrass = 1          ; Nuparu dig spots get earth and grass instead of grey quads

[Debug]
HangWatchdog = 1          ; log where the game is stuck if it freezes, or where it crashed
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
