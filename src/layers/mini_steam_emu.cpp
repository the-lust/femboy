#include "mini_steam_emu.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "steam.hpp"
#include "memory.hpp"
#include "steam_handlers.hpp"
#include "ceg_strategy.hpp"

static bool(__stdcall* Real_SteamAPI_Init)() = nullptr;
static void(__stdcall* Real_SteamAPI_Shutdown)() = nullptr;
static bool(__stdcall* Real_SteamAPI_RestartAppIfNecessary)(uint32_t) = nullptr;
static uint32_t(__stdcall* Real_SteamAPI_GetHSteamUser)() = nullptr;
static uint32_t(__stdcall* Real_SteamAPI_GetHSteamPipe)() = nullptr;
static bool(__stdcall* Real_SteamAPI_IsSteamRunning)() = nullptr;
static void* (__stdcall* Real_CreateInterface)(const char*, int*) = nullptr;

bool __stdcall femboy::emu::MiniSteamEmu::hook_steam_api_init()
{
    LOG("[MiniSteamEmu] SteamAPI_Init -> true");
    return true;
}

void __stdcall femboy::emu::MiniSteamEmu::hook_steam_api_shutdown()
{
    LOG("[MiniSteamEmu] SteamAPI_Shutdown -> no-op");
}

bool __stdcall femboy::emu::MiniSteamEmu::hook_steam_api_restart_app_if_necessary(uint32_t un_own_app_id)
{
    (void)un_own_app_id;
    LOG("[MiniSteamEmu] SteamAPI_RestartAppIfNecessary(%u) -> false", un_own_app_id);
    return false;
}

uint32_t __stdcall femboy::emu::MiniSteamEmu::hook_steam_api_get_h_steam_user()
{
    return 0x10001;
}

uint32_t __stdcall femboy::emu::MiniSteamEmu::hook_steam_api_get_h_steam_pipe()
{
    return 0x10002;
}

bool __stdcall femboy::emu::MiniSteamEmu::hook_steam_api_is_steam_running()
{
    return true;
}

