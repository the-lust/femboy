#include "gbe_detection.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"

bool femboy::detect::GbeDetection::Apply()
{
    LOG("[GbeDetection] Scanning for GBE emulators...");

    wchar_t game_path[MAX_PATH];
    GetModuleFileNameW(nullptr, game_path, MAX_PATH);
    wchar_t game_dir[MAX_PATH];
    wcsncpy_s(game_dir, game_path, MAX_PATH);
    wchar_t* last_slash = wcsrchr(game_dir, L'\\');
    if (last_slash) *last_slash = L'\0';

    wchar_t steam_settings_path[MAX_PATH];
    wcsncpy_s(steam_settings_path, game_dir, MAX_PATH);
    wcscat_s(steam_settings_path, L"\\steam_settings");
    if (GetFileAttributesW(steam_settings_path) != INVALID_FILE_ATTRIBUTES &&
        (GetFileAttributesW(steam_settings_path) & FILE_ATTRIBUTE_DIRECTORY))
    {
        LOG("[GbeDetection] steam_settings directory found in game folder -- GBE indicator");
        g_gbe_detected = true;
        return true;
    }

    wchar_t real_steam_path[MAX_PATH] = {0};
    HKEY h_key = nullptr;
    bool has_real_steam_path = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                      0, KEY_READ | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS)
    {
        DWORD type = 0, size = sizeof(real_steam_path);
        if (RegQueryValueExW(h_key, L"InstallPath", nullptr, &type, (LPBYTE)real_steam_path, &size) == ERROR_SUCCESS)
            has_real_steam_path = true;
        RegCloseKey(h_key);
    }
    if (!has_real_steam_path)
    {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
                          0, KEY_READ, &h_key) == ERROR_SUCCESS)
        {
            DWORD type = 0, size = sizeof(real_steam_path);
            if (RegQueryValueExW(h_key, L"SteamPath", nullptr, &type, (LPBYTE)real_steam_path, &size) == ERROR_SUCCESS)
                has_real_steam_path = true;
            RegCloseKey(h_key);
        }
    }

    wchar_t sys_path[MAX_PATH];
    GetSystemDirectoryW(sys_path, MAX_PATH);
    size_t sys_path_len = wcslen(sys_path);

    HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (h_snap == INVALID_HANDLE_VALUE)
    {
        LOG("[GbeDetection] snapshot failed");
        return false;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(h_snap, &me))
    {
        do
        {
            // gbe detection goes hard
            std::wstring name(me.szModule);
            for (auto& c : name) c = towlower(c);

            bool is_steam_api = (name.find(L"steam_api") != std::wstring::npos);
            bool is_steam_client = (name.find(L"steamclient") != std::wstring::npos);

            bool is_known_gbe = (
                name.find(L"greenluma") != std::wstring::npos ||
                name.find(L"revolt") != std::wstring::npos ||
                name.find(L"sse") == 0 ||
                name.find(L"smartsteamemu") != std::wstring::npos ||
                name.find(L"goldberg") != std::wstring::npos ||
                name.find(L"steamemu") != std::wstring::npos ||
                name.find(L"emulator") != std::wstring::npos ||
                name.find(L"crack") != std::wstring::npos
            );

            if (is_known_gbe)
            {
                LOG("[GbeDetection] Known GBE DLL detected: %ls", me.szModule);
                g_gbe_detected = true;
                CloseHandle(h_snap);
                return true;
            }

            if (!is_steam_api && !is_steam_client) continue;

            wchar_t mod_path[MAX_PATH];
            GetModuleFileNameExW(GetCurrentProcess(), me.hModule, mod_path, MAX_PATH);
            std::wstring mod_dir(mod_path);
            size_t slash_pos = mod_dir.rfind(L'\\');
            if (slash_pos != std::wstring::npos)
                mod_dir = mod_dir.substr(0, slash_pos);

            if (has_real_steam_path)
            {
                if (_wcsnicmp(mod_dir.c_str(), real_steam_path, wcslen(real_steam_path)) == 0)
                {
                    LOG("[GbeDetection] %ls is from real Steam installation -- skipping", me.szModule);
                    continue;
                }
            }

            if (_wcsnicmp(mod_dir.c_str(), sys_path, sys_path_len) == 0)
            {
                LOG("[GbeDetection] %ls is from System32 -- skipping", me.szModule);
                continue;
            }

            if (_wcsicmp(mod_dir.c_str(), game_dir) == 0 ||
                mod_dir.find(L"steam_settings") != std::wstring::npos)
            {
                LOG("[GbeDetection] GBE detected: %ls in game directory", me.szModule);
                g_gbe_detected = true;
                CloseHandle(h_snap);
                return true;
            }

            HMODULE h_mod = GetModuleHandleW(me.szModule);
            if (h_mod)
            {
                if (GetProcAddress(h_mod, "gbe_SteamAPI_Init"))
                {
                    LOG("[GbeDetection] GBE detected via gbe_ export: %ls", me.szModule);
                    g_gbe_detected = true;
                    CloseHandle(h_snap);
                    return true;
                }
            }

            if (has_real_steam_path && is_steam_api)
            {
                bool from_steam = (_wcsnicmp(mod_dir.c_str(), real_steam_path, wcslen(real_steam_path)) == 0);
                bool from_game = (_wcsicmp(mod_dir.c_str(), game_dir) == 0);
                bool from_system = (_wcsnicmp(mod_dir.c_str(), sys_path, sys_path_len) == 0);
                if (!from_steam && !from_game && !from_system)
                {
                    LOG("[GbeDetection] Suspicious steam_api DLL location: %ls (not Steam/game/system)", mod_path);
                    g_gbe_detected = true;
                    CloseHandle(h_snap);
                    return true;
                }
            }

        } while (Module32NextW(h_snap, &me));
    }

    CloseHandle(h_snap);
    LOG("[GbeDetection] No GBE emulator found");
    return false;
}

const char* femboy::detect::GbeDetection::Name()
{
    return "gbe_detection";
}
