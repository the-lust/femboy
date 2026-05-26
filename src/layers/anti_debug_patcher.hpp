#pragma once
#include "base.hpp"
#include "hooking.hpp"
#include <vector>

namespace femboy::patch {

class AntiDebugPatcher : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;
    void remove_all_hooks();

private:
    std::vector<femboy::hook::Detour*> m_hooks;
    bool m_active = false;

    bool patch_peb_being_debugged();
    bool patch_tls_callbacks();

    static DWORD __stdcall hook_get_tick_count();
    static BOOL __stdcall hook_is_debugger_present();
    static BOOL __stdcall hook_check_remote_debugger_present(HANDLE h_process, PBOOL pb_debugger_present);
    static LONG __stdcall hook_win_verify_trust(HWND hwnd, GUID* pg_action_id, void* p_wvt_data);
    static HANDLE __stdcall hook_create_toolhelp32_snapshot(DWORD dw_flags, DWORD th32_process_id);

    static NTSTATUS NTAPI hook_nt_query_system_information(
        ULONG system_information_class, void* system_information,
        ULONG system_information_length, PULONG return_length);
    static NTSTATUS NTAPI hook_nt_query_information_process(
        HANDLE process_handle, ULONG process_information_class,
        void* process_information, ULONG process_information_length,
        PULONG return_length);

    static bool patch_crc32_anti_tamper();
    static void patch_caller_patterns(uintptr_t return_address);
};

}