void* __stdcall femboy::emu::MiniSteamEmu::hook_create_interface(const char* pch_version, int* p_return_code)
{
    LOG("[MiniSteamEmu] CreateInterface(%s)", pch_version ? pch_version : "null");

    if (p_return_code) *p_return_code = 0;

    void* fake_vtable = femboy::steam::g_interface_lookup.create_fake_vtable(pch_version);
    if (fake_vtable)
    {
        LOG("[MiniSteamEmu] returning fake vtable %p for %s (%zu methods)",
            fake_vtable, pch_version,
            femboy::steam::g_interface_lookup.find(pch_version) ?
            femboy::steam::g_interface_lookup.find(pch_version)->methods.size() : 0);
        return fake_vtable;
    }

    LOG("[MiniSteamEmu] unknown interface, falling through");
    if (Real_CreateInterface)
        return Real_CreateInterface(pch_version, p_return_code);
    if (p_return_code) *p_return_code = 1;
    return nullptr;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_b_is_subscribed(void* this_ptr)
{
    (void)this_ptr;
    LOG("[MiniSteamEmu] BIsSubscribed -> true");
    return true;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_b_is_low_violence(void* this_ptr)
{
    (void)this_ptr;
    return false;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_b_is_cybercafe(void* this_ptr)
{
    (void)this_ptr;
    return false;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_b_is_subscribed_app(void* this_ptr, int edx, uint32_t n_app_id)
{
    (void)this_ptr; (void)edx;
    uint32_t cfg_app_id = femboy::g_config.cold_client.app_id;
    if (cfg_app_id == 0)
    {
        std::ifstream f("steam_appid.txt");
        if (f) f >> cfg_app_id;
    }
    bool result = (n_app_id == cfg_app_id);
    LOG("[MiniSteamEmu] BIsSubscribedApp(%u) -> %s", n_app_id, result ? "true" : "false");
    return result;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_b_is_dlc_installed(void* this_ptr, int edx, uint32_t n_app_id)
{
    (void)this_ptr; (void)edx; (void)n_app_id;
    LOG("[MiniSteamEmu] BIsDlcInstalled(%u) -> true", n_app_id);
    return true;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_b_logged_on(void* this_ptr)
{
    (void)this_ptr;
    return true;
}

uint64_t __fastcall femboy::emu::MiniSteamEmu::hook_get_steam_id(void* this_ptr, int edx)
{
    (void)this_ptr; (void)edx;
    LOG("[MiniSteamEmu] GetSteamID -> %llu", femboy::g_config.steam_emu.steam_id);
    return femboy::g_config.steam_emu.steam_id;
}

uint32_t __fastcall femboy::emu::MiniSteamEmu::hook_get_app_id(void* this_ptr)
{
    (void)this_ptr;
    uint32_t app_id = femboy::g_config.cold_client.app_id;
    if (app_id == 0)
    {
        std::ifstream f("steam_appid.txt");
        if (f) f >> app_id;
    }
    LOG("[MiniSteamEmu] GetAppID -> %u", app_id);
    return app_id;
}

uint32_t __fastcall femboy::emu::MiniSteamEmu::hook_get_connected_universe(void* this_ptr)
{
    (void)this_ptr;
    return 1;
}

uint32_t __fastcall femboy::emu::MiniSteamEmu::hook_get_server_real_time(void* this_ptr)
{
    (void)this_ptr;
    return (uint32_t)time(nullptr);
}

uint32_t __fastcall femboy::emu::MiniSteamEmu::hook_user_has_license_for_app(void* this_ptr, int edx, void* h_user, uint32_t n_app_id)
{
    (void)this_ptr; (void)edx; (void)h_user; (void)n_app_id;
    LOG("[MiniSteamEmu] UserHasLicenseForApp -> HasLicense");
    return 3;
}

uint32_t __fastcall femboy::emu::MiniSteamEmu::hook_get_app_ownership_ticket_data(void* this_ptr, int edx,
    void* h_ticket, void* p_ticket, uint32_t cb_max_ticket,
    uint32_t* pcb_ticket, bool* pb_owned, void* p_sig, uint32_t cb_sig)
{
    (void)this_ptr; (void)edx;
    (void)h_ticket; (void)p_sig; (void)cb_sig;

    if (femboy::g_is_ceg_protected)
    {
        LOG("[MiniSteamEmu] GetAppOwnershipTicketData -> generating CEG ticket");
        femboy::patch::CegStrategy::generate_ticket_data(
            (uint8_t*)p_ticket, cb_max_ticket, pcb_ticket, pb_owned);
        return 0;
    }

    LOG("[MiniSteamEmu] GetAppOwnershipTicketData -> 0 (no ticket)");
    if (pcb_ticket) *pcb_ticket = 0;
    if (pb_owned) *pb_owned = false;
    return 0;
}

const char* __fastcall femboy::emu::MiniSteamEmu::hook_get_current_game_language(void* this_ptr)
{
    (void)this_ptr;
    return "english";
}

uint64_t __fastcall femboy::emu::MiniSteamEmu::hook_get_app_owner(void* this_ptr)
{
    (void)this_ptr;
    return femboy::g_config.steam_emu.steam_id;
}

uint32_t __fastcall femboy::emu::MiniSteamEmu::hook_get_dlc_count(void* this_ptr)
{
    (void)this_ptr;
    return 0;
}

bool __fastcall femboy::emu::MiniSteamEmu::hook_is_overlay_enabled(void* this_ptr)
{
    (void)this_ptr;
    return false;
}

bool femboy::emu::MiniSteamEmu::find_and_hook_steam_module(const wchar_t* module_name)
{
    HMODULE h_mod = GetModuleHandleW(module_name);
    if (!h_mod) return false;

    void* create_interface = (void*)GetProcAddress(h_mod, "CreateInterface");
    if (create_interface)
    {
        auto* hook = new femboy::hook::Detour();
        if (hook->Install(create_interface, hook_create_interface))
        {
            m_hooks.push_back(hook);
            LOG("[MiniSteamEmu] hooked CreateInterface in %ls", module_name);
        }
        else
        {
            delete hook;
        }
    }

    femboy::hook::install_iat_hook_for_all_modules("steam_api", "SteamAPI_Init", hook_steam_api_init, (void**)&Real_SteamAPI_Init);
    femboy::hook::install_iat_hook_for_all_modules("steam_api", "SteamAPI_Shutdown", hook_steam_api_shutdown, (void**)&Real_SteamAPI_Shutdown);
    femboy::hook::install_iat_hook_for_all_modules("steam_api", "SteamAPI_RestartAppIfNecessary", hook_steam_api_restart_app_if_necessary, (void**)&Real_SteamAPI_RestartAppIfNecessary);
    femboy::hook::install_iat_hook_for_all_modules("steam_api", "SteamAPI_GetHSteamUser", hook_steam_api_get_h_steam_user, (void**)&Real_SteamAPI_GetHSteamUser);
    femboy::hook::install_iat_hook_for_all_modules("steam_api", "SteamAPI_GetHSteamPipe", hook_steam_api_get_h_steam_pipe, (void**)&Real_SteamAPI_GetHSteamPipe);
    femboy::hook::install_iat_hook_for_all_modules("steam_api", "SteamAPI_IsSteamRunning", hook_steam_api_is_steam_running, (void**)&Real_SteamAPI_IsSteamRunning);

    return true;
}

const char* femboy::emu::MiniSteamEmu::Name()
{
    return "mini_steam_emu";
}

bool femboy::emu::MiniSteamEmu::Apply()
{
    if (femboy::g_gbe_detected)
    {
        LOG("[MiniSteamEmu] GBE detected -- skipping MiniSteamEmu fr fr");
        return false;
    }

    if (!femboy::g_config.patches.enable_mini_steam_emu)
    {
        LOG("[MiniSteamEmu] disabled");
        return false;
    }

    LOG("[MiniSteamEmu] initializing Steam API emulation...");

    if (femboy::steam::g_interface_lookup.get_count() == 0)
    {
        if (!femboy::steam::g_interface_lookup.load_from_resource(100))
        {
            if (!femboy::steam::g_interface_lookup.load_from_file(L"femboy_interfaces.json"))
            {
                LOG("[MiniSteamEmu] no interface lookup data available!");
            }
        }
    }
    LOG("[MiniSteamEmu] loaded %zu interface versions", femboy::steam::g_interface_lookup.get_count());

    register_all_handlers();

    find_and_hook_steam_module(L"steam_api.dll");
    find_and_hook_steam_module(L"steam_api64.dll");

    if (femboy::g_config.cold_client.enable)
    {
        find_and_hook_steam_module(femboy::g_config.cold_client.steam_client_dll.c_str());
    }

    m_active = true;
    LOG("[MiniSteamEmu] hooks installed successfully");
    return true;
}

void femboy::emu::MiniSteamEmu::remove_all_hooks()
{
    if (!m_active) return;
    LOG("[MiniSteamEmu] removing hooks...");
    for (auto* hook : m_hooks)
    {
        hook->Remove();
        delete hook;
    }
    m_hooks.clear();
    m_active = false;
}

void femboy::emu::MiniSteamEmu::detach_fake_vtables()
{
    femboy::steam::g_interface_lookup.cleanup_all_fake_vtables();
}

void femboy::emu::MiniSteamEmu::register_all_handlers()
{
    femboy::steam::register_handler("BIsSubscribed", (void*)hook_b_is_subscribed);
    femboy::steam::register_handler("BIsLowViolence", (void*)hook_b_is_low_violence);
    femboy::steam::register_handler("BIsCybercafe", (void*)hook_b_is_cybercafe);
    femboy::steam::register_handler("BIsSubscribedApp", (void*)hook_b_is_subscribed_app);
    femboy::steam::register_handler("BIsDlcInstalled", (void*)hook_b_is_dlc_installed);
    femboy::steam::register_handler("BLoggedOn", (void*)hook_b_logged_on);
    femboy::steam::register_handler("GetSteamID", (void*)hook_get_steam_id);
    femboy::steam::register_handler("GetAppID", (void*)hook_get_app_id);
    femboy::steam::register_handler("GetConnectedUniverse", (void*)hook_get_connected_universe);
    femboy::steam::register_handler("GetServerRealTime", (void*)hook_get_server_real_time);
    femboy::steam::register_handler("UserHasLicenseForApp", (void*)hook_user_has_license_for_app);
    femboy::steam::register_handler("GetAppOwnershipTicketData", (void*)hook_get_app_ownership_ticket_data);
    femboy::steam::register_handler("GetCurrentGameLanguage", (void*)hook_get_current_game_language);
    femboy::steam::register_handler("GetAppOwner", (void*)hook_get_app_owner);
    femboy::steam::register_handler("GetDLCCount", (void*)hook_get_dlc_count);
    femboy::steam::register_handler("IsOverlayEnabled", (void*)hook_is_overlay_enabled);
    LOG("[MiniSteamEmu] registered %zu vtable method handlers", 16);
    femboy::steam::register_all_steam_handlers();
}
