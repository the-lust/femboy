#pragma once

#include "framework.hpp"

NAMESPACE_BEGIN(femboy)

class LayerBase
{
public:
    virtual ~LayerBase() = default;
    virtual const char* Name() = 0;
    virtual bool Apply() = 0;
};

extern bool g_gbe_detected;
extern bool g_is_ceg_protected;
extern uintptr_t g_oep_addr;
extern std::atomic<bool> g_oep_confirmed;
extern HMODULE g_femboy_hmodule;
extern bool g_verbose_log;

NAMESPACE_END(femboy)
