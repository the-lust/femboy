#include "anti_debug_patcher.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "memory.hpp"
#include <intrin.h>

static BOOL(WINAPI* Real_IsDebuggerPresent)() = nullptr;
static BOOL(WINAPI* Real_CheckRemoteDebuggerPresent)(HANDLE, PBOOL) = nullptr;
static DWORD(WINAPI* Real_GetTickCount)() = nullptr;
static bool g_get_tick_count_patched = false;
static LONG(WINAPI* Real_WinVerifyTrust)(HWND, GUID*, void*) = nullptr;
static HANDLE(WINAPI* Real_CreateToolhelp32Snapshot)(DWORD, DWORD) = nullptr;

static NTSTATUS(NTAPI* Real_NtQuerySystemInformation)(
    ULONG, void*, ULONG, PULONG) = nullptr;
static NTSTATUS(NTAPI* Real_NtQueryInformationProcess)(
    HANDLE, ULONG, void*, ULONG, PULONG) = nullptr;

#define SystemKernelDebuggerInformation 0x23
#define ProcessDebugPort 0x7
#define ProcessDebugFlags 0x1F
#define ProcessDebugObjectHandle 0x1E
#define STATUS_SUCCESS 0
#define STATUS_INVALID_INFO_CLASS 0xC0000003

const char* femboy::patch::AntiDebugPatcher::Name()
{
    return "anti_debug_patcher";
}

bool femboy::patch::AntiDebugPatcher::patch_peb_being_debugged()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;

        uintptr_t sec_base = base + sec[i].VirtualAddress;
        DWORD sec_size = sec[i].Misc.VirtualSize;
        std::vector<uint8_t> data(sec_size);
        if (!read_memory_safe(sec_base, data.data(), sec_size))
            continue;

#ifdef _WIN64
        const uint8_t pattern[] = { 0x65, 0x0F, 0xB6, 0x40, 0x02 };
        const uint8_t replace[] = { 0x33, 0xC0, 0x90, 0x90, 0x90 };
#else
        const uint8_t pattern[] = { 0x64, 0x0F, 0xB6, 0x40, 0x02 };
        const uint8_t replace[] = { 0x33, 0xC0, 0x90, 0x90, 0x90 };
#endif

        size_t pat_len = sizeof(pattern);
        for (DWORD j = 0; j + pat_len <= sec_size; j++)
        {
            if (memcmp(data.data() + j, pattern, pat_len) == 0)
            {
                uintptr_t patch_addr = sec_base + j;
                write_memory_safe(patch_addr, replace, pat_len);
                LOG("[AntiDebug] PEB BeingDebugged patch at 0x%p", (void*)patch_addr);
                return true;
            }
        }
    }
    return false;
}

bool femboy::patch::AntiDebugPatcher::patch_tls_callbacks()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    IMAGE_DATA_DIRECTORY* tls_dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!tls_dir->Size || !tls_dir->VirtualAddress) return false;

    IMAGE_TLS_DIRECTORY* tls = (IMAGE_TLS_DIRECTORY*)(base + tls_dir->VirtualAddress);
    if (!tls->AddressOfCallBacks) return false;

    uintptr_t* callbacks = (uintptr_t*)tls->AddressOfCallBacks;
    for (int i = 0; ; i++)
    {
        uintptr_t cb = 0;
        read_memory_safe((uintptr_t)&callbacks[i], &cb, sizeof(cb));
        if (cb == 0) break;

        uint8_t ret = 0xC3;
        write_memory_safe(cb, &ret, 1);
        LOG("[AntiDebug] TLS callback %d at 0x%p neutralised", i, (void*)cb);
    }

    return true;
}

DWORD __stdcall femboy::patch::AntiDebugPatcher::hook_get_tick_count()
{
    DWORD result = 0;
    if (Real_GetTickCount) result = Real_GetTickCount();

    if (!g_get_tick_count_patched)
    {
        patch_caller_patterns((uintptr_t)_ReturnAddress());
        g_get_tick_count_patched = true;
    }
    return result;
}

