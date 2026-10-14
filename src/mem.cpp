#include "mem.h"

#include <windows.h>
#include <cstring>

bool BytesMatch(uintptr_t address, const uint8_t* expected, size_t size)
{
    return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
}

bool PatchCallRel32(uintptr_t callSite, const void* newTarget)
{
    auto* code = reinterpret_cast<uint8_t*>(callSite);
    if (code[0] != 0xE8)
        return false;
    DWORD oldProtect;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(newTarget) - (callSite + 5));
    std::memcpy(code + 1, &rel, sizeof(rel));
    VirtualProtect(code, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    return true;
}

bool WriteCallAndNops(uintptr_t address, size_t size, const void* target)
{
    if (size < 5)
        return false;
    auto* code = reinterpret_cast<uint8_t*>(address);
    DWORD oldProtect;
    if (!VirtualProtect(code, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    code[0] = 0xE8;
    const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (address + 5));
    std::memcpy(code + 1, &rel, sizeof(rel));
    std::memset(code + 5, 0x90, size - 5);
    VirtualProtect(code, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), code, size);
    return true;
}

bool WriteCode(uintptr_t address, const void* data, size_t size)
{
    auto* code = reinterpret_cast<uint8_t*>(address);
    DWORD oldProtect;
    if (!VirtualProtect(code, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(code, data, size);
    VirtualProtect(code, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), code, size);
    return true;
}
