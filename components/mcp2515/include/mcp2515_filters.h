/*
 * mcp2515_filters - assigning a caller's acceptance filters to the
 * controller's two receive buffers.
 *
 * SDK-independent, and separate from mcp2515.c, because the mapping is the
 * part that fails *silently*: a filter written to the wrong RXFn register, or
 * a receive buffer left in accept-all mode because the caller happened to
 * supply fewer filters than there are slots, produces a bus that works — it
 * just also delivers frames that were meant to be rejected. Nothing reports
 * that. It is arithmetic, so it is host-tested instead of trusted.
 *
 * The hardware, which is not symmetric (datasheet section 4.5):
 *
 *     RXB0   RXF0, RXF1                 masked by RXM0
 *     RXB1   RXF2, RXF3, RXF4, RXF5     masked by RXM1
 *
 * A buffer accepts a frame when *any* of its filters matches under its bank's
 * mask. Both buffers are always live — there is no way to disable one — so a
 * filter set that leaves a bank unconfigured does not narrow anything: that
 * bank keeps taking everything on the wire.
 *
 * This module therefore always produces a fully populated plan. Unused slots
 * repeat a filter that is already in the plan rather than being left open,
 * which is exactly redundant and never permissive.
 */

#ifndef PICO_FRAMEWORK_MCP2515_FILTERS_H
#define PICO_FRAMEWORK_MCP2515_FILTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RXF0..RXF5. */
#define MCP2515_FILTER_SLOTS 6u

/* RXM0 and RXM1. */
#define MCP2515_MASK_SLOTS 2u

/* How many of the six filters each buffer owns. Not three and three. */
#define MCP2515_BANK0_SLOTS 2u
#define MCP2515_BANK1_SLOTS 4u

/*
 * The register values mcp2515.c writes, in slot order: filter_id[n] goes to
 * RXFn and mask[b] to RXMb. Ids and masks are packed as can_id_pack()
 * produces them, ready for mcp2515_frame_pack_id().
 *
 * `accept_all` means the caller supplied no filters: both buffers go to
 * RXM=11 and neither id nor mask is used.
 */
typedef struct {
    bool accept_all;
    uint32_t filter_id[MCP2515_FILTER_SLOTS];
    uint32_t mask[MCP2515_MASK_SLOTS];
} mcp2515_filter_plan_t;

/*
 * Whether `count` filters can be expressed by this controller at all.
 *
 * Three separate hardware limits, all of which would otherwise be discovered
 * as frames quietly getting through:
 *
 *   - at most MCP2515_FILTER_SLOTS filters;
 *   - filters [0..1] must share one `.mask`, and filters [2..5] a second,
 *     because a mask is per receive buffer and not per filter;
 *   - every mask must include CAN_FLAG_EXTENDED (the controller always
 *     compares a filter's standard/extended flag exactly, it cannot mask that
 *     bit off) and must not include CAN_FLAG_RTR (there is no hardware
 *     remote-request filter).
 *
 * `filters` may be NULL when `count` is 0.
 */
bool mcp2515_filters_are_valid(const can_filter_t *filters, size_t count);

/*
 * Fill `plan` from a valid filter list. Returns false, leaving `plan`
 * untouched, for anything mcp2515_filters_are_valid() rejects.
 *
 * With one or two filters there is nothing to put in RXB1's four slots, and
 * leaving them open would make RXB1 accept the whole bus. The plan repeats the
 * caller's filters there under the same mask instead, so both buffers accept
 * exactly the same frames and the pair is used only for depth.
 */
bool mcp2515_filter_plan(const can_filter_t *filters, size_t count,
                         mcp2515_filter_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MCP2515_FILTERS_H */
