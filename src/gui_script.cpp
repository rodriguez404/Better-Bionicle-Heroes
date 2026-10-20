// GUI script additions and the mod's GUI commands / text callbacks.
//
// The menus are text scripts (gui/game_global.guic etc.), read by the engine's text parser:
// ScriptOpen 0x56b010 (cdecl(path) -> parser) and ScriptReadLine 0x56a2c0 (cdecl(parser) -> length of
// the next non-empty line, 0 at the end; the line is in parser+0x50c, a buffer of parser+0x514 bytes,
// and the tokenizer 0x56a610 reads it from the cursor parser+0x520). For a script with additions the
// mod watches the lines and returns its own lines from ScriptReadLine at the anchors, so the menu is
// built as if the file had them.
//
// Names in the scripts are resolved while the script is parsed: "Action X" (0x4bfed0) takes a command
// if there is one, else the element named X built so far, else some other object. So a page must
// come before the lines that open it (the game's own scripts never refer forward), hence inserts
// before a block.
//
// Commands ("Action", gui+0x3cc, GuiAddCommand 0x4bc910) and value callbacks ("Text ? X",
// "Alpha ? X"..., gui+0x3e4, GuiAddValue 0x4bc980) are lists of the GUI object [0x6a7548]. The game
// fills them in GuiRegisterCommands 0x464360, before it loads game_global; the mod adds its own right
// after. Entries come from pools of the GUI object ([gui+0x37c] + 0x80 / + 0x200) that can run out,
// which is checked first.

#include "gui_script.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr uintptr_t kParserPath = 8;          // char[], the path the parser was opened with
constexpr uintptr_t kParserLine = 0x50c;      // char* line buffer
constexpr uintptr_t kParserLineSize = 0x514;  // its size
constexpr uintptr_t kParserLineNo = 0x51c;
constexpr uintptr_t kParserCursor = 0x520;    // tokenizer position in the line
constexpr uintptr_t kParserBinary = 0x558;    // char, nonzero: not a text script
constexpr uintptr_t kGuiCommands = 0x3cc;
constexpr uintptr_t kGuiValues = 0x3e4;
constexpr uintptr_t kGuiPools = 0x37c;
constexpr uintptr_t kCommandPool = 0x80;
constexpr uintptr_t kValuePool = 0x200;

using ScriptOpen_t = void*(__cdecl*)(const char* path);
using ScriptReadLine_t = unsigned(__cdecl*)(void* parser);
using GuiRegisterCommands_t = void(__cdecl*)(void* gui);
using GuiAdd_t = void(__thiscall*)(void* list, const char* name, void* fn);
using GetLanguage_t = int(__cdecl*)();
ScriptOpen_t o_ScriptOpen = nullptr;
ScriptReadLine_t o_ScriptReadLine = nullptr;
GuiRegisterCommands_t o_GuiRegisterCommands = nullptr;
const GameFn<GuiAdd_t> GuiAddCommandFn{addr::GuiAddCommand};
const GameFn<GuiAdd_t> GuiAddValueFn{addr::GuiAddValue};
const GameFn<GetLanguage_t> GetLanguage{addr::GetLanguage};

template <typename T>
T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

struct Insert
{
    std::string file;
    bool before;  // before the block that starts with `anchor`, else after the anchor lines
    std::vector<std::string> anchor;
    std::vector<std::string> lines;
    size_t matched = 0;  // anchor lines seen so far in the current read ("after" inserts)
    bool done = false;
};

struct Callback
{
    std::string name;
    void* fn;
};

struct QueuedLine
{
    std::string text;
    bool fromFile;  // a line of the script itself (read ahead), not one of the mod's
};

std::vector<Insert> g_inserts;
std::vector<Callback> g_commands;
std::vector<Callback> g_texts;

void* g_parser = nullptr;         // parser of the script being read with additions
std::string g_path;
std::vector<QueuedLine> g_queue;  // lines to return before reading on
size_t g_next = 0;
bool g_eof = false;               // the script ended while reading ahead

bool PathMatches(const std::string& path, const std::string& file)
{
    std::string a = path, b = file;
    for (auto* s : {&a, &b})
        for (char& c : *s)
            c = static_cast<char>(c == '/' ? '\\' : (c >= 'A' && c <= 'Z' ? c + 32 : c));
    return a.find(b) != std::string::npos;
}

bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Trimmed(const std::string& line)
{
    size_t begin = 0, end = line.size();
    while (begin < end && IsSpace(line[begin]))
        ++begin;
    while (end > begin && IsSpace(line[end - 1]))
        --end;
    return line.substr(begin, end - begin);
}

