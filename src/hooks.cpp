#include "hooks.h"

#include "log.h"
#include "mem.h"

#include <MinHook.h>

bool HooksInit()
{
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        Log("MinHook init failed (%d)", static_cast<int>(status));
        return false;
    }
    return true;
}

bool PrologueMatches(const HookSpec& h)
{
    return BytesMatch(h.address, h.prologue, sizeof(h.prologue));
}

bool InstallHook(const HookSpec& h)
{
    if (!PrologueMatches(h))
    {
        Log("%s @%08X: unexpected bytes, not hooked", h.name, static_cast<unsigned>(h.address));
        return false;
    }
    void* target = reinterpret_cast<void*>(h.address);
    if (MH_CreateHook(target, h.detour, h.original) != MH_OK || MH_EnableHook(target) != MH_OK)
    {
        Log("%s @%08X: MinHook failed", h.name, static_cast<unsigned>(h.address));
        return false;
    }
    return true;
}
