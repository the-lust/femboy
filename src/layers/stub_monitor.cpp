#include "stub_monitor.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "memory.hpp"

static int(__stdcall* Real_InitSecurityCookie)() = nullptr;

void* femboy::patch::StubMonitor::m_guard_page_base = nullptr;
size_t femboy::patch::StubMonitor::m_guard_page_size = 0;
void* femboy::patch::StubMonitor::m_veh_handle = nullptr;
static int g_guard_page_hits = 0;

const char* femboy::patch::StubMonitor::Name()
{
    return "stub_monitor";
}

bool femboy::patch::StubMonitor::is_in_executable_code(uintptr_t addr)
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uintptr_t start = base + sec[i].VirtualAddress;
        uintptr_t end = start + sec[i].Misc.VirtualSize;
        if (addr >= start && addr < end) return true;
    }
    return false;
}

LONG CALLBACK femboy::patch::StubMonitor::vex_handler(PEXCEPTION_POINTERS p_except)
{
    if (p_except->ExceptionRecord->ExceptionCode != STATUS_GUARD_PAGE_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    uintptr_t fault_addr = (uintptr_t)p_except->ExceptionRecord->ExceptionInformation[1];
    uintptr_t guard_start = (uintptr_t)m_guard_page_base;
    uintptr_t guard_end = guard_start + m_guard_page_size;

    if (fault_addr >= guard_start && fault_addr < guard_end)
    {
        LOG("[StubMonitor] VEH: guard page hit #%d at %p", g_guard_page_hits + 1, (void*)fault_addr);

        uint8_t prologue[4] = {0};
        read_memory_safe(fault_addr, prologue, sizeof(prologue));
        bool valid_prologue = (
            (prologue[0] == 0x55) ||
            (prologue[0] == 0x48 && prologue[1] == 0x89 && prologue[2] == 0xE5) ||
            (prologue[0] == 0xE9) ||
            (prologue[0] == 0xCC) ||
            (prologue[0] == 0x31 && prologue[1] == 0xC0) ||
            (prologue[0] == 0xB8) ||
            (prologue[0] == 0x48 && prologue[1] == 0x83 && prologue[2] == 0xEC) ||
            (prologue[0] == 0x83 && prologue[1] == 0xEC)
        );

        if (!valid_prologue)
        {
            g_guard_page_hits++;
            LOG("[StubMonitor] OEP not yet valid (prologue check failed, hit #%d)", g_guard_page_hits);

            // TODO: this guard page counter is kinda jank, fix later
            if (g_guard_page_hits >= 10)
            {
                LOG("[StubMonitor] guard page hit limit reached -- forcing OEP confirmation");
                g_oep_addr = fault_addr;
                g_oep_confirmed = true;
                RemoveVectoredExceptionHandler(m_veh_handle);
                m_veh_handle = nullptr;
                DWORD old;
                VirtualProtect(m_guard_page_base, m_guard_page_size, PAGE_EXECUTE_READ, &old);
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            DWORD old;
            VirtualProtect(m_guard_page_base, m_guard_page_size, PAGE_EXECUTE_READ | PAGE_GUARD, &old);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        LOG("[StubMonitor] OEP confirmed with valid prologue at %p", (void*)fault_addr);

        DWORD old;
        VirtualProtect(m_guard_page_base, m_guard_page_size, PAGE_EXECUTE_READ, &old);

        g_oep_addr = fault_addr;
        g_oep_confirmed = true;
        RemoveVectoredExceptionHandler(m_veh_handle);
        m_veh_handle = nullptr;

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void femboy::patch::StubMonitor::set_guard_page(uintptr_t oep)
{
    if (!oep) return;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_guard_page_base = (void*)(oep & ~((uintptr_t)si.dwPageSize - 1));
    m_guard_page_size = si.dwPageSize;

    DWORD old;
    if (VirtualProtect(m_guard_page_base, m_guard_page_size,
        PAGE_EXECUTE_READ | PAGE_GUARD, &old))
    {
        LOG("[StubMonitor] PAGE_GUARD set on page %p (OEP in range)", m_guard_page_base);
    }
    else
    {
        LOG("[StubMonitor] VirtualProtect failed for guard page (err=%d)", GetLastError());
    }
}

bool femboy::patch::StubMonitor::find_oep_in_stub(uintptr_t& oep)
{
    uintptr_t stub_start = femboy::stub::g_variant_info.stub_base;
    size_t stub_size = femboy::stub::g_variant_info.stub_size;
    if (!stub_start || !stub_size) return false;

    std::vector<uint8_t> buf(stub_size);
    if (!read_memory_safe(stub_start, buf.data(), stub_size))
        return false;

    for (size_t i = 0; i + 5 <= stub_size; i++)
    {
        if (buf[i] != 0xE9) continue;
        int32_t offset = *(int32_t*)&buf[i + 1];
        uintptr_t target = stub_start + i + 5 + offset;

        uintptr_t stub_end = stub_start + stub_size;
        if (target >= stub_start && target < stub_end) continue;
        if (!is_in_executable_code(target)) continue;

        oep = target;
        LOG("[StubMonitor] V2.x OEP via E9 at stub+0x%zx -> %p", i, (void*)target);
        return true;
    }
    return false;
}

int __stdcall femboy::patch::StubMonitor::hook_init_security_cookie()
{
    LOG("[StubMonitor] __security_init_cookie hooked (stub calls this before OEP jump)");

    uintptr_t oep = 0;
    if (find_oep_in_stub(oep))
    {
        set_guard_page(oep);
    }
    else
    {
        LOG("[StubMonitor] could not find OEP via stub scan");
    }

    if (Real_InitSecurityCookie)
        return Real_InitSecurityCookie();
    return 0;
}

bool femboy::patch::StubMonitor::Apply()
{
    if (!femboy::g_config.patches.enable_oep_monitor)
    {
        LOG("[StubMonitor] disabled");
        return false;
    }

    LOG("[StubMonitor] applying VEH guard-page stub monitoring...");

    m_veh_handle = AddVectoredExceptionHandler(1, vex_handler);
    LOG("[StubMonitor] VEH handler registered: %p", m_veh_handle);

    {
        auto* hk_sec = new femboy::hook::Detour();
        bool installed = false;
        HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        if (h_snap != INVALID_HANDLE_VALUE)
        {
            MODULEENTRY32W me;
            me.dwSize = sizeof(me);
            if (Module32FirstW(h_snap, &me))
            {
                do
                {
                    HMODULE h_mod = GetModuleHandleW(me.szModule);
                    if (!h_mod) continue;
                    void* p_sec = GetProcAddress(h_mod, "__security_init_cookie");
                    if (p_sec && hk_sec->Install(p_sec, hook_init_security_cookie))
                    {
                        m_hooks.push_back(hk_sec);
                        LOG("[StubMonitor] hooked __security_init_cookie in %ls", me.szModule);
                        installed = true;
                        break;
                    }
                } while (Module32NextW(h_snap, &me));
            }
            CloseHandle(h_snap);
        }
        if (!installed) delete hk_sec;
    }

    if (g_oep_addr != 0)
    {
        LOG("[StubMonitor] OEP known statically (%p), setting guard page", (void*)g_oep_addr);
        set_guard_page(g_oep_addr);
    }
    else
    {
        LOG("[StubMonitor] OEP not known statically -- will detect via __security_init_cookie hook");
    }

    m_active = true;
    return true;
}

void femboy::patch::StubMonitor::remove_all_hooks()
{
    if (m_veh_handle)
    {
        RemoveVectoredExceptionHandler(m_veh_handle);
        m_veh_handle = nullptr;
    }

    if (m_guard_page_base)
    {
        DWORD old;
        VirtualProtect(m_guard_page_base, m_guard_page_size, PAGE_EXECUTE_READ, &old);
        m_guard_page_base = nullptr;
        m_guard_page_size = 0;
    }

    if (!m_active) return;
    for (auto* hook : m_hooks)
    {
        hook->Remove();
        delete hook;
    }
    m_hooks.clear();
    m_active = false;
}
