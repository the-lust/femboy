#include "variant_detector.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"

const char* femboy::stub::VariantDetector::Name()
{
    return "variant_detector";
}

bool femboy::stub::VariantDetector::Apply()
{
    if (!femboy::g_config.patches.enable_variant_detection)
    {
        LOG("[VariantDetector] disabled");
        return false;
    }

    LOG("[VariantDetector] scanning for SteamStub variant...");

    if (femboy::g_config.patches.entry_point_rva != 0)
    {
        LOG("[VariantDetector] using manual EntryPointRVA from config: 0x%X",
            femboy::g_config.patches.entry_point_rva);
        g_oep_addr = (uintptr_t)GetModuleHandleW(nullptr) + femboy::g_config.patches.entry_point_rva;
        return true;
    }

    if (!detect_stub(m_info))
    {
        LOG("[VariantDetector] no SteamStub detected");
        return false;
    }

    g_variant_info = m_info;

    LOG("[VariantDetector] variant=%d, stubBase=%p, stubSize=%zu, hasBind=%s",
        (int)m_info.variant, (void*)m_info.stub_base, m_info.stub_size,
        m_info.has_bind_section ? "yes" : "no");

    uintptr_t oep = 0;
    bool oep_found = false;

    switch (m_info.variant)
    {
    case Variant::V1_x86:
        oep_found = extract_oep_v1(m_info, oep);
        break;

    case Variant::V3_0_x86:
    case Variant::V3_1_x86:
    case Variant::V3_1_x64:
        oep_found = extract_oep_v3(m_info, oep);
        break;

    case Variant::V2_0_x86:
    case Variant::V2_1_x86:
        LOG("[VariantDetector] V2.x -- OEP cannot be statically extracted, will use StubMonitor");
        break;

    default:
        LOG("[VariantDetector] unknown variant, will use StubMonitor");
        break;
    }

    if (oep_found && oep > 0)
    {
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        g_oep_addr = base + oep;
        LOG("[VariantDetector] OEP = 0x%p (RVA 0x%X)", (void*)g_oep_addr, oep);
    }

    return true;
}
