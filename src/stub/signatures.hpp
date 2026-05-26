#pragma once
#include <cstdint>
#include <cstddef>
#include "stub.hpp"

namespace femboy::stub
{

struct VariantSig
{
    Variant variant;
    const uint8_t* pattern;
    const char* mask;
    size_t length;
};

// V1 x86: 60 81 EC 00 10 00 00 BE ?? ?? ?? ?? B9 6A
static const uint8_t sig_v1_x86[] = {
    0x60, 0x81, 0xEC, 0x00, 0x10, 0x00, 0x00, 0xBE, 0x00, 0x00, 0x00, 0x00, 0xB9, 0x6A
};
static const char mask_v1_x86[] = "xxxxxxx????xx";

// V2.0 x86
static const uint8_t sig_v20_x86[] = {
    0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x00, 0x10, 0x00, 0x00, 0xBE
};
static const char mask_v20_x86[] = "xxxxxxxxxxxxxxx";

// V2.1 x86
static const uint8_t sig_v21_x86[] = {
    0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x00, 0x10, 0x00, 0x00, 0xC7
};
static const char mask_v21_x86[] = "xxxxxxxxxxxxxxx";

// V3.0 x86 / V3.1 x86
static const uint8_t sig_v3_x86[] = {
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55
};
static const char mask_v3_x86[] = "x....xxxxxxxxx";

// V3.1 x64
static const uint8_t sig_v31_x64[] = {
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x41, 0x50
};
static const char mask_v31_x64[] = "x....xxxxxxxxxxx";

static const VariantSig g_signatures[] = {
    { Variant::V1_x86,   sig_v1_x86,  mask_v1_x86,  sizeof(sig_v1_x86) },
    { Variant::V2_0_x86, sig_v20_x86, mask_v20_x86, sizeof(sig_v20_x86) },
    { Variant::V2_1_x86, sig_v21_x86, mask_v21_x86, sizeof(sig_v21_x86) },
    { Variant::V3_0_x86, sig_v3_x86,  mask_v3_x86,  sizeof(sig_v3_x86) },
    { Variant::V3_1_x86, sig_v3_x86,  mask_v3_x86,  sizeof(sig_v3_x86) },
    { Variant::V3_1_x64, sig_v31_x64, mask_v31_x64, sizeof(sig_v31_x64) },
};

constexpr uint32_t V3_HEADER_SIG_V0 = 0xC0DEC0DE;
constexpr uint32_t V3_HEADER_SIG_V1 = 0xC0DEC0DF;

}
