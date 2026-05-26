#pragma once
#include "base.hpp"

namespace femboy::patch {

class PluginLoader : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;
};

}
