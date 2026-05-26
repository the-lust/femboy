#pragma once
#include "base.hpp"
#include "hooking.hpp"
#include <vector>
#include <unordered_map>

namespace femboy::emu {

class MiniSteamEmu : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;
    void remove_all_hooks();
    void detach_fake_vtables();

private:
    std::vector<femboy::hook::Detour*> m_hooks;
    std::vector<void*> m_fake_vtables;
    bool m_active = false;

    static bool __stdcall hook_steam_api_init();
    static void __stdcall hook_steam_api_shutdown();
    static bool __stdcall hook_steam_api_restart_app_if_necessary(uint32_t un_own_app_id);
    static uint32_t __stdcall hook_steam_api_get_h_steam_user();
    static uint32_t __stdcall hook_steam_api_get_h_steam_pipe();
    static bool __stdcall hook_steam_api_is_steam_running();

    static void* __stdcall hook_create_interface(const char* pch_version, int* p_return_code);

    static bool __fastcall hook_b_is_subscribed(void* this_ptr);
    static bool __fastcall hook_b_is_low_violence(void* this_ptr);
    static bool __fastcall hook_b_is_cybercafe(void* this_ptr);
    static bool __fastcall hook_b_is_subscribed_app(void* this_ptr, int edx, uint32_t n_app_id);
    static bool __fastcall hook_b_is_dlc_installed(void* this_ptr, int edx, uint32_t n_app_id);
    static bool __fastcall hook_b_logged_on(void* this_ptr);
    static uint64_t __fastcall hook_get_steam_id(void* this_ptr, int edx);
    static uint32_t __fastcall hook_get_app_id(void* this_ptr);
    static uint32_t __fastcall hook_get_connected_universe(void* this_ptr);
    static uint32_t __fastcall hook_get_server_real_time(void* this_ptr);
    static uint32_t __fastcall hook_user_has_license_for_app(void* this_ptr, int edx, void* h_user, uint32_t n_app_id);
    static uint32_t __fastcall hook_get_app_ownership_ticket_data(void* this_ptr, int edx, void* h_ticket, void* p_ticket, uint32_t cb_max_ticket, uint32_t* pcb_ticket, bool* pb_owned, void* p_sig, uint32_t cb_sig);
    static const char* __fastcall hook_get_current_game_language(void* this_ptr);
    static uint64_t __fastcall hook_get_app_owner(void* this_ptr);
    static uint32_t __fastcall hook_get_dlc_count(void* this_ptr);
    static bool __fastcall hook_is_overlay_enabled(void* this_ptr);

    bool find_and_hook_steam_module(const wchar_t* module_name);
    void register_all_handlers();
};

}