std::string CurrentLine(void* parser)
{
    const char* text = At<const char*>(reinterpret_cast<uintptr_t>(parser), kParserLine);
    return text ? text : "";
}

void Queue(const std::vector<std::string>& lines, bool fromFile)
{
    for (const std::string& line : lines)
        g_queue.push_back({line, fromFile});
}

// "After" anchors advance on the script's own lines as the game gets them (never on the mod's).
void Observe(const std::string& line)
{
    const std::string text = Trimmed(line);
    for (Insert& ins : g_inserts)
    {
        if (ins.done || ins.before || !PathMatches(g_path, ins.file) || text != ins.anchor[ins.matched])
            continue;
        if (++ins.matched == ins.anchor.size())
        {
            ins.done = true;
            Queue(ins.lines, false);
        }
    }
}

// Hands the next queued line to the game as if ScriptReadLine had read it.
unsigned EmitQueued(void* parserPtr)
{
    const auto parser = reinterpret_cast<uintptr_t>(parserPtr);
    while (g_next < g_queue.size())
    {
        const QueuedLine line = g_queue[g_next++];
        char* buffer = At<char*>(parser, kParserLine);
        if (!buffer || line.text.empty() || line.text.size() + 1 > At<unsigned>(parser, kParserLineSize))
            continue;
        std::memcpy(buffer, line.text.c_str(), line.text.size() + 1);
        At<unsigned>(parser, kParserCursor) = 0;
        ++At<int>(parser, kParserLineNo);
        if (line.fromFile)
            Observe(line.text);
        return static_cast<unsigned>(line.text.size());
    }
    g_queue.clear();
    g_next = 0;
    return 0;
}

void FinishScript()
{
    for (const Insert& ins : g_inserts)
        if (PathMatches(g_path, ins.file))
        {
            const std::string& anchor = ins.before ? ins.anchor.front() : ins.anchor.back();
            if (ins.done)
                Log("GuiScript: %s: %zu line(s) added %s \"%s\"", g_path.c_str(), ins.lines.size(),
                    ins.before ? "before" : "after", anchor.c_str());
            else
                Log("GuiScript: %s: anchor \"%s\" not found, %zu line(s) NOT added", g_path.c_str(),
                    ins.before ? anchor.c_str() : ins.anchor[ins.matched].c_str(), ins.lines.size());
        }
    g_parser = nullptr;
}

void* __cdecl hk_ScriptOpen(const char* path)
{
    void* parser = o_ScriptOpen(path);
    if (!parser || !path)
        return parser;
    bool wanted = false;
    for (Insert& ins : g_inserts)
        if (PathMatches(path, ins.file))
        {
            ins.matched = 0;
            ins.done = false;
            wanted = true;
        }
    if (wanted)
    {
        g_parser = parser;
        g_path = path;
        g_queue.clear();
        g_next = 0;
        g_eof = false;
    }
    return parser;
}

unsigned __cdecl hk_ScriptReadLine(void* parserPtr)
{
    if (!parserPtr || parserPtr != g_parser || At<char>(reinterpret_cast<uintptr_t>(parserPtr), kParserBinary) != 0)
        return o_ScriptReadLine(parserPtr);
    // A parser at the same address may be a different file if ours was closed before its end.
    if (std::strcmp(reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(parserPtr) + kParserPath),
                    g_path.c_str()) != 0)
    {
        g_parser = nullptr;
        return o_ScriptReadLine(parserPtr);
    }
    if (g_next < g_queue.size())
        if (const unsigned n = EmitQueued(parserPtr))
            return n;

    const unsigned length = g_eof ? 0 : o_ScriptReadLine(parserPtr);
    if (length == 0)
    {
        FinishScript();
        return 0;
    }
    const std::string line = CurrentLine(parserPtr);

    // "Before" inserts: read ahead to see whether the block starting here is the one, then hand the
    // game the mod's lines followed by the lines read.
    const std::string first = Trimmed(line);
    for (Insert& ins : g_inserts)
    {
        if (ins.done || !ins.before || !PathMatches(g_path, ins.file) || first != ins.anchor[0])
            continue;
        std::vector<std::string> read{line};
        bool all = true;
        for (size_t k = 1; k < ins.anchor.size(); ++k)
        {
            if (o_ScriptReadLine(parserPtr) == 0)
            {
                g_eof = true;
                all = false;
                break;
            }
            read.push_back(CurrentLine(parserPtr));
            if (Trimmed(read.back()) != ins.anchor[k])
            {
                all = false;
                break;
            }
        }
        if (all)
        {
            ins.done = true;
            Queue(ins.lines, false);
        }
        Queue(read, true);
        return EmitQueued(parserPtr);
    }

    Observe(line);
    return length;
}

