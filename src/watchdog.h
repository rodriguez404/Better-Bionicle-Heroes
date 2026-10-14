#pragma once

// Background thread that logs where the game's window thread is stuck when Windows reports the
// window as hung ([Debug] HangWatchdog=1, on by default).
void StartHangWatchdog();
