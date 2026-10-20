// Hang diagnostics: when Windows reports the game window as hung, log where the window's thread is
// (EIP plus return-address candidates from its stack, with module names) a few times, so a freeze
// can be attributed without a debugger attached. Crashes (stack overflow, a jump outside every module,
// any unhandled exception) are logged the same way.

#include "watchdog.h"

#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <intrin.h>

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

void LogStack(const char* prefix, const CONTEXT& ctx)
{
    char where[160] = "?";
    if (!DescribeCodeAddress(ctx.Eip, where, sizeof(where)))
        std::snprintf(where, sizeof(where), "%08lX (no module)", ctx.Eip);
    Log("%s EIP %s  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX", prefix, where,
        ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx, ctx.Esi, ctx.Edi, ctx.Ebp, ctx.Esp);
    int listed = 0;
    for (DWORD i = 0; i < 1024 && listed < 24; ++i)
    {
        DWORD value = 0;
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(ctx.Esp + i * 4)),
                               &value, sizeof(value), &read) ||
            read != sizeof(value))
            break;
        char desc[160];
        if (value > 0x10000 && DescribeCodeAddress(value, desc, sizeof(desc)))
        {
            Log("%s   [esp+%03X] %s", prefix, static_cast<unsigned>(i * 4), desc);
            ++listed;
        }
    }
}

// Stack overflows and jumps to garbage (EIP outside every module): the faulting thread may have no stack
// left, so the handler only copies the context and the stack into a static buffer and lets the watchdog
// thread (own stack) write the log.
struct CrashCapture
{
    DWORD code;
    CONTEXT ctx;
    DWORD words;
    DWORD stack[16384];  // 64 KB above ESP
};
CrashCapture g_crash;
HANDLE g_crashReady = nullptr;
HANDLE g_crashLogged = nullptr;
volatile LONG g_crashTaken = 0;

// The first fault of a burst (more than 2 s after the previous one), kept quietly: a fault inside the
// game's own exception handler recurses into a stack overflow, and only this one shows what went
// wrong in the first place.
struct FaultCapture
{
    DWORD code;
    DWORD tick;
    DWORD faultAddress;  // address read or written (access violation)
    CONTEXT ctx;
    DWORD words;
    DWORD stack[512];
};
FaultCapture g_fault;
DWORD g_lastFaultTick = 0;
volatile LONG g_faultBusy = 0;

void RememberFault(const EXCEPTION_POINTERS* info)
{
    const DWORD now = GetTickCount();
    const bool newBurst = g_lastFaultTick == 0 || now - g_lastFaultTick > 2000;
    g_lastFaultTick = now;
    if (!newBurst || InterlockedExchange(&g_faultBusy, 1) != 0)
        return;
    g_fault.code = info->ExceptionRecord->ExceptionCode;
    g_fault.tick = now;
    g_fault.faultAddress = info->ExceptionRecord->NumberParameters >= 2
                               ? static_cast<DWORD>(info->ExceptionRecord->ExceptionInformation[1])
                               : 0;
    g_fault.ctx = *info->ContextRecord;
    const DWORD esp = info->ContextRecord->Esp;
    const DWORD stackBase = __readfsdword(4);
    DWORD words = esp < stackBase ? (stackBase - esp) / 4 : 0;
    if (words > 512)
        words = 512;
    std::memcpy(g_fault.stack, reinterpret_cast<const void*>(static_cast<uintptr_t>(esp)), words * 4);
    g_fault.words = words;
    InterlockedExchange(&g_faultBusy, 0);
}

