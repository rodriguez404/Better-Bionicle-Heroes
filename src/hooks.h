#pragma once

#include <cstdint>

// A function hook whose target is checked against its known first bytes before hooking.
struct HookSpec
{
    const char* name;
    uintptr_t address;
    uint8_t prologue[8];
    void* detour;
    void** original;
};

bool HooksInit();                   // MH_Initialize, safe to call more than once
bool PrologueMatches(const HookSpec& h);
bool InstallHook(const HookSpec& h);
