#include "ceg_detection.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "stub.hpp"
#include "memory.hpp"

const char* femboy::detect::CegDetection::Name()
{
    return "ceg_detection";
}

bool femboy::detect::CegDetection::Apply()
{
    if (!femboy::g_config.patches.enable_ceg_detection)
    {
        LOG("[CegDetection] disabled");
        return false;
    }

    LOG("[CegDetection] checking for CEG protection...");

    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    bool has_bind = false;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    bool code_raw_vs_virtual = false;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (memcmp(sec[i].Name, ".bind", 5) == 0) has_bind = true;
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            if (sec[i].SizeOfRawData < sec[i].Misc.VirtualSize)
                code_raw_vs_virtual = true;
        }
    }

    if (has_bind)
    {
        LOG("[CegDetection] .bind section found, not CEG");
        return false;
    }

    if (!code_raw_vs_virtual)
    {
        LOG("[CegDetection] code section raw >= virtual, not CEG");
        return false;
    }

    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)
        (base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    bool has_steam_imports = false;

    if (imp && (uintptr_t)imp > base)
    {
        for (; imp->Name; imp++)
        {
            const char* mod_name = (const char*)(base + imp->Name);
            if (strstr(mod_name, "steam"))
            {
                has_steam_imports = true;
                break;
            }
        }
    }

    if (!has_steam_imports)
    {
        LOG("[CegDetection] no Steam imports found");
        return false;
    }

    LOG("[CegDetection] CEG protection DETECTED!");
    g_is_ceg_protected = true;
    return true;
}
