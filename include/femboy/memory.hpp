#pragma once

#include "framework.hpp"

NAMESPACE_BEGIN(femboy)

uintptr_t get_module_base(const wchar_t* module_name);
bool read_memory_safe(uintptr_t addr, void* buf, size_t size);
bool write_memory_safe(uintptr_t addr, const void* buf, size_t size);
bool mem_search(const uint8_t* data, size_t data_len, const uint8_t* pattern, size_t pattern_len, uintptr_t& result);
bool mem_search_with_mask(const uint8_t* data, size_t data_len, const uint8_t* pattern, const char* mask, uintptr_t& result);

NAMESPACE_END(femboy)
