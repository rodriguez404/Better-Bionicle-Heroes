// Nuparu dig spots look right in levels whose data lacks their textures (e.g. mountain3).
//
// A dig spot is three objects at one place: the special "DARKearthCone" (an earth mound), the special
// "DARKbionicleBit" (the black piece) and an unnamed grass tuft. A material draws the scene texture whose
// slot is in the low 15 bits of its attribute (runtime material +0x78; file +0x6c). In most levels the
// mound uses a 128x128 cracked-earth texture and the tuft its own 128x128 grass texture, with alpha
// (attribute high byte 0x11, bit 0 of +0x40 / file +0x34). In some levels (mountain3) both materials
// point at one 8x8 grey placeholder and the tuft has the mound's opaque flags, so the dig spot is a lump
// of flat grey quads.
//
// Scene textures are created one by one by the TST0 chunk loader (0x548e30) through 0x581720(dds, size,
// &slot), after materials, objects and specials are loaded and before anything is drawn (changing
// materials later has no effect). When the slot of a DARKearthCone material is created from such a
// placeholder, the mod gives the materials that share the slot (the tuft) the grass flags, and creates
// the slot from a combined texture made of desert1's dig-spot textures (read from the player's own
// bionicle.dat): grass colour and alpha, cracked earth under the transparent parts. The tuft shows only
// the blades; the opaque mound shows earth with a few blades painted on it.

#include "dig_grass.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
// Scene being loaded ([addr::SceneLoading]).
constexpr uintptr_t kSceneMaterials = 0x08;     // material*[]
constexpr uintptr_t kSceneMaterialCount = 0x0c;
constexpr uintptr_t kSceneObjectCount = 0x10;
constexpr uintptr_t kSceneObjects = 0x14;       // object*[]
constexpr uintptr_t kSceneSpecialCount = 0x20;
constexpr uintptr_t kSceneSpecials = 0x24;      // special[], stride 0x4c
constexpr uintptr_t kSceneTextureCount = 0x1c4;
constexpr uintptr_t kSceneTextures = 0x1c8;     // texture id[] (filled by the loader)
constexpr uintptr_t kSpecialStride = 0x4c;
constexpr uintptr_t kSpecialInstance = 0x40;    // instance*
constexpr uintptr_t kSpecialName = 0x44;        // const char*
constexpr uintptr_t kInstanceObject = 0x40;     // object index
constexpr uintptr_t kObjectGeometry = 0x0c;     // geometry* (list: +0 next, +4 material*)
constexpr uintptr_t kMaterialFlags = 0x40;      // bit 0: alpha blending (file +0x34)
constexpr uintptr_t kMaterialAttr = 0x78;       // bits 0..14 texture slot, 16..23 render layer (file +0x6c)
constexpr uint32_t kGrassLayer = 0x11;          // attribute high byte of the dig-spot grass in good levels

constexpr int kMaxMounds = 8;
constexpr uint32_t kPlaceholderMaxSize = 32;
constexpr uint32_t kSize = 128;

using CreateTexture_t = int(__cdecl*)(const void* dds, uint32_t size, void* out);
CreateTexture_t o_CreateTexture = nullptr;

std::vector<uint8_t> g_combined;  // 128x128 A8R8G8B8 DDS
uintptr_t g_scene = 0;
uintptr_t g_mounds[kMaxMounds];   // DARKearthCone materials of g_scene
int g_moundCount = 0;

template <typename T>
T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

// ---- textures from bionicle.dat ---------------------------------------------------------------

