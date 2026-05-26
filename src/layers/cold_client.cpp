#include "cold_client.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"

std::vector<femboy::patch::ColdClient::RegBackup> femboy::patch::ColdClient::s_reg_backups;
bool femboy::patch::ColdClient::s_has_backup = false;

const char* femboy::patch::ColdClient::Name()
{
    return "cold_client";
}

// registry stuff is always a pain
bool femboy::patch::ColdClient::backup_and_write(const wchar_t* sub_key, const wchar_t* value_name,
    DWORD type, const void* data, DWORD data_size)
{
    HKEY h_key;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, sub_key, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &h_key, nullptr);
    if (result != ERROR_SUCCESS) return false;

    RegBackup backup;
    backup.h_key = h_key;
    backup.sub_key = sub_key;
    backup.value_name = value_name;
    backup.type = type;

    DWORD existing_size = 0;
    result = RegQueryValueExW(h_key, value_name, nullptr, &backup.type, nullptr, &existing_size);
    if (result == ERROR_SUCCESS && existing_size > 0)
    {
        backup.data.resize(existing_size);
        RegQueryValueExW(h_key, value_name, nullptr, &backup.type,
            backup.data.data(), &existing_size);
    }
    s_reg_backups.push_back(std::move(backup));

    result = RegSetValueExW(h_key, value_name, 0, type,
        (const BYTE*)data, data_size);
    RegCloseKey(h_key);

    return result == ERROR_SUCCESS;
}

bool femboy::patch::ColdClient::Apply()
{
    if (!femboy::g_config.cold_client.enable)
    {
        LOG("[ColdClient] disabled");
        return false;
    }

    LOG("[ColdClient] applying registry patches");

    DWORD app_id = femboy::g_config.cold_client.app_id;
    if (app_id == 0)
    {
        std::ifstream f("steam_appid.txt");
        if (f) f >> app_id;
    }

    if (app_id > 0)
    {
        backup_and_write(L"Software\\Valve\\Steam", L"ActiveProcess",
            REG_DWORD, &app_id, sizeof(app_id));
        backup_and_write(L"Software\\Valve\\Steam", L"RunningAppID",
            REG_DWORD, &app_id, sizeof(app_id));

        wchar_t game_dir[MAX_PATH];
        GetModuleFileNameW(nullptr, game_dir, MAX_PATH);
        wchar_t* last_slash = wcsrchr(game_dir, L'\\');
        if (last_slash) *last_slash = L'\0';
        backup_and_write(L"Software\\Valve\\Steam", L"SteamPath",
            REG_SZ, game_dir, (DWORD)((wcslen(game_dir) + 1) * sizeof(wchar_t)));

        LOG("[ColdClient] wrote AppId=%u, SteamPath=%ls", app_id, game_dir);
    }

    s_has_backup = true;
    return true;
}

// yooo we gotta clean up after ourselves so Steam doesn't get confused later
void femboy::patch::ColdClient::restore_registry()
{
    if (!s_has_backup) return;
    LOG("[ColdClient] restoring registry backups");

    for (auto& backup : s_reg_backups)
    {
        HKEY h_key;
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, backup.sub_key.c_str(),
            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &h_key, nullptr);
        if (result != ERROR_SUCCESS) continue;

        if (backup.data.empty())
        {
            RegDeleteValueW(h_key, backup.value_name.c_str());
        }
        else
        {
            RegSetValueExW(h_key, backup.value_name.c_str(), 0, backup.type,
                backup.data.data(), (DWORD)backup.data.size());
        }
        RegCloseKey(h_key);
    }

    s_reg_backups.clear();
    s_has_backup = false;
}
