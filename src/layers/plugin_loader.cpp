#include "plugin_loader.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"

const char* femboy::patch::PluginLoader::Name()
{
    return "plugin_loader";
}

bool femboy::patch::PluginLoader::Apply()
{
    if (!femboy::g_config.patches.enable_plugin_loader)
    {
        LOG("[PluginLoader] disabled");
        return false;
    }

    std::wstring load_path = femboy::g_config.plugin_loader.load_path;
    if (load_path.empty()) load_path = L"steam_settings\\load_dlls\\";

    LOG("[PluginLoader] scanning: %ls", load_path.c_str());

    // load order? lowkey nobody uses this
    std::wstring search_path = load_path + L"*.dll";
    WIN32_FIND_DATAW ffd;
    HANDLE h_find = FindFirstFileW(search_path.c_str(), &ffd);
    if (h_find == INVALID_HANDLE_VALUE)
    {
        LOG("[PluginLoader] no DLLs found");
        return false;
    }

    do
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            std::wstring full_path = load_path + ffd.cFileName;
            HMODULE h_plugin = LoadLibraryW(full_path.c_str());
            if (h_plugin)
                LOG("[PluginLoader] loaded: %ls", ffd.cFileName);
            else
                LOG("[PluginLoader] FAILED: %ls", ffd.cFileName);
        }
    } while (FindNextFileW(h_find, &ffd));

    FindClose(h_find);
    return true;
}