bool InAnyModule(DWORD address)
{
    HMODULE module = nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(address)), &module) &&
           module;
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_IN_PAGE_ERROR)
        RememberFault(info);
    const bool interesting = code == EXCEPTION_STACK_OVERFLOW ||
                             (code == EXCEPTION_ACCESS_VIOLATION && !InAnyModule(info->ContextRecord->Eip));
    if (!interesting || !g_crashReady || InterlockedExchange(&g_crashTaken, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    g_crash.code = code;
    g_crash.ctx = *info->ContextRecord;
    const DWORD esp = info->ContextRecord->Esp;
    const DWORD stackBase = __readfsdword(4);  // TEB StackBase
    DWORD words = esp < stackBase ? (stackBase - esp) / 4 : 0;
    if (words > 16384)
        words = 16384;
    std::memcpy(g_crash.stack, reinterpret_cast<const void*>(static_cast<uintptr_t>(esp)), words * 4);
    g_crash.words = words;
    SetEvent(g_crashReady);
    WaitForSingleObject(g_crashLogged, 5000);
    return EXCEPTION_CONTINUE_SEARCH;
}

void LogCapturedCrash()
{
    const CONTEXT& ctx = g_crash.ctx;
    char where[160];
    if (!DescribeCodeAddress(ctx.Eip, where, sizeof(where)))
        std::snprintf(where, sizeof(where), "%08lX (no module)", ctx.Eip);
    Log("crash: %s, EIP %s  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX, %lu stack words",
        g_crash.code == EXCEPTION_STACK_OVERFLOW ? "stack overflow" : "access violation", where, ctx.Eax, ctx.Ebx,
        ctx.Ecx, ctx.Edx, ctx.Esi, ctx.Edi, ctx.Ebp, ctx.Esp, g_crash.words);
    int listed = 0;
    for (DWORD i = 0; i < g_crash.words && listed < 32; ++i)
    {
        char desc[160];
        if (g_crash.stack[i] > 0x10000 && DescribeCodeAddress(g_crash.stack[i], desc, sizeof(desc)))
        {
            Log("crash:   [esp+%04X] %s", static_cast<unsigned>(i * 4), desc);
            ++listed;
        }
    }
    // Code addresses that repeat in the stack (recursion).
    struct Count
    {
        DWORD value;
        int n;
    };
    static Count counts[512];
    int distinct = 0;
    for (DWORD i = 0; i < g_crash.words; ++i)
    {
        const DWORD v = g_crash.stack[i];
        if (v <= 0x10000 || !InAnyModule(v))
            continue;
        int k = 0;
        while (k < distinct && counts[k].value != v)
            ++k;
        if (k == distinct && distinct < 512)
            counts[distinct++] = {v, 0};
        if (k < distinct)
            ++counts[k].n;
    }
    for (int round = 0; round < 8; ++round)
    {
        int best = -1;
        for (int k = 0; k < distinct; ++k)
            if (counts[k].n > 1 && (best < 0 || counts[k].n > counts[best].n))
                best = k;
        if (best < 0)
            break;
        char desc[160];
        DescribeCodeAddress(counts[best].value, desc, sizeof(desc));
        Log("crash:   repeated %d x %s", counts[best].n, desc);
        counts[best].n = 0;
    }

    // What started it.
    if (g_fault.tick == 0)
        return;
    const CONTEXT& f = g_fault.ctx;
    if (!DescribeCodeAddress(f.Eip, where, sizeof(where)))
        std::snprintf(where, sizeof(where), "%08lX (no module)", f.Eip);
    Log("crash: first fault %lu ms before: %08lX at %s, address %08lX  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX "
        "ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX",
        GetTickCount() - g_fault.tick, g_fault.code, where, g_fault.faultAddress, f.Eax, f.Ebx, f.Ecx, f.Edx, f.Esi,
        f.Edi, f.Ebp, f.Esp);
    listed = 0;
    for (DWORD i = 0; i < g_fault.words && listed < 24; ++i)
    {
        char desc[160];
        if (g_fault.stack[i] > 0x10000 && DescribeCodeAddress(g_fault.stack[i], desc, sizeof(desc)))
        {
            Log("crash:   [esp+%04X] %s", static_cast<unsigned>(i * 4), desc);
            ++listed;
        }
    }
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* info)
{
    Log("crash: unhandled exception %08lX at %08lX", info->ExceptionRecord->ExceptionCode,
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress)));
    LogStack("crash:", *info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI WatchdogMain(LPVOID)
{
    int dumpsThisHang = 0;
    for (;;)
    {
        if (WaitForSingleObject(g_crashReady, 2000) == WAIT_OBJECT_0)
        {
            LogCapturedCrash();
            SetEvent(g_crashLogged);
            continue;
        }
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
    g_crashReady = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    g_crashLogged = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (HANDLE thread = CreateThread(nullptr, 0, WatchdogMain, nullptr, 0, nullptr))
        CloseHandle(thread);
    AddVectoredExceptionHandler(1, OnException);
    SetUnhandledExceptionFilter(OnUnhandled);
}
