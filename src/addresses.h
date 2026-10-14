#pragma once

// Game addresses, found at startup by byte signatures (src/signatures.inc) instead of being fixed,
// so the mod works on every Bionicle.exe build whose code matches (verified: the NoCD Bionicle.exe
// and bionicle_win2k.exe, both 1.0.0.32). An address that is not found stays 0 and the features that
// need it stay off.

#include <cstdint>
#include <initializer_list>

namespace addr
{
#define SIG_CODE(name, pattern, offset, isCall) inline uintptr_t name = 0;
#define SIG_GLOBAL(name, pattern, offset) inline uintptr_t name = 0;
#include "signatures.inc"
#undef SIG_CODE
#undef SIG_GLOBAL
} // namespace addr

// Scans the game's code once; logs every signature that is missing or ambiguous.
void ResolveAddresses();
void ResolveAddressesIn(const uint8_t* moduleBase);  // same for a mapped image (tests)

// True if all given addresses were found; otherwise logs which ones are missing for `feature`.
bool AddressesFound(const char* feature, std::initializer_list<const uintptr_t*> required);

// Calls a game function whose address is resolved at runtime; usable as a namespace-scope constant.
template <typename F>
struct GameFn
{
    const uintptr_t& address;
    template <typename... Args>
    auto operator()(Args... args) const
    {
        return reinterpret_cast<F>(address)(args...);
    }
};
