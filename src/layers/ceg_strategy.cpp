#include "ceg_strategy.hpp"
#include "framework.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "steam.hpp"

const char* femboy::patch::CegStrategy::Name()
{
    return "ceg_strategy";
}

bool femboy::patch::CegStrategy::Apply()
{
    if (!femboy::g_config.patches.enable_ceg_strategy || !g_is_ceg_protected)
    {
        LOG("[CegStrategy] disabled or not CEG");
        return false;
    }

    LOG("[CegStrategy] applying CEG bypass...");

    // TODO: idk figure this out later
    return true;
}

// ceg ticket gen go brr
bool femboy::patch::CegStrategy::generate_ticket_data(
    uint8_t* ticket, uint32_t cb_max_ticket,
    uint32_t* pcb_ticket, bool* pb_owned)
{
    struct {
        uint32_t magic;
        uint64_t steam_id;
        uint32_t app_id;
        uint32_t timestamp;
        uint32_t ownership_flags;
        uint32_t ticket_version;
        uint8_t  padding[256 - 28];
    } t = {};

    t.magic = 0x00010003;
    t.steam_id = femboy::g_config.steam_emu.steam_id;
    t.app_id = femboy::g_config.cold_client.app_id;
    if (t.app_id == 0)
    {
        std::ifstream f("steam_appid.txt");
        if (f) f >> t.app_id;
    }
    t.timestamp = (uint32_t)time(nullptr);
    t.ownership_flags = 1;
    t.ticket_version = 2;

    uint32_t ticket_size = sizeof(t);
    if (cb_max_ticket > 0 && cb_max_ticket < ticket_size)
        ticket_size = cb_max_ticket;

    if (ticket && ticket_size > 0)
        memcpy(ticket, &t, ticket_size);

    if (pcb_ticket) *pcb_ticket = ticket_size;
    if (pb_owned) *pb_owned = true;

    LOG("[CegStrategy] generated ticket: %u bytes, appID=%u, steamID=%llu",
        ticket_size, t.app_id, t.steam_id);
    return true;
}
