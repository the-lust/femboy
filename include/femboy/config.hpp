#pragma once

#include "framework.hpp"

NAMESPACE_BEGIN(femboy)

struct Config
{
    struct
    {
        bool enable_plugin_loader = true;
        bool enable_cold_client = false;
        bool enable_ceg_detection = true;
        bool enable_ceg_strategy = true;
        bool enable_variant_detection = true;
        bool enable_mini_steam_emu = false;
        bool enable_anti_debug = true;
        bool enable_oep_monitor = true;
        bool stop_after_first_success = false;
        uint32_t entry_point_rva = 0;
    } patches;

    struct
    {
        bool enable = false;
        uint32_t app_id = 0;
        std::wstring steam_client_dll = L"steamclient64.dll";
        std::wstring game_exe_path;
    } cold_client;

    struct
    {
        uint64_t steam_id = 76561197960287930ULL;
        std::wstring user_name = L"Player";
        std::wstring language = L"english";
    } steam_emu;

    struct
    {
        std::wstring chunk_path = L"ceg_chunks\\";
        bool bypass_ticket_validation = true;
        bool intercept_code_downloads = true;
    } ceg;

    struct
    {
        std::wstring load_path = L"steam_settings\\load_dlls\\";
        std::wstring load_order;
    } plugin_loader;

    struct
    {
        bool verbose_log = false;
    } debug;
};

extern Config g_config;
bool load_config(const wchar_t* path = L"femboy.ini");

NAMESPACE_END(femboy)