bool ReadAt(FILE* f, long offset, void* out, size_t size)
{
    return fseek(f, offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}

// bionicle.dat: [u32 table offset][u32 table size]; table: -1, files, files x {offset, size, packed
// size, flags}, nodes, nodes x {s16 next, s16 prev, u32 name}, pool size, names. Node next <= 0: file
// -next; next > 0: directory ending before node next.
bool FindDatFile(FILE* f, const char* wanted, uint32_t& offset, uint32_t& size)
{
    uint32_t hdr[2];
    if (!ReadAt(f, 0, hdr, sizeof(hdr)) || hdr[1] < 16 || hdr[1] > (64u << 20))
        return false;
    std::vector<uint8_t> t(hdr[1]);
    if (!ReadAt(f, static_cast<long>(hdr[0]), t.data(), t.size()))
        return false;
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &t[o], 4); return v; };
    const uint32_t files = u32(4);
    size_t p = 8 + size_t(files) * 16;
    if (p + 4 > t.size())
        return false;
    const uint32_t nodes = u32(p);
    const size_t nodeBase = p + 4;
    if (nodeBase + size_t(nodes) * 8 + 4 > t.size())
        return false;
    const size_t poolSize = u32(nodeBase + size_t(nodes) * 8);
    const size_t pool = nodeBase + size_t(nodes) * 8 + 4;
    if (pool + poolSize > t.size())
        return false;

    std::vector<std::pair<int, std::string>> dirs;  // (end node, name)
    for (uint32_t i = 1; i < nodes; ++i)
    {
        while (!dirs.empty() && int(i) >= dirs.back().first)
            dirs.pop_back();
        int16_t next;
        std::memcpy(&next, &t[nodeBase + i * 8], 2);
        const uint32_t nameOff = u32(nodeBase + i * 8 + 4);
        if (nameOff >= poolSize)
            return false;
        const char* name = reinterpret_cast<const char*>(&t[pool + nameOff]);
        if (next > 0)
        {
            dirs.emplace_back(next, name);
            continue;
        }
        std::string path;
        for (const auto& d : dirs)
            if (!d.second.empty())
                path += d.second + "/";
        path += name;
        if (_stricmp(path.c_str(), wanted) == 0 && uint32_t(-next) < files)
        {
            offset = u32(8 + size_t(-next) * 16);
            size = u32(8 + size_t(-next) * 16 + 4);
            return true;
        }
    }
    return false;
}

// Textures of the TST0 chunk of a NU20 scene at `nupOffset` in the archive (DDS files).
bool ReadSceneTextures(FILE* f, uint32_t nupOffset, uint32_t nupSize, std::initializer_list<int> indices,
                       std::vector<std::vector<uint8_t>>& out)
{
    for (uint32_t off = 0x10; off + 8 <= nupSize;)
    {
        char tag[4];
        uint32_t size;
        if (!ReadAt(f, long(nupOffset + off), tag, 4) || !ReadAt(f, long(nupOffset + off + 4), &size, 4) ||
            size < 8 || off + size > nupSize)
            return false;
        if (std::memcmp(tag, "TST0", 4) != 0)
        {
            off += size;
            continue;
        }
        std::vector<uint8_t> c(size);
        if (!ReadAt(f, long(nupOffset + off), c.data(), size))
            return false;
        uint32_t count;
        std::memcpy(&count, &c[8], 4);
        if (0x20 + size_t(count) * 20 > size)
            return false;
        auto entryOffset = [&](uint32_t i) { uint32_t v; std::memcpy(&v, &c[0x20 + i * 20 + 16], 4); return v; };
        size_t data = 0x20 + size_t(count) * 20;
        while (data + 4 <= size && std::memcmp(&c[data], "DDS ", 4) != 0)
            ++data;
        for (const int index : indices)
        {
            if (index < 0 || uint32_t(index) >= count)
                return false;
            const uint32_t begin = entryOffset(index);
            uint32_t end = size - uint32_t(data);
            for (uint32_t i = 0; i < count; ++i)
                if (entryOffset(i) > begin && entryOffset(i) < end)
                    end = entryOffset(i);
            if (data + end > size || begin >= end)
                return false;
            out.emplace_back(c.begin() + data + begin, c.begin() + data + end);
        }
        return true;
    }
    return false;
}

// 565 colour block of DXT1/3/5 (4-colour mode).
void DecodeColourBlock(const uint8_t* b, uint8_t rgba[16][4])
{
    uint16_t c0, c1;
    std::memcpy(&c0, b, 2);
    std::memcpy(&c1, b + 2, 2);
    uint8_t col[4][3];
    auto expand = [](uint16_t c, uint8_t out[3]) {
        out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
        out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
        out[2] = uint8_t((c & 31) * 255 / 31);
    };
    expand(c0, col[0]);
    expand(c1, col[1]);
    for (int k = 0; k < 3; ++k)
    {
        col[2][k] = uint8_t((2 * col[0][k] + col[1][k]) / 3);
        col[3][k] = uint8_t((col[0][k] + 2 * col[1][k]) / 3);
    }
    uint32_t bits;
    std::memcpy(&bits, b + 4, 4);
    for (int i = 0; i < 16; ++i)
        std::memcpy(rgba[i], col[(bits >> (2 * i)) & 3], 3);
}

