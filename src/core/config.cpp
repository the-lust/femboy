#include "config.hpp"

NAMESPACE_BEGIN(femboy)

Config g_config;

static std::wstring get_ini_string(const wchar_t* section, const wchar_t* key, const wchar_t* def, const wchar_t* path)
{
    wchar_t buf[1024] = { 0 };
    GetPrivateProfileStringW(section, key, def, buf, 1024, path);
    return buf;
}

static int get_ini_int(const wchar_t* section, const wchar_t* key, int def, const wchar_t* path)
{
    return GetPrivateProfileIntW(section, key, def, path);
}

static uint64_t get_ini_uint64(const wchar_t* section, const wchar_t* key, uint64_t def, const wchar_t* path)
{
    wchar_t buf[32] = { 0 };
    GetPrivateProfileStringW(section, key, L"", buf, 32, path);
    if (buf[0] == L'\0') return def;
    // definately not the safest way to parse but it works
    return _wtoi64(buf);
}

bool load_config(const wchar_t* path)
{
    g_config.patches.enable_plugin_loader = !!get_ini_int(L"Patches", L"EnablePluginLoader", 1, path);
    g_config.patches.enable_cold_client = !!get_ini_int(L"Patches", L"EnableColdClient", 0, path);
    g_config.patches.enable_ceg_detection = !!get_ini_int(L"Patches", L"EnableCEGDetection", 1, path);
    g_config.patches.enable_ceg_strategy = !!get_ini_int(L"Patches", L"EnableCEGStrategy", 1, path);
    g_config.patches.enable_variant_detection = !!get_ini_int(L"Patches", L"EnableVariantDetection", 1, path);
    g_config.patches.enable_mini_steam_emu = !!get_ini_int(L"Patches", L"EnableMiniSteamEmu", 0, path);
    g_config.patches.enable_anti_debug = !!get_ini_int(L"Patches", L"EnableAntiDebug", 1, path);
    g_config.patches.enable_oep_monitor = !!get_ini_int(L"Patches", L"EnableOEPMonitor", 1, path);
    g_config.patches.stop_after_first_success = !!get_ini_int(L"Patches", L"StopAfterFirstSuccess", 0, path);
    g_config.patches.entry_point_rva = get_ini_int(L"Patches", L"EntryPointRVA", 0, path);

    g_config.cold_client.enable = !!get_ini_int(L"ColdClient", L"Enable", 0, path);
    g_config.cold_client.app_id = get_ini_int(L"ColdClient", L"AppId", 0, path);
    g_config.cold_client.steam_client_dll = get_ini_string(L"ColdClient", L"SteamClientDll", L"steamclient64.dll", path);
    g_config.cold_client.game_exe_path = get_ini_string(L"ColdClient", L"GameExePath", L"", path);

    g_config.steam_emu.steam_id = get_ini_uint64(L"SteamEmu", L"SteamId", 76561197960287930ULL, path);
    g_config.steam_emu.user_name = get_ini_string(L"SteamEmu", L"UserName", L"Player", path);
    g_config.steam_emu.language = get_ini_string(L"SteamEmu", L"Language", L"english", path);

    g_config.ceg.chunk_path = get_ini_string(L"CEG", L"ChunkPath", L"ceg_chunks\\", path);
    g_config.ceg.bypass_ticket_validation = !!get_ini_int(L"CEG", L"BypassTicketValidation", 1, path);
    g_config.ceg.intercept_code_downloads = !!get_ini_int(L"CEG", L"InterceptCodeDownloads", 1, path);

    g_config.plugin_loader.load_path = get_ini_string(L"PluginLoader", L"LoadPath", L"steam_settings\\load_dlls\\", path);
    g_config.plugin_loader.load_order = get_ini_string(L"PluginLoader", L"LoadOrder", L"", path);

    g_config.debug.verbose_log = !!get_ini_int(L"Debug", L"VerboseLog", 0, path);
    return true;
}

NAMESPACE_END(femboy)
