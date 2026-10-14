# Ultimate ASI Loader

[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT, see
`LICENSE`), x86 build, shipped in release archives as `dinput8.dll` so the mod works out of the box.

`dinput8.dll` itself is not kept in git (binaries are ignored); `package.cmd` expects it here. Use the
x86 `dinput8.dll` from an official release.

Any Ultimate ASI Loader loads every `.asi` in the game folder, so it does not matter whose copy ends
up in the game folder (e.g. the one from BionicleHeroesGFX): both mods are loaded either way.
