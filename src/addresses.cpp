#include "addresses.h"

#include "log.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace
{
struct Signature
{
    const char* name;
    const char* pattern;
    int offset;
    enum Kind { Code, CallTarget, Global } kind;
    uintptr_t* out;
};

const Signature kSignatures[] = {
#define SIG_CODE(name, pattern, offset, isCall) \
    {#name, pattern, offset, (isCall) ? Signature::CallTarget : Signature::Code, &addr::name},
#define SIG_GLOBAL(name, pattern, offset) {#name, pattern, offset, Signature::Global, &addr::name},
#include "signatures.inc"
#undef SIG_CODE
#undef SIG_GLOBAL
};

// "8B 44 ?? 04" -> bytes + mask (false = wildcard).
void Parse(const char* pattern, std::vector<uint8_t>& bytes, std::vector<bool>& mask)
{
    for (const char* p = pattern; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        if (p[0] == '?')
        {
            bytes.push_back(0);
            mask.push_back(false);
            p += (p[1] == '?') ? 2 : 1;
            continue;
        }
        char hex[3] = {p[0], p[1], 0};
        bytes.push_back(static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)));
        mask.push_back(true);
        p += 2;
    }
}

// Matches of the pattern in [begin, end) (stops counting at 2).
int Find(const uint8_t* begin, const uint8_t* end, const std::vector<uint8_t>& bytes, const std::vector<bool>& mask,
         const uint8_t** match)
{
    size_t anchor = 0;  // first fixed byte, for memchr
    while (anchor < mask.size() && !mask[anchor])
        ++anchor;
    int count = 0;
    const size_t n = bytes.size();
    for (const uint8_t* p = begin + anchor; p + (n - anchor) <= end;)
    {
        p = static_cast<const uint8_t*>(std::memchr(p, bytes[anchor], static_cast<size_t>(end - p - (n - anchor) + 1)));
        if (!p)
            break;
        const uint8_t* start = p - anchor;
        bool ok = true;
        for (size_t i = 0; i < n && ok; ++i)
            ok = !mask[i] || start[i] == bytes[i];
        if (ok)
        {
            *match = start;
            if (++count > 1)
                break;
        }
        ++p;
    }
    return count;
}
} // namespace

void ResolveAddresses()
{
    ResolveAddressesIn(reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr)));
}

void ResolveAddressesIn(const uint8_t* base)
{
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    const uint8_t* begin = nullptr;
    const uint8_t* end = nullptr;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        if (std::strncmp(reinterpret_cast<const char*>(section->Name), ".text", 5) == 0)
        {
            begin = base + section->VirtualAddress;
            end = begin + section->Misc.VirtualSize;
        }
    if (!begin)
    {
        Log("signatures: no .text section in the game exe");
        return;
    }

    const DWORD t0 = GetTickCount();
    int found = 0;
    for (const Signature& s : kSignatures)
    {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;
        Parse(s.pattern, bytes, mask);
        const uint8_t* match = nullptr;
        const int count = Find(begin, end, bytes, mask, &match);
        if (count != 1)
        {
            Log("signatures: %s %s", s.name, count ? "ambiguous" : "not found");
            continue;
        }
        const uint8_t* at = match + s.offset;
        uintptr_t value = 0;
        switch (s.kind)
        {
        case Signature::Code:
            value = reinterpret_cast<uintptr_t>(at);
            break;
        case Signature::CallTarget:
            if (at[0] == 0xE8)
                value = reinterpret_cast<uintptr_t>(at) + 5 + *reinterpret_cast<const int32_t*>(at + 1);
            break;
        case Signature::Global:
            value = *reinterpret_cast<const uint32_t*>(at);
            break;
        }
        *s.out = value;
        found += value ? 1 : 0;
    }
    Log("signatures: %d of %d found (%lu ms)", found, static_cast<int>(sizeof(kSignatures) / sizeof(kSignatures[0])),
        GetTickCount() - t0);
}

bool AddressesFound(const char* feature, std::initializer_list<const uintptr_t*> required)
{
    bool all = true;
    for (const uintptr_t* a : required)
        all = all && *a != 0;
    if (!all)
        Log("%s: game code not recognised (see the signatures lines above); feature disabled", feature);
    return all;
}
