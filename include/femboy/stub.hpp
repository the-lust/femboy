#pragma once
#include <cstdint>
#include <cstddef>

namespace femboy::stub
{

enum class Variant : uint8_t
{
    Unknown = 0,
    V1_x86,
    V2_0_x86,
    V2_1_x86,
    V3_0_x86,
    V3_1_x86,
    V3_1_x64
};

struct VariantInfo
{
    Variant variant;
    uintptr_t oep_rva;
    uintptr_t stub_base;
    size_t stub_size;
    bool has_bind_section;
};

extern VariantInfo g_variant_info;

bool detect_stub(VariantInfo& info);
bool extract_oep_v1(const VariantInfo& info, uintptr_t& oep);
bool extract_oep_v3(const VariantInfo& info, uintptr_t& oep);
bool steam_xor_decode(const uint8_t* encoded, size_t size, uint8_t* decoded);
bool try_decode_v3_header(uintptr_t hdr_addr, uintptr_t& oep);

}
