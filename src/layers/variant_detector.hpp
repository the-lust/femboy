#pragma once
#include "base.hpp"
#include "stub.hpp"

namespace femboy::stub {

class VariantDetector : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;

    VariantInfo get_info() const { return m_info; }

private:
    VariantInfo m_info;
};

}
