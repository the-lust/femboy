#include "memory.hpp"

NAMESPACE_BEGIN(femboy)

uintptr_t get_module_base(const wchar_t* module_name)
{
    if (!module_name) return 0;

    // sheesh module enum is slow but it works
    HMODULE h_mods[1024];
    DWORD needed;
    HANDLE h_proc = GetCurrentProcess();
    if (EnumProcessModules(h_proc, h_mods, sizeof(h_mods), &needed))
    {
        for (DWORD i = 0; i < needed / sizeof(HMODULE); i++)
        {
            wchar_t name[MAX_PATH];
            if (GetModuleFileNameExW(h_proc, h_mods[i], name, MAX_PATH))
            {
                const wchar_t* found = wcsrchr(name, L'\\');
                if (found) found++; else found = name;
                if (_wcsicmp(found, module_name) == 0)
                    return (uintptr_t)h_mods[i];
            }
        }
    }

    HMODULE hm = GetModuleHandleW(module_name);
    return hm ? (uintptr_t)hm : 0;
}

bool read_memory_safe(uintptr_t addr, void* buf, size_t size)
{
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, buf, size, &read) && read == size;
}

bool write_memory_safe(uintptr_t addr, const void* buf, size_t size)
{
    DWORD old;
    HANDLE h_proc = GetCurrentProcess();
    VirtualProtectEx(h_proc, (LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &old);
    SIZE_T written = 0;
    BOOL ok = WriteProcessMemory(h_proc, (LPVOID)addr, buf, size, &written);
    VirtualProtectEx(h_proc, (LPVOID)addr, size, old, &old);
    return ok && written == size;
}

bool mem_search(const uint8_t* data, size_t data_len, const uint8_t* pattern, size_t pattern_len, uintptr_t& result)
{
    for (size_t i = 0; i + pattern_len <= data_len; i++)
    {
        bool found = true;
        for (size_t j = 0; j < pattern_len; j++)
        {
            if (data[i + j] != pattern[j]) { found = false; break; }
        }
        if (found) { result = i; return true; }
    }
    return false;
}

bool mem_search_with_mask(const uint8_t* data, size_t data_len, const uint8_t* pattern, const char* mask, uintptr_t& result)
{
    size_t pattern_len = strlen(mask);
    for (size_t i = 0; i + pattern_len <= data_len; i++)
    {
        bool found = true;
        for (size_t j = 0; j < pattern_len; j++)
        {
            if (mask[j] == 'x' && data[i + j] != pattern[j]) { found = false; break; }
        }
        if (found) { result = i; return true; }
    }
    return false;
}

NAMESPACE_END(femboy)
