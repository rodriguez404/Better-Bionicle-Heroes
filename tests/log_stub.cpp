// Log() for the test tools: straight to stdout.
#include "../src/log.h"

#include <cstdarg>
#include <cstdio>

void LogOpen(const char*) {}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::putchar('\n');
}
