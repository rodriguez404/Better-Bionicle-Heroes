#pragma once

// Two Piraka resident at once (zone boss + Vezon) with instant switching; see piraka_cache.cpp.
bool PirakaCachePreload(int charId);  // load charId behind the currently loaded Piraka
bool PirakaCacheReady();
bool PirakaCacheHas(int charId);
bool PirakaCacheSwitch(int charId);   // make charId the loaded Piraka (no loading)
void PirakaCacheRelease();            // free the inactive Piraka, keep the active one
