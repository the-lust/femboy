#include "memory.hpp"
#include "logger.hpp"
#include "signatures.hpp"
#include <vector>
#include <cstring>

namespace femboy::stub
{

VariantInfo g_variant_info = {};

// these sigs from steamless hit different no cap

static bool has_bind_section()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (memcmp(sec[i].Name, ".bind", 5) == 0) return true;
    }
    return false;
}

bool detect_stub(VariantInfo& info)
{
    memset(&info, 0, sizeof(info));
    info.has_bind_section = has_bind_section();

    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    uintptr_t ep = base + nt->OptionalHeader.AddressOfEntryPoint;

    const size_t scan_size = 0x1000;
    std::vector<uint8_t> ep_data(scan_size);
    if (!read_memory_safe(ep, ep_data.data(), scan_size))
    {
        LOG("detect_stub: cannot read EP code");
        return false;
    }

    for (auto& sig : g_signatures)
    {
        uintptr_t offset;
        if (mem_search_with_mask(ep_data.data(), scan_size, sig.pattern, sig.mask, offset))
        {
            info.variant = sig.variant;
            info.oep_rva = 0;
            info.stub_base = ep + offset;
            info.stub_size = scan_size - offset;

            LOG("detect_stub: detected variant %d at EP+%p",
                (int)sig.variant, (void*)offset);
            return true;
        }
    }

    LOG("detect_stub: no known variant found");
    info.variant = Variant::Unknown;
    return false;
}

bool extract_oep_v1(const VariantInfo& info, uintptr_t& oep)
{
    (void)info;
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (memcmp(sec[i].Name, ".bind", 5) == 0)
        {
            uintptr_t bind_base = base + sec[i].VirtualAddress;
            DWORD bind_size = sec[i].Misc.VirtualSize;
            std::vector<uint8_t> bind_data(bind_size);
            if (!read_memory_safe(bind_base, bind_data.data(), bind_size))
                return false;
            // deadass took me 3 tries to get this sig right
            // Pattern: 61 B8 ?? ?? ?? ?? FF E0
            const uint8_t pattern[] = { 0x61, 0xB8, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0 };
            const char mask[] = "xx....xx";
            uintptr_t pat_off;
            if (mem_search_with_mask(bind_data.data(), bind_size, pattern, mask, pat_off))
            {
                oep = *(uint32_t*)&bind_data[pat_off + 2];
                LOG("extract_oep_v1: OEP = 0x%p", (void*)oep);
                return true;
            }
            return false;
        }
    }
    return false;
}

bool steam_xor_decode(const uint8_t* encoded, size_t size, uint8_t* decoded)
{
    if (size < 8 || size % 4 != 0) return false;

    uint32_t key;
    memcpy(&key, encoded, 4);
    memcpy(decoded, encoded, 4);

    for (size_t i = 4; i < size; i += 4)
    {
        uint32_t chunk;
        memcpy(&chunk, encoded + i, 4);
        uint32_t d_chunk = chunk ^ key;
        memcpy(decoded + i, &d_chunk, 4);
        key = chunk;
    }
    return true;
}

bool try_decode_v3_header(uintptr_t hdr_addr, uintptr_t& oep)
{
    if (!hdr_addr) return false;

    uint32_t header_sizes[] = { 0xF0, 0xD0, 0xB0 };
    for (auto hdr_size : header_sizes)
    {
        std::vector<uint8_t> header(hdr_size + 4);
        if (!read_memory_safe(hdr_addr, header.data(), hdr_size + 4))
            continue;

        std::vector<uint8_t> decoded(hdr_size + 4);
        if (!steam_xor_decode(header.data(), hdr_size + 4, decoded.data()))
            continue;

        uint32_t sig;
        memcpy(&sig, decoded.data() + 4, 4);
        if (sig == V3_HEADER_SIG_V0 || sig == V3_HEADER_SIG_V1)
        {
            memcpy(&oep, decoded.data() + 0x1C, 4);
            LOG("try_decode_v3_header: sig=0x%08X OEP=0x%p at %p", sig, (void*)oep, (void*)hdr_addr);
            return true;
        }
    }
    return false;
}

bool extract_oep_v3(const VariantInfo& info, uintptr_t& oep)
{
    (void)info;
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    uint32_t ep_va = nt->OptionalHeader.AddressOfEntryPoint;

    // Strategy 1: Try header locations relative to EP
    uint32_t header_sizes[] = { 0xF0, 0xD0, 0xB0 };
    for (auto hdr_size : header_sizes)
    {
        if (ep_va < hdr_size) continue;
        if (try_decode_v3_header(base + ep_va - hdr_size, oep))
            return true;
    }

    // Strategy 2: Try header locations relative to TLS callback VA
    // Some games (e.g. Shadow of War x64) store the header relative to TLS callback, not
    // the entry point. This is because the stub's internal init jumps through TLS first.
    IMAGE_DATA_DIRECTORY* tls_dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tls_dir->Size > 0 && tls_dir->VirtualAddress != 0)
    {
        IMAGE_TLS_DIRECTORY* tls = (IMAGE_TLS_DIRECTORY*)(base + tls_dir->VirtualAddress);
        uintptr_t cb_va;
#ifdef _WIN64
        cb_va = (uintptr_t)tls->AddressOfCallBacks;
#else
        cb_va = base + tls->AddressOfCallBacks;
#endif
        uintptr_t* callbacks = (uintptr_t*)cb_va;
        if (callbacks)
        {
            for (size_t cb_idx = 0; ; cb_idx++)
            {
                uintptr_t cb_addr;
                if (!read_memory_safe(
                    (uintptr_t)&callbacks[cb_idx], (uint8_t*)&cb_addr, sizeof(cb_addr)))
                    break;

                if (cb_addr == 0 || cb_addr == (uintptr_t)-1) break;

                LOG("extract_oep_v3: TLS callback[%zu] VA = 0x%p", cb_idx, (void*)cb_addr);
                for (auto hdr_size : header_sizes)
                {
                    if (cb_addr >= base + hdr_size &&
                        try_decode_v3_header(cb_addr - hdr_size, oep))
                    {
                        LOG("extract_oep_v3: header found via TLS callback[%zu]", cb_idx);
                        return true;
                    }
                }
            }
        }
    }
    else
    {
        LOG("extract_oep_v3: no TLS directory present");
    }

    LOG("extract_oep_v3: could not find valid V3.x header");
    return false;
}

}
