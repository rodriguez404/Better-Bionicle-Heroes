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

### Diagnostics

- **Hang log.** If the game freezes, the mod writes where it is stuck to
  `BetterBionicleHeroes.log` (wait ~20 s before closing the game). It does nothing otherwise.
  — `[Debug] HangWatchdog`

## Requirements

- Bionicle Heroes for PC, `Bionicle.exe` with SHA-256
  `E1DAA3DC24CC184CCE1B0B01DF032BD9A32B2A29D265AE04DBC6C90978D7C4A8`.
  On any other build the mod changes nothing and says so in its log.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (x86) as `dinput8.dll`
  in the game folder (already there if you use BionicleHeroesGFX).

## Installation

Copy `BetterBionicleHeroes.asi` and `BetterBionicleHeroes.ini` next to `Bionicle.exe`.
To uninstall, delete both files. The mod never writes to save games.

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

[Debug]
HangWatchdog = 1          ; log where the game is stuck if it freezes
```

## Building

Visual Studio Build Tools with the C++ workload and CMake (x86 / Win32 target):

```
build.cmd
```

Output: `build\Release\BetterBionicleHeroes.asi`.

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu (BSD 2-Clause), vendored in
  `third_party/minhook`.
- [BionicleHeroesGFX](https://github.com/ermaccer/BionicleHeroesGFX) by ermaccer, for showing the
  way in.
