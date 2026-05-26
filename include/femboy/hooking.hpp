#pragma once
#include "framework.hpp"

namespace femboy { namespace hook {

struct HookInfo
{
    void** target_ptr;
    uint8_t original_bytes[14];
    size_t hook_size;
    bool active;
};

class Detour
{
public:
    Detour();
    ~Detour();

    bool Install(void* target, void* detour);
    bool Remove();
    bool IsActive() const { return m_info.active; }
    void* GetTarget() const { return m_target; }

private:
    void* m_target;
    void* m_detour;
    HookInfo m_info;
};

bool install_iat_hook(const wchar_t* target_module, const char* export_prefix, const char* func_name, void* detour, void** original);
bool install_iat_hook_for_all_modules(const char* export_prefix, const char* func_name, void* detour, void** original);
bool remove_iat_hook(const wchar_t* module_name, const char* func_name);

} }
