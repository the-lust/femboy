#pragma once
#include "base.hpp"

namespace femboy::detect {

class CegDetection : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;
};

}