// 128x128 DXT3/DXT5 DDS -> RGBA.
bool DecodeDds(const std::vector<uint8_t>& dds, std::vector<uint8_t>& rgba)
{
    if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0 || At<uint32_t>(uintptr_t(dds.data()), 12) != kSize ||
        At<uint32_t>(uintptr_t(dds.data()), 16) != kSize)
        return false;
    const bool dxt5 = std::memcmp(dds.data() + 84, "DXT5", 4) == 0;
    if (!dxt5 && std::memcmp(dds.data() + 84, "DXT3", 4) != 0)
        return false;
    const size_t blocks = (kSize / 4) * (kSize / 4);
    if (dds.size() < 128 + blocks * 16)
        return false;
    rgba.assign(kSize * kSize * 4, 0);
    for (size_t n = 0; n < blocks; ++n)
    {
        const uint8_t* b = dds.data() + 128 + n * 16;
        uint8_t px[16][4];
        DecodeColourBlock(b + 8, px);
        if (dxt5)
        {
            uint8_t a[8] = {b[0], b[1]};
            for (int k = 2; k < 8; ++k)
                a[k] = a[0] > a[1] ? uint8_t(((8 - k) * a[0] + (k - 1) * a[1]) / 7)
                                   : (k < 6 ? uint8_t(((6 - k) * a[0] + (k - 1) * a[1]) / 5) : (k == 6 ? 0 : 255));
            uint64_t bits = 0;
            std::memcpy(&bits, b + 2, 6);
            for (int i = 0; i < 16; ++i)
                px[i][3] = a[(bits >> (3 * i)) & 7];
        }
        else
        {
            for (int i = 0; i < 16; ++i)
                px[i][3] = uint8_t(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
        }
        const size_t bx = (n % (kSize / 4)) * 4, by = (n / (kSize / 4)) * 4;
        for (int i = 0; i < 16; ++i)
            std::memcpy(&rgba[((by + i / 4) * kSize + bx + i % 4) * 4], px[i], 4);
    }
    return true;
}

bool BuildCombinedTexture()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dat(path);
    dat = dat.substr(0, dat.find_last_of("\\/") + 1) + "bionicle.dat";
    FILE* f = fopen(dat.c_str(), "rb");
    if (!f)
        return false;
    uint32_t offset = 0, size = 0;
    std::vector<std::vector<uint8_t>> tex;
    // desert1's dig spot: 156 = cracked earth (mound, 128x128 DXT3), 157 = grass (tuft, 128x128 DXT5).
    const bool read = FindDatFile(f, "levels/desert1/desert1.nup", offset, size) &&
                      ReadSceneTextures(f, offset, size, {156, 157}, tex);
    fclose(f);
    std::vector<uint8_t> earth, grass;
    if (!read || !DecodeDds(tex[0], earth) || !DecodeDds(tex[1], grass))
        return false;

    g_combined.assign(128 + kSize * kSize * 4, 0);
    const uint32_t header[] = {0x20534444, 124, 0x100F, kSize, kSize, kSize * 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               32, 0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, 0x1000};
    std::memcpy(g_combined.data(), header, sizeof(header));
    for (size_t i = 0; i < kSize * kSize; ++i)
    {
        const uint8_t* e = &earth[i * 4];
        const uint8_t* g = &grass[i * 4];
        const unsigned a = g[3];
        uint8_t* o = &g_combined[128 + i * 4];  // B, G, R, A
        for (int k = 0; k < 3; ++k)
            o[2 - k] = uint8_t((g[k] * a + e[k] * (255 - a)) / 255);
        o[3] = uint8_t(a);
    }
    return true;
}

// ---- scene ------------------------------------------------------------------------------------

