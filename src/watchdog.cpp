// Hang diagnostics: when Windows reports the game window as hung, log where the window's thread is
// (EIP plus return-address candidates from its stack, with module names) a few times, so a freeze
// can be attributed without a debugger attached.

#include "watchdog.h"

#include "log.h"

#include <windows.h>

#include <cstdio>

namespace
{
HWND FindGameWindow()
{
    struct Search
    {
        DWORD pid;
        HWND found;
    } search{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == s->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER))
            {
                s->found = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// "Bionicle.exe+0x1234" style description, or nullptr-equivalent when not inside a module.
bool DescribeCodeAddress(DWORD address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(address)), &module) ||
        !module)
        return false;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = path;
    for (const char* p = path; *p; ++p)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    std::snprintf(out, size, "%s+0x%lX", name, address - static_cast<DWORD>(reinterpret_cast<uintptr_t>(module)));
    return true;
}

void DumpThread(DWORD threadId)
{
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    if (!thread)
    {
        Log("watchdog: cannot open thread %lu", threadId);
        return;
    }
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    DWORD stack[1024] = {};
    SIZE_T stackRead = 0;
    bool ok = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1))
    {
        ok = GetThreadContext(thread, &ctx) != 0;
        if (ok)
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(ctx.Esp)), stack,
                              sizeof(stack), &stackRead);
        ResumeThread(thread);
    }
    CloseHandle(thread);
    if (!ok)
    {
        Log("watchdog: cannot read context of thread %lu", threadId);
        return;
    }

    char where[160] = "?";
    if (!DescribeCodeAddress(ctx.Eip, where, sizeof(where)))
        std::snprintf(where, sizeof(where), "%08lX (no module)", ctx.Eip);
    Log("watchdog: thread %lu at EIP %s  ESP=%08lX EBP=%08lX EAX=%08lX ECX=%08lX", threadId, where, ctx.Esp, ctx.Ebp,
        ctx.Eax, ctx.Ecx);

    // Return-address candidates: stack words that point into a loaded module.
    int listed = 0;
    for (SIZE_T i = 0; i < stackRead / sizeof(DWORD) && listed < 24; ++i)
    {
        char desc[160];
        if (stack[i] > 0x10000 && DescribeCodeAddress(stack[i], desc, sizeof(desc)))
        {
            Log("watchdog:   [esp+%03X] %s", static_cast<unsigned>(i * 4), desc);
            ++listed;
        }
    }
}

DWORD WINAPI WatchdogMain(LPVOID)
{
    int dumpsThisHang = 0;
    for (;;)
    {
        Sleep(2000);
        const HWND hwnd = FindGameWindow();
        if (!hwnd)
            continue;
        if (!IsHungAppWindow(hwnd))
        {
            if (dumpsThisHang)
                Log("watchdog: window responding again");
            dumpsThisHang = 0;
            continue;
        }
        // A few samples per hang (2 s apart) tell a tight loop from a wait.
        if (dumpsThisHang < 4)
        {
            const DWORD threadId = GetWindowThreadProcessId(hwnd, nullptr);
            Log("watchdog: game window not responding (sample %d)", dumpsThisHang + 1);
            DumpThread(threadId);
            ++dumpsThisHang;
        }
    }
}
} // namespace

void StartHangWatchdog()
{
    if (HANDLE thread = CreateThread(nullptr, 0, WatchdogMain, nullptr, 0, nullptr))
        CloseHandle(thread);
}
