# Better Bionicle Heroes

Gameplay mod for **Bionicle Heroes** (PC, 2006), loaded as an `.asi` plugin.

## Features

### Vezon as an 8th character

In the original game, beating Vezon makes him *replace* the zone boss: in every zone level the
Piraka slot holds Vezon instead of Vezok, Reidak, Hakann, Avak, Thok or Zaktan, and Vezon can use
all of their constructions.

With this mod, once Vezon is beaten:

- each zone level keeps its boss **and** adds Vezon, so the character wheel holds up to 8
  characters (6 Toa + the zone boss + Vezon);
- the mouse wheel cycles `… → zone boss → Vezon → …`, both directions;
- switching between the boss and Vezon is instant: both models stay loaded;
- the zone boss's constructions belong to the boss again — Vezon can't use them (the hint shows
  "switch mask" with the boss icon) and keeps his own black ones;
- the "switch to the needed mask" prompt picks the boss or Vezon depending on the construction.

Outside zone levels (the hub) and before Vezon is beaten, the game behaves as usual.

## Requirements

- Bionicle Heroes for PC, `Bionicle.exe` with SHA-256
  `E1DAA3DC24CC184CCE1B0B01DF032BD9A32B2A29D265AE04DBC6C90978D7C4A8`.
  On any other build the mod changes nothing and says so in its log.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (x86) as `dinput8.dll`
  in the game folder (already there if you use BionicleHeroesGFX).

## Installation

Copy `BetterBionicleHeroes.asi` and `BetterBionicleHeroes.ini` next to `Bionicle.exe`.
To uninstall, delete both files. The mod never writes to save games.

## Settings (`BetterBionicleHeroes.ini`)

| Key | Default | Meaning |
|---|---|---|
| `[VezonExtra] ExtraCharacter` | 1 | Vezon joins next to the zone boss instead of replacing him |
| `[VezonExtra] RestrictVezonPowers` | 1 | Vezon can't use boss constructions (only with `ExtraCharacter = 1`) |
| `[VezonExtra] InstantSwap` | 1 | Keep boss and Vezon loaded together; 0 = reload the model on each switch (~40 ms hitch) |
| `[Debug] HangWatchdog` | 1 | If the game freezes, log where it is stuck to `BetterBionicleHeroes.log` |

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
