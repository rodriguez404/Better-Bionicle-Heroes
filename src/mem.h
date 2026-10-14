#pragma once

#include <cstddef>
#include <cstdint>

// True if the bytes at `address` match `expected` (used to refuse to patch an unknown exe build).
bool BytesMatch(uintptr_t address, const uint8_t* expected, size_t size);

// Rewrites the rel32 target of the 5-byte `call` instruction at `callSite`.
bool PatchCallRel32(uintptr_t callSite, const void* newTarget);

// Overwrites `size` bytes at `address` with `call target` followed by NOPs.
bool WriteCallAndNops(uintptr_t address, size_t size, const void* target);

// Overwrites `size` bytes of game code at `address` with `data`.
bool WriteCode(uintptr_t address, const void* data, size_t size);
