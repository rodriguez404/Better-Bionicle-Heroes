#pragma once

struct VezonExtraConfig
{
    bool extraCharacter = true;  // Vezon joins as an extra character next to the zone boss
    bool restrictVezon = true;   // Vezon cannot use boss constructions (types 6, 8-13)
    bool instantSwap = true;     // keep boss and Vezon loaded together (no reload on switching)
};

bool InstallVezonExtra(const VezonExtraConfig& cfg);
