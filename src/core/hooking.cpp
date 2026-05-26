#include "hooking.hpp"
#include "logger.hpp"

namespace femboy { namespace hook {

Detour::Detour() : m_target(nullptr), m_detour(nullptr)
{
    m_info.target_ptr = nullptr;
    m_info.hook_size = 0;
    m_info.active = false;
    memset(m_info.original_bytes, 0, sizeof(m_info.original_bytes));
}

Detour::~Detour()
{
    if (m_info.active) Remove();
}

// lowkey this detour is finicky af
bool Detour::Install(void* target, void* detour)
{
    if (!target || !detour) return false;
    m_target = target;
    m_detour = detour;

    m_info.target_ptr = (void**)target;

#ifdef _WIN64
    uint8_t jmp_bytes[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    *(uint64_t*)&jmp_bytes[6] = (uint64_t)detour;
    m_info.hook_size = 14;
#else
    uint8_t jmp_bytes[5] = { 0xE9, 0, 0, 0, 0 };
    int32_t rel32 = (int32_t)((uintptr_t)detour - (uintptr_t)target - 5);
    *(int32_t*)&jmp_bytes[1] = rel32;
    m_info.hook_size = 5;
#endif

    DWORD old;
    VirtualProtect(target, m_info.hook_size, PAGE_EXECUTE_READWRITE, &old);
    memcpy(m_info.original_bytes, target, m_info.hook_size);
    memcpy(target, jmp_bytes, m_info.hook_size);
    VirtualProtect(target, m_info.hook_size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, m_info.hook_size);
    m_info.active = true;
    return true;
}

bool Detour::Remove()
{
    if (!m_info.active) return false;
    DWORD old;
    VirtualProtect(m_target, m_info.hook_size, PAGE_EXECUTE_READWRITE, &old);
    memcpy(m_target, m_info.original_bytes, m_info.hook_size);
    VirtualProtect(m_target, m_info.hook_size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), m_target, m_info.hook_size);
    m_info.active = false;
    return true;
}

struct IatHookInfo
{
    void** thunk;
    void* original;
};

static std::unordered_map<std::string, IatHookInfo> g_iat_hooks;
static std::mutex g_iat_mutex;

// no cap this IAT patching is clean
 bool install_iat_hook(const wchar_t* target_module, const char* export_prefix, const char* func_name, void* detour, void** original)
{
    HMODULE h_mod = GetModuleHandleW(target_module);
    if (!h_mod) { LOG("IATHook: %ls not loaded", target_module); return false; }

    ULONG size;
    PIMAGE_IMPORT_DESCRIPTOR imp_desc = (PIMAGE_IMPORT_DESCRIPTOR)
        ImageDirectoryEntryToData(h_mod, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size);
    if (!imp_desc) { LOG("IATHook: no import table in %ls", target_module); return false; }

    for (; imp_desc->Name; imp_desc++)
    {
        const char* mod_name = (const char*)((uintptr_t)h_mod + imp_desc->Name);
        if (!strstr(mod_name, export_prefix)) continue;

        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((uintptr_t)h_mod + imp_desc->FirstThunk);
        PIMAGE_THUNK_DATA name_thunk = (PIMAGE_THUNK_DATA)((uintptr_t)h_mod + imp_desc->OriginalFirstThunk);

        for (; thunk->u1.Function; thunk++, name_thunk++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(name_thunk->u1.Ordinal)) continue;
            PIMAGE_IMPORT_BY_NAME import = (PIMAGE_IMPORT_BY_NAME)
                ((uintptr_t)h_mod + name_thunk->u1.AddressOfData);
            if (strcmp(import->Name, func_name) == 0)
            {
                std::lock_guard<std::mutex> lock(g_iat_mutex);
                std::string key = std::string(func_name) + "@" + std::to_string((uintptr_t)&thunk->u1.Function);
                if (g_iat_hooks.count(key)) { LOG("IATHook: already hooked %s", func_name); return false; }

                DWORD old;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                if (original) *original = (void*)thunk->u1.Function;
                IatHookInfo info;
                info.thunk = reinterpret_cast<void**>(&thunk->u1.Function);
                info.original = reinterpret_cast<void*>(static_cast<uintptr_t>(thunk->u1.Function));
                g_iat_hooks[key] = info;
                thunk->u1.Function = (uintptr_t)detour;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                LOG("IATHook: %ls!%s -> %p (was %p)", target_module, func_name, detour, info.original);
                return true;
            }
        }
    }
    LOG("IATHook: %s not found in %ls imports (prefix=%s)", func_name, target_module, export_prefix);
    return false;
}

bool install_iat_hook_for_all_modules(const char* export_prefix, const char* func_name, void* detour, void** original)
{
    HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (h_snap == INVALID_HANDLE_VALUE) return false;

    bool any_success = false;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(h_snap, &me))
    {
        do
        {
            if (install_iat_hook(me.szModule, export_prefix, func_name, detour, original))
                any_success = true;
        } while (Module32NextW(h_snap, &me));
    }
    CloseHandle(h_snap);
    return any_success;
}

bool remove_iat_hook(const wchar_t* module_name, const char* func_name)
{
    (void)module_name;
    std::lock_guard<std::mutex> lock(g_iat_mutex);
    for (auto& [key, info] : g_iat_hooks)
    {
        size_t at_pos = key.find('@');
        if (at_pos != std::string::npos && key.substr(0, at_pos) == func_name)
        {
            DWORD old;
            VirtualProtect(info.thunk, sizeof(void*), PAGE_READWRITE, &old);
            *info.thunk = info.original;
            VirtualProtect(info.thunk, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

} }