BOOL __stdcall femboy::patch::AntiDebugPatcher::hook_is_debugger_present()
{
    patch_caller_patterns((uintptr_t)_ReturnAddress());
    return FALSE;
}

BOOL __stdcall femboy::patch::AntiDebugPatcher::hook_check_remote_debugger_present(HANDLE h_process, PBOOL pb_debugger_present)
{
    (void)h_process;
    if (pb_debugger_present) *pb_debugger_present = FALSE;
    patch_caller_patterns((uintptr_t)_ReturnAddress());
    return TRUE;
}

LONG __stdcall femboy::patch::AntiDebugPatcher::hook_win_verify_trust(HWND hwnd, GUID* pg_action_id, void* p_wvt_data)
{
    (void)hwnd; (void)pg_action_id; (void)p_wvt_data;
    return 0;
}

HANDLE __stdcall femboy::patch::AntiDebugPatcher::hook_create_toolhelp32_snapshot(DWORD dw_flags, DWORD th32_process_id)
{
    if (!g_oep_confirmed)
    {
        LOG("[AntiDebug] CreateToolhelp32Snapshot blocked (stub phase)");
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    if (Real_CreateToolhelp32Snapshot)
        return Real_CreateToolhelp32Snapshot(dw_flags, th32_process_id);
    return CreateToolhelp32Snapshot(dw_flags, th32_process_id);
}

NTSTATUS NTAPI femboy::patch::AntiDebugPatcher::hook_nt_query_system_information(
    ULONG system_information_class, void* system_information,
    ULONG system_information_length, PULONG return_length)
{
    // nt sysinfo struct layout — pulled this from ntifs.h fr fr
    if (system_information_class == SystemKernelDebuggerInformation)
    {
        LOG("[AntiDebug] NtQuerySystemInformation(KernelDebugger) blocked");
        if (system_information_length < 2)
            return STATUS_INVALID_INFO_CLASS;

        uint8_t* info = (uint8_t*)system_information;
        info[0] = FALSE;
        info[1] = TRUE;
        if (return_length) *return_length = 2;
        return STATUS_SUCCESS;
    }

    if (Real_NtQuerySystemInformation)
        return Real_NtQuerySystemInformation(system_information_class, system_information,
            system_information_length, return_length);
    return STATUS_INVALID_INFO_CLASS;
}

NTSTATUS NTAPI femboy::patch::AntiDebugPatcher::hook_nt_query_information_process(
    HANDLE process_handle, ULONG process_information_class,
    void* process_information, ULONG process_information_length,
    PULONG return_length)
{
    if (process_information_class == ProcessDebugPort)
    {
        LOG("[AntiDebug] NtQueryInformationProcess(DebugPort) blocked");
        if (process_information_length < sizeof(DWORD))
            return STATUS_INVALID_INFO_CLASS;
        *(DWORD*)process_information = 0;
        if (return_length) *return_length = sizeof(DWORD);
        return STATUS_SUCCESS;
    }

    if (process_information_class == ProcessDebugFlags)
    {
        LOG("[AntiDebug] NtQueryInformationProcess(DebugFlags) blocked");
        if (process_information_length < sizeof(DWORD))
            return STATUS_INVALID_INFO_CLASS;
        *(DWORD*)process_information = 1;
        if (return_length) *return_length = sizeof(DWORD);
        return STATUS_SUCCESS;
    }

    if (process_information_class == ProcessDebugObjectHandle)
    {
        LOG("[AntiDebug] NtQueryInformationProcess(DebugObject) blocked");
        if (process_information_length < sizeof(HANDLE))
            return STATUS_INVALID_INFO_CLASS;
        *(HANDLE*)process_information = nullptr;
        if (return_length) *return_length = sizeof(HANDLE);
        return STATUS_SUCCESS;
    }

    if (Real_NtQueryInformationProcess)
        return Real_NtQueryInformationProcess(process_handle, process_information_class,
            process_information, process_information_length, return_length);
    return STATUS_INVALID_INFO_CLASS;
}

bool femboy::patch::AntiDebugPatcher::patch_crc32_anti_tamper()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    bool found = false;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;

        uintptr_t sec_base = base + sec[i].VirtualAddress;
        DWORD sec_size = sec[i].Misc.VirtualSize;

        std::vector<uint8_t> data(sec_size);
        if (!read_memory_safe(sec_base, data.data(), sec_size))
            continue;

        const uint8_t poly_bytes[] = { 0x20, 0x83, 0xB8, 0xED };
        const uint8_t poly_bytes_64[] = { 0xED, 0xB8, 0x83, 0x20 };

        for (DWORD j = 0; j + 8 <= sec_size; j++)
        {
            if ((data[j] == 0xB8 || data[j] == 0xB9 || data[j] == 0xBA || data[j] == 0xBB) &&
                memcmp(&data[j + 1], poly_bytes, 4) == 0)
            {
                LOG("[AntiDebug] CRC32 polynomial at 0x%p", (void*)(sec_base + j));

                for (DWORD k = j + 5; k < j + 0x50 && k + 2 < sec_size; k++)
                {
                    if (data[k] == 0x85 && data[k + 1] == 0xC0)
                    {
                        for (DWORD m = k + 2; m < k + 10 && m + 2 < sec_size; m++)
                        {
                            if (data[m] == 0x74 || data[m] == 0x75)
                            {
                                uintptr_t patch_addr = sec_base + m;
                                uint8_t nop2[] = { 0x90, 0x90 };
                                write_memory_safe(patch_addr, nop2, 2);
                                LOG("[AntiDebug] CRC32 conditional jump NOPed at 0x%p",
                                    (void*)patch_addr);
                                found = true;
                            }
                        }
                    }
                }
            }

            if (data[j] == 0xF2 && data[j + 1] == 0x0F && data[j + 2] == 0x38 && data[j + 3] == 0xF1)
            {
                LOG("[AntiDebug] SSE4.2 CRC32 instruction at 0x%p", (void*)(sec_base + j));
                for (DWORD k = j + 4; k < j + 0x50 && k + 2 < sec_size; k++)
                {
                    if (data[k] == 0x85 && data[k + 1] == 0xC0)
                    {
                        for (DWORD m = k + 2; m < k + 10 && m + 2 < sec_size; m++)
                        {
                            if (data[m] == 0x74 || data[m] == 0x75)
                            {
                                uintptr_t patch_addr = sec_base + m;
                                uint8_t nop2[] = { 0x90, 0x90 };
                                write_memory_safe(patch_addr, nop2, 2);
                                LOG("[AntiDebug] CRC32(SSE4.2) conditional jump NOPed at 0x%p",
                                    (void*)patch_addr);
                                found = true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (found)
        LOG("[AntiDebug] CRC32 anti-tamper patching complete");
    else
        LOG("[AntiDebug] CRC32 anti-tamper patterns not found (game may not use it)");
    return found;
}

void femboy::patch::AntiDebugPatcher::patch_caller_patterns(uintptr_t return_address)
{
    if (!return_address) return;

    const size_t scan_range = 512;
    std::vector<uint8_t> code(scan_range);
    uintptr_t scan_base = return_address - scan_range / 2;
    if (!read_memory_safe(scan_base, code.data(), scan_range))
        return;

    const uint8_t pat1[] = { 0x85, 0xC0, 0x74, 0x00 };
    uintptr_t offset;
    if (mem_search(code.data(), code.size(), pat1, 3, offset))
    {
        const uint8_t nop4[] = { 0x90, 0x90, 0x90, 0x90 };
        write_memory_safe(scan_base + offset, nop4, 3);
        LOG("[AntiDebug] patched jz pattern at 0x%p", (void*)(scan_base + offset));
    }

    const uint8_t pat2[] = { 0x85, 0xC0, 0x0F, 0x85 };
    if (mem_search(code.data(), code.size(), pat2, 4, offset))
    {
        const uint8_t nop6[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        write_memory_safe(scan_base + offset, nop6, 6);
        LOG("[AntiDebug] patched jnz pattern at 0x%p", (void*)(scan_base + offset));
    }

    const uint8_t pat3[] = { 0x44, 0x0F, 0xB6, 0xF8, 0x3C, 0x30, 0x0F, 0x84 };
    if (mem_search(code.data(), code.size(), pat3, sizeof(pat3), offset))
    {
        const uint8_t nop6[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        write_memory_safe(scan_base + offset, nop6, 6);
        LOG("[AntiDebug] patched steam-stubbed je pattern at 0x%p", (void*)(scan_base + offset));
    }
}

bool femboy::patch::AntiDebugPatcher::Apply()
{
    if (!femboy::g_config.patches.enable_anti_debug)
    {
        LOG("[AntiDebug] disabled");
        return false;
    }

    LOG("[AntiDebug] applying anti-debug patches...");

    patch_peb_being_debugged();
    patch_tls_callbacks();
    patch_crc32_anti_tamper();

    HMODULE h_kernel = GetModuleHandleW(L"kernel32.dll");
    if (h_kernel)
    {
        auto* hk_get_tick_count = new femboy::hook::Detour();
        if (hk_get_tick_count->Install(GetProcAddress(h_kernel, "GetTickCount"), hook_get_tick_count))
        {
            m_hooks.push_back(hk_get_tick_count);
            LOG("[AntiDebug] hooked GetTickCount");
        }
        else delete hk_get_tick_count;

        auto* hk_is_debugger = new femboy::hook::Detour();
        if (hk_is_debugger->Install(GetProcAddress(h_kernel, "IsDebuggerPresent"), hook_is_debugger_present))
        {
            m_hooks.push_back(hk_is_debugger);
            LOG("[AntiDebug] hooked IsDebuggerPresent");
        }
        else delete hk_is_debugger;

        auto* hk_check_remote = new femboy::hook::Detour();
        if (hk_check_remote->Install(GetProcAddress(h_kernel, "CheckRemoteDebuggerPresent"), hook_check_remote_debugger_present))
        {
            m_hooks.push_back(hk_check_remote);
            LOG("[AntiDebug] hooked CheckRemoteDebuggerPresent");
        }
        else delete hk_check_remote;
    }

    HMODULE h_wintrust = GetModuleHandleW(L"wintrust.dll");
    if (h_wintrust)
    {
        auto* hk_wvt = new femboy::hook::Detour();
        if (hk_wvt->Install(GetProcAddress(h_wintrust, "WinVerifyTrust"), hook_win_verify_trust))
        {
            m_hooks.push_back(hk_wvt);
            LOG("[AntiDebug] hooked WinVerifyTrust");
        }
        else delete hk_wvt;
    }

    HMODULE h_ntdll = GetModuleHandleW(L"ntdll.dll");
    if (h_ntdll)
    {
        auto* hk_nt_qsi = new femboy::hook::Detour();
        if (hk_nt_qsi->Install(GetProcAddress(h_ntdll, "NtQuerySystemInformation"), hook_nt_query_system_information))
        {
            m_hooks.push_back(hk_nt_qsi);
            LOG("[AntiDebug] hooked NtQuerySystemInformation");
        }
        else delete hk_nt_qsi;

        auto* hk_nt_qip = new femboy::hook::Detour();
        if (hk_nt_qip->Install(GetProcAddress(h_ntdll, "NtQueryInformationProcess"), hook_nt_query_information_process))
        {
            m_hooks.push_back(hk_nt_qip);
            LOG("[AntiDebug] hooked NtQueryInformationProcess");
        }
        else delete hk_nt_qip;
    }

    m_active = true;
    return true;
}

void femboy::patch::AntiDebugPatcher::remove_all_hooks()
{
    if (!m_active) return;
    for (auto* hook : m_hooks)
    {
        hook->Remove();
        delete hook;
    }
    m_hooks.clear();
    m_active = false;
}