// Materials of the scene's DARKearthCone specials.
void FindMounds(uintptr_t scene)
{
    g_moundCount = 0;
    __try
    {
        const int specials = At<int>(scene, kSceneSpecialCount);
        const uintptr_t specialBase = At<uintptr_t>(scene, kSceneSpecials);
        const int objects = At<int>(scene, kSceneObjectCount);
        const uintptr_t objectBase = At<uintptr_t>(scene, kSceneObjects);
        for (int i = 0; i < specials && i < 4096; ++i)
        {
            const uintptr_t special = specialBase + i * kSpecialStride;
            const char* name = At<const char*>(special, kSpecialName);
            if (!name || _strnicmp(name, "DARKearthCone", 13) != 0)
                continue;
            const uintptr_t instance = At<uintptr_t>(special, kSpecialInstance);
            const int object = instance ? At<int>(instance, kInstanceObject) : -1;
            if (object < 0 || object >= objects)
                continue;
            uintptr_t geometry = At<uintptr_t>(At<uintptr_t>(objectBase, object * 4), kObjectGeometry);
            for (int n = 0; geometry && n < 8; ++n, geometry = At<uintptr_t>(geometry, 0))
            {
                const uintptr_t material = At<uintptr_t>(geometry, 4);
                bool known = !material;
                for (int k = 0; k < g_moundCount; ++k)
                    known = known || g_mounds[k] == material;
                if (!known && g_moundCount < kMaxMounds)
                    g_mounds[g_moundCount++] = material;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_moundCount = 0;
    }
}

// Gives the other materials drawing `slot` (the grass tuft) the grass flags; returns how many.
int FixGrassMaterials(uintptr_t scene, int slot)
{
    int fixed = 0;
    __try
    {
        const int count = At<int>(scene, kSceneMaterialCount);
        const uintptr_t materials = At<uintptr_t>(scene, kSceneMaterials);
        for (int i = 0; i < count && i < 8192; ++i)
        {
            const uintptr_t material = At<uintptr_t>(materials, i * 4);
            if (!material || int(At<uint32_t>(material, kMaterialAttr) & 0x7fff) != slot)
                continue;
            bool mound = false;
            for (int k = 0; k < g_moundCount; ++k)
                mound = mound || g_mounds[k] == material;
            if (mound)
                continue;
            At<uint32_t>(material, kMaterialAttr) = (At<uint32_t>(material, kMaterialAttr) & 0xffff) | (kGrassLayer << 16);
            At<uint32_t>(material, kMaterialFlags) |= 1;
            ++fixed;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return fixed;
}

bool IsPlaceholder(const void* dds, uint32_t size)
{
    const auto* p = static_cast<const uint8_t*>(dds);
    return dds && size >= 128 && std::memcmp(p, "DDS ", 4) == 0 && At<uint32_t>(uintptr_t(p), 12) <= kPlaceholderMaxSize &&
           At<uint32_t>(uintptr_t(p), 16) <= kPlaceholderMaxSize;
}

int SlotOf(uintptr_t scene, void* out)
{
    __try
    {
        const uintptr_t textures = scene ? At<uintptr_t>(scene, kSceneTextures) : 0;
        const int count = scene ? At<int>(scene, kSceneTextureCount) : 0;
        const auto o = reinterpret_cast<uintptr_t>(out);
        if (textures && o >= textures && o < textures + count * 4u && (o - textures) % 4 == 0)
            return int((o - textures) / 4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return -1;
}

int __cdecl hk_CreateTexture(const void* dds, uint32_t size, void* out)
{
    const uintptr_t scene = At<uintptr_t>(addr::SceneLoading, 0);
    const int slot = SlotOf(scene, out);
    if (slot >= 0 && IsPlaceholder(dds, size))
    {
        if (scene != g_scene)
        {
            g_scene = scene;
            FindMounds(scene);
        }
        for (int k = 0; k < g_moundCount; ++k)
        {
            if (int(At<uint32_t>(g_mounds[k], kMaterialAttr) & 0x7fff) != slot)
                continue;
            const int grass = FixGrassMaterials(scene, slot);
            Log("DigSpotGrass: texture %d (%ux%u placeholder) -> earth and grass, %d grass material(s) fixed", slot,
                At<uint32_t>(uintptr_t(dds), 16), At<uint32_t>(uintptr_t(dds), 12), grass);
            return o_CreateTexture(g_combined.data(), uint32_t(g_combined.size()), out);
        }
    }
    return o_CreateTexture(dds, size, out);
}
} // namespace

bool InstallDigSpotGrass()
{
    if (!AddressesFound("DigSpotGrass", {&addr::CreateTextureFromMemory, &addr::SceneLoading}))
        return false;
    if (!BuildCombinedTexture())
    {
        Log("DigSpotGrass: dig-spot textures not found in bionicle.dat; feature disabled");
        return false;
    }
    const HookSpec hook = {"CreateTextureFromMemory", addr::CreateTextureFromMemory, reinterpret_cast<void*>(&hk_CreateTexture),
                           reinterpret_cast<void**>(&o_CreateTexture)};
    const bool ok = HooksInit() && InstallHook(hook);
    Log("DigSpotGrass: %s", ok ? "installed" : "FAILED");
    return ok;
}
