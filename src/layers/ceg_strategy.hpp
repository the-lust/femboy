#pragma once
#include "base.hpp"
#include <cstdint>

namespace femboy::patch {

class CegStrategy : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;

    static bool generate_ticket_data(
        uint8_t* ticket, uint32_t cb_max_ticket,
        uint32_t* pcb_ticket, bool* pb_owned);
};

}
