#pragma once

#include <cstdio>
#include <cstdint>
#include <windows.h>

// macros cuz im lazy
#define NAMESPACE_BEGIN(name) namespace name {
#define NAMESPACE_END(name)   }

#define FEMBOY_EXPORT

#define FEMBOY_LOG(...) do { \
    char _fbl_buf_[2048]; \
    int _fbl_len_ = snprintf(_fbl_buf_, sizeof(_fbl_buf_), __VA_ARGS__); \
    if (_fbl_len_ > 0) OutputDebugStringA(_fbl_buf_); \
} while (0)

#define FEMBOY_READ(addr, buf, size)  ::femboy::read_memory_safe((uintptr_t)(addr), (void*)(buf), (size_t)(size))
#define FEMBOY_WRITE(addr, buf, size) ::femboy::write_memory_safe((uintptr_t)(addr), (const void*)(buf), (size_t)(size))

#define SAFE_VIRTUAL_PROTECT(addr, size, prot) do { \
    DWORD _old_; \
    if (!VirtualProtect((LPVOID)(addr), (SIZE_T)(size), (DWORD)(prot), &_old_)) { \
        FEMBOY_LOG("[!] VirtualProtect failed at %p (size=%zu, prot=0x%lX)", (void*)(addr), (size_t)(size), (DWORD)(prot)); \
    } \
} while (0)

#define PATTERN_BYTES(name, ...) static const uint8_t pat_##name[] = { __VA_ARGS__ }
#define PATTERN_MASK(name, ...)  static const char    pat_mask_##name[] = __VA_ARGS__

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define HEX_DUMP(ptr, len) do { \
    const uint8_t* _b_ = (const uint8_t*)(ptr); \
    size_t _l_ = (size_t)(len); \
    char _h_[4096]; \
    int _p_ = 0; \
    for (size_t _i_ = 0; _i_ < _l_ && _p_ < (int)sizeof(_h_) - 8; _i_++) { \
        _p_ += snprintf(_h_ + _p_, sizeof(_h_) - _p_, "%02X ", _b_[_i_]); \
    } \
    FEMBOY_LOG("HEX[%zu]: %s", _l_, _h_); \
} while (0)