// Room for one more entry in a pool of the GUI object (FUN_004b45c0: free list, else a bump arena
// with an optional limit).
bool PoolHasRoom(uintptr_t pool)
{
    if (At<uintptr_t>(pool, 4))
        return true;
    const int limit = At<int>(pool, 0x10);
    if (limit != 0 && At<int>(pool, 8) >= limit)
        return false;
    const uintptr_t arena = At<uintptr_t>(pool, 0);
    const uintptr_t cursor = **reinterpret_cast<uintptr_t**>(arena);
    const uintptr_t end = **reinterpret_cast<uintptr_t**>(arena + 4);
    return end > cursor && end - cursor > At<uintptr_t>(pool, 0x14) + 16;
}

void AddAll(void* gui, const std::vector<Callback>& list, uintptr_t listOffset, uintptr_t poolOffset,
            const GameFn<GuiAdd_t>& add, const char* kind)
{
    const auto g = reinterpret_cast<uintptr_t>(gui);
    for (const Callback& c : list)
    {
        const uintptr_t pools = At<uintptr_t>(g, kGuiPools);
        if (!pools || !PoolHasRoom(pools + poolOffset))
        {
            Log("GuiScript: no room for GUI %s %s", kind, c.name.c_str());
            continue;
        }
        add(reinterpret_cast<void*>(g + listOffset), c.name.c_str(), c.fn);
    }
}

void __cdecl hk_GuiRegisterCommands(void* gui)
{
    o_GuiRegisterCommands(gui);
    if (!gui)
        return;
    AddAll(gui, g_commands, kGuiCommands, kCommandPool, GuiAddCommandFn, "command");
    AddAll(gui, g_texts, kGuiValues, kValuePool, GuiAddValueFn, "text");
}

void AddInsert(const char* file, bool before, std::initializer_list<const char*> anchor,
               std::initializer_list<const char*> lines)
{
    Insert ins;
    ins.file = file;
    ins.before = before;
    ins.anchor.assign(anchor.begin(), anchor.end());
    ins.lines.assign(lines.begin(), lines.end());
    if (!ins.anchor.empty())
        g_inserts.push_back(std::move(ins));
}
} // namespace

void GuiScriptInsertAfter(const char* file, std::initializer_list<const char*> after,
                          std::initializer_list<const char*> lines)
{
    AddInsert(file, false, after, lines);
}

void GuiScriptInsertBefore(const char* file, std::initializer_list<const char*> blockStart,
                           std::initializer_list<const char*> lines)
{
    AddInsert(file, true, blockStart, lines);
}

void GuiAddCommand(const char* name, GuiCommandFn fn)
{
    g_commands.push_back({name, reinterpret_cast<void*>(fn)});
}

void GuiAddText(const char* name, GuiTextFn fn)
{
    g_texts.push_back({name, reinterpret_cast<void*>(fn)});
}

int GuiLanguage()
{
    return addr::GetLanguage ? GetLanguage() : 1;
}

bool InstallGuiScript()
{
    if (!AddressesFound("GuiScript", {&addr::ScriptOpen, &addr::ScriptReadLine, &addr::GuiRegisterCommands,
                                      &addr::GuiAddCommand, &addr::GuiAddValue, &addr::GetLanguage}))
        return false;
    const HookSpec hooks[] = {
        {"ScriptOpen", addr::ScriptOpen, reinterpret_cast<void*>(&hk_ScriptOpen), reinterpret_cast<void**>(&o_ScriptOpen)},
        {"ScriptReadLine", addr::ScriptReadLine, reinterpret_cast<void*>(&hk_ScriptReadLine),
         reinterpret_cast<void**>(&o_ScriptReadLine)},
        {"GuiRegisterCommands", addr::GuiRegisterCommands, reinterpret_cast<void*>(&hk_GuiRegisterCommands),
         reinterpret_cast<void**>(&o_GuiRegisterCommands)},
    };
    bool ok = HooksInit();
    for (const auto& h : hooks)
        ok = ok && InstallHook(h);
    Log("GuiScript: %s (%zu addition(s), %zu command(s), %zu text(s))", ok ? "installed" : "FAILED", g_inserts.size(),
        g_commands.size(), g_texts.size());
    return ok;
}
