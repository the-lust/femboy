#include "base.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "memory.hpp"
#include "hooking.hpp"
#include "stub.hpp"
#include "steam.hpp"
#include "steam_handlers.hpp"

// layer headers
#include "gbe_detection.hpp"
#include "plugin_loader.hpp"
#include "cold_client.hpp"
#include "ceg_detection.hpp"
#include "ceg_strategy.hpp"
#include "variant_detector.hpp"
#include "mini_steam_emu.hpp"
#include "anti_debug_patcher.hpp"
#include "stub_monitor.hpp"

NAMESPACE_BEGIN(femboy)

bool g_gbe_detected = false;
bool g_is_ceg_protected = false;
uintptr_t g_oep_addr = 0;
std::atomic<bool> g_oep_confirmed = false;
HMODULE g_femboy_hmodule = nullptr;
bool g_verbose_log = false;

// Layer instance pointers for cleanup
static patch::ColdClient* g_cold_client = nullptr;
static emu::MiniSteamEmu* g_mini_steam_emu = nullptr;
static patch::StubMonitor* g_stub_monitor = nullptr;
static patch::AntiDebugPatcher* g_anti_debug_patcher = nullptr;
static bool g_cold_client_active = false;
static bool g_mini_steam_emu_active = false;
static bool g_stub_monitor_active = false;
static bool g_anti_debug_active = false;

static bool safe_apply_layer(LayerBase* layer)
{
    __try
    {
        LOG("Running layer: %s", layer->Name());
        bool result = layer->Apply();
        LOG("Layer %s: %s", layer->Name(), result ? "success" : "skipped/failed");
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LOG("Layer %s: EXCEPTION 0x%08X", layer->Name(), GetExceptionCode());
        return false;
    }
}

static void cleanup_all()
{
    LOG("=== Cleanup: removing all hooks and restoring state ===");

    if (g_stub_monitor_active && g_stub_monitor)
    {
        g_stub_monitor->remove_all_hooks();
        LOG("[Cleanup] StubMonitor hooks removed");
    }

    if (g_mini_steam_emu_active && g_mini_steam_emu)
    {
        g_mini_steam_emu->remove_all_hooks();
        LOG("[Cleanup] MiniSteamEmu hooks removed (vtables preserved)");
    }

    if (g_anti_debug_active && g_anti_debug_patcher)
    {
        g_anti_debug_patcher->remove_all_hooks();
        LOG("[Cleanup] AntiDebugPatcher hooks removed");
    }

    if (g_cold_client_active && g_cold_client)
    {
        patch::ColdClient::restore_registry();
        LOG("[Cleanup] ColdClient registry restored");
    }

    LOG("=== Cleanup complete ===");
}

static void wait_for_oep_and_cleanup()
{
    LOG("Waiting for OEP to be reached...");
    while (!g_oep_confirmed)
    {
        Sleep(50);
    }

    LOG("OEP confirmed! Cleaning up...");
    cleanup_all();

    LOG("Unloading femboy.dll...");
    FreeLibraryAndExitThread(g_femboy_hmodule, 0);
}

static DWORD WINAPI worker_thread(LPVOID)
{
    LOG("Worker thread started");

    std::vector<std::unique_ptr<LayerBase>> layers;

    layers.push_back(std::make_unique<detect::GbeDetection>());

    if (g_config.patches.enable_plugin_loader)
        layers.push_back(std::make_unique<patch::PluginLoader>());

    if (g_config.patches.enable_cold_client || g_config.cold_client.enable)
    {
        auto cc = std::make_unique<patch::ColdClient>();
        g_cold_client = cc.get();
        g_cold_client_active = true;
        layers.push_back(std::move(cc));
    }

    if (g_config.patches.enable_ceg_detection)
        layers.push_back(std::make_unique<detect::CegDetection>());

    if (g_config.patches.enable_ceg_strategy)
        layers.push_back(std::make_unique<patch::CegStrategy>());

    if (g_config.patches.enable_variant_detection)
        layers.push_back(std::make_unique<stub::VariantDetector>());

    if (g_config.patches.enable_mini_steam_emu && !g_gbe_detected)
    {
        auto me = std::make_unique<emu::MiniSteamEmu>();
        g_mini_steam_emu = me.get();
        g_mini_steam_emu_active = true;
        layers.push_back(std::move(me));
    }

    if (g_config.patches.enable_anti_debug)
    {
        auto ad = std::make_unique<patch::AntiDebugPatcher>();
        g_anti_debug_patcher = ad.get();
        g_anti_debug_active = true;
        layers.push_back(std::move(ad));
    }

    if (g_config.patches.enable_oep_monitor)
    {
        auto sm = std::make_unique<patch::StubMonitor>();
        g_stub_monitor = sm.get();
        g_stub_monitor_active = true;
        layers.push_back(std::move(sm));
    }

    LOG("Executing %zu layers sequentially", layers.size());

    for (auto& layer : layers)
    {
        bool ok = safe_apply_layer(layer.get());
        if (ok && g_config.patches.stop_after_first_success)
        {
            LOG("StopAfterFirstSuccess: stopping after %s", layer->Name());
            break;
        }
    }

    if (g_config.patches.enable_oep_monitor)
    {
        wait_for_oep_and_cleanup();
    }
    else
    {
        LOG("Worker thread finished - all layers completed (no OEP monitor)");
    }

    return 0;
}

// ok but for real tho this is the entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_femboy_hmodule = hModule;

        DisableThreadLibraryCalls(hModule);

        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::wstring log_path = exe_path;
        size_t pos = log_path.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            log_path = log_path.substr(0, pos + 1);
        log_path += L"femboy.log";

        Logger::Instance().Init(log_path.c_str());
        Logger::Instance().SetVerbose(false);

        LOG("==========================================");
        LOG("femboy.dll v1.0.0 loaded");
        LOG("Architecture: %s", sizeof(void*) == 8 ? "x64" : "x86");
        LOG("Process: %ws", exe_path);

        std::wstring ini_path = log_path;
        ini_path = ini_path.substr(0, ini_path.find_last_of(L"\\/") + 1) + L"femboy.ini";
        load_config(ini_path.c_str());

        g_verbose_log = g_config.debug.verbose_log;
        Logger::Instance().SetVerbose(g_verbose_log);

        HANDLE h_thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
        if (h_thread)
        {
            CloseHandle(h_thread);
            LOG("Worker thread spawned");
        }
        else
        {
            LOG("FATAL: CreateThread failed (err=%d)", GetLastError());
        }

        return TRUE;
    }

    case DLL_PROCESS_DETACH:
        LOG("DLL_PROCESS_DETACH");
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}

NAMESPACE_END(femboy)
