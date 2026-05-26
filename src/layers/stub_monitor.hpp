#pragma once
#include "base.hpp"
#include "hooking.hpp"
#include "stub.hpp"
#include <vector>

namespace femboy::patch {

class StubMonitor : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;
    void remove_all_hooks();

private:
    std::vector<femboy::hook::Detour*> m_hooks;
    bool m_active = false;

    static LONG CALLBACK vex_handler(PEXCEPTION_POINTERS p_except);
    static void* m_guard_page_base;
    static size_t m_guard_page_size;
    static void* m_veh_handle;

    static int __stdcall hook_init_security_cookie();
    static bool find_oep_in_stub(uintptr_t& oep);
    static void set_guard_page(uintptr_t oep);
    static bool is_in_executable_code(uintptr_t addr);
};

}
