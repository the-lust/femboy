#pragma once
#include "base.hpp"

namespace femboy::detect {

class GbeDetection : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;
};

}
