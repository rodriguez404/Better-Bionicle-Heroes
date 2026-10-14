// Checks the signatures against a game exe without running it:
//   sigtest.exe "C:\Games\Bionicle Heroes Game\Bionicle.exe"
// Prints every resolved address as it would be with the exe at its preferred base (0x400000).

#include "../src/addresses.h"

#include <windows.h>

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: sigtest <game exe>\n");
        return 2;
    }
    const HMODULE module = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!module)
    {
        std::printf("cannot map %s (%lu)\n", argv[1], GetLastError());
        return 2;
    }
    const auto base = reinterpret_cast<const uint8_t*>(module);
    ResolveAddressesIn(base);

    const uintptr_t shift = reinterpret_cast<uintptr_t>(base) - 0x400000;
    int missing = 0;
#define SIG_CODE(name, pattern, offset, isCall) \
    std::printf("%-24s %08X\n", #name, addr::name ? static_cast<unsigned>(addr::name - shift) : 0u); \
    missing += addr::name ? 0 : 1;
#define SIG_GLOBAL(name, pattern, offset) \
    std::printf("%-24s %08X\n", #name, static_cast<unsigned>(addr::name)); \
    missing += addr::name ? 0 : 1;
#include "../src/signatures.inc"
    std::printf("%d missing\n", missing);
    return missing ? 1 : 0;
}
