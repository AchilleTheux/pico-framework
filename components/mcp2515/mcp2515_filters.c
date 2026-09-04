#include "mcp2515_filters.h"

/*
 * The first filter sharing a bank with this one, and therefore the one whose
 * `.mask` the whole bank has to use. The split is 2/4, not 3/3: RXB0 owns
 * RXF0 and RXF1, RXB1 owns RXF2 through RXF5.
 */
static size_t bank_leader(size_t filter_index)
{
    return filter_index < MCP2515_BANK0_SLOTS ? 0u : MCP2515_BANK0_SLOTS;
}

bool mcp2515_filters_are_valid(const can_filter_t *filters, size_t count)
{
    if (count == 0) {
        return true;
    }
    if (filters == NULL || count > MCP2515_FILTER_SLOTS) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if ((filters[i].mask & CAN_FLAG_EXTENDED) == 0 ||
            (filters[i].mask & CAN_FLAG_RTR) != 0) {
            return false; /* the controller cannot mask off frame type or RTR */
        }
    }

    /* One mask register per receive buffer, so every filter landing in the
       same buffer has to agree on it — and on frame type, since the single
       mask is written in the layout of the frame type it applies to and the
       two layouts put a mask's bits in different registers. */
    for (size_t i = 1; i < count; i++) {
        const size_t leader = bank_leader(i);
        if (filters[i].mask != filters[leader].mask ||
            can_id_is_extended(filters[i].id) != can_id_is_extended(filters[leader].id)) {
            return false;
        }
    }

    return true;
}

bool mcp2515_filter_plan(const can_filter_t *filters, size_t count,
                         mcp2515_filter_plan_t *plan)
{
    if (plan == NULL || !mcp2515_filters_are_valid(filters, count)) {
        return false;
    }

    if (count == 0) {
        plan->accept_all = true;
        for (size_t i = 0; i < MCP2515_FILTER_SLOTS; i++) {
            plan->filter_id[i] = 0;
        }
        plan->mask[0] = 0;
        plan->mask[1] = 0;
        plan->mask_extended[0] = false;
        plan->mask_extended[1] = false;
        return true;
    }

    plan->accept_all = false;

    /* RXB0: the first one or two filters, the second slot repeating the first
       when only one was given. */
    const size_t bank0_count = (count < MCP2515_BANK0_SLOTS) ? count : MCP2515_BANK0_SLOTS;
    for (size_t i = 0; i < MCP2515_BANK0_SLOTS; i++) {
        plan->filter_id[i] = filters[i % bank0_count].id;
    }
    plan->mask[0] = filters[0].mask;
    plan->mask_extended[0] = can_id_is_extended(filters[0].id);

    /*
     * RXB1: whatever is left over. With two filters or fewer there is no
     * leftover, and an unconfigured RXB1 would accept every frame on the bus —
     * so it repeats RXB0's filters under RXB0's mask instead. Both buffers
     * then accept exactly the same frames, and the second one is depth rather
     * than a hole.
     */
    const size_t bank1_first = MCP2515_BANK0_SLOTS;
    const size_t bank1_count = (count > bank1_first) ? (count - bank1_first) : bank0_count;
    const size_t bank1_source = (count > bank1_first) ? bank1_first : 0u;

    for (size_t i = 0; i < MCP2515_BANK1_SLOTS; i++) {
        plan->filter_id[MCP2515_BANK0_SLOTS + i] = filters[bank1_source + (i % bank1_count)].id;
    }
    plan->mask[1] = filters[bank1_source].mask;
    plan->mask_extended[1] = can_id_is_extended(filters[bank1_source].id);

    return true;
}
