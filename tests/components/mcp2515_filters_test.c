/*
 * Host-side tests for the MCP2515 acceptance-filter bank assignment.
 *
 * The failure this covers is silent by construction: a receive buffer with no
 * filter configured stays in accept-all mode, so a filter set that only fills
 * one bank looks installed and still delivers the whole bus through the other
 * buffer. Nothing on the wire, and no status register, says so.
 *
 * The hardware layout being asserted (datasheet section 4.5):
 *
 *     RXB0   RXF0, RXF1                 masked by RXM0
 *     RXB1   RXF2, RXF3, RXF4, RXF5     masked by RXM1
 */

#include <string.h>

#include "test.h"

#include "can_frame.h"
#include "mcp2515_filters.h"

/* A mask that selects an exact standard id, with the frame-type bit compared
   as the controller insists. */
#define EXACT_STD (CAN_STANDARD_ID_MAX | CAN_FLAG_EXTENDED)

static can_filter_t std_filter(uint32_t id, uint32_t mask)
{
    can_filter_t filter = {
        .id = can_id_pack(id, false, false),
        .mask = mask,
    };
    return filter;
}

/* True when every slot of `plan` matches one of `count` accepted ids. */
static bool plan_only_accepts(const mcp2515_filter_plan_t *plan,
                              const uint32_t *ids, size_t count)
{
    for (size_t slot = 0; slot < MCP2515_FILTER_SLOTS; slot++) {
        bool found = false;
        for (size_t i = 0; i < count; i++) {
            if (plan->filter_id[slot] == ids[i]) {
                found = true;
                break;
            }
        }
        if (!found) {
            printf("    slot %zu holds 0x%08X, which is not one of the accepted ids\n",
                   slot, (unsigned)plan->filter_id[slot]);
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Both buffers, always
 * -------------------------------------------------------------------------*/

TEST(no_filters_accepts_everything)
{
    mcp2515_filter_plan_t plan;
    memset(&plan, 0xAA, sizeof(plan));

    CHECK(mcp2515_filter_plan(NULL, 0, &plan));
    CHECK(plan.accept_all);
}

TEST(a_single_filter_still_fills_the_second_bank)
{
    /*
     * The bug this file exists for. One filter is enough to configure RXB0,
     * and leaving RXB1 alone would leave it accepting the entire bus — so a
     * caller who asked for one identifier would receive all of them.
     */
    const can_filter_t filters[] = { std_filter(0x123, EXACT_STD) };

    mcp2515_filter_plan_t plan;
    CHECK(mcp2515_filter_plan(filters, 1, &plan));
    CHECK(!plan.accept_all);

    const uint32_t accepted[] = { filters[0].id };
    CHECK(plan_only_accepts(&plan, accepted, count_of_(accepted)));

    /* And both masks are the caller's, not a wide-open one. */
    CHECK_EQ_U32(plan.mask[0], EXACT_STD);
    CHECK_EQ_U32(plan.mask[1], EXACT_STD);
}

TEST(two_filters_fill_both_banks)
{
    const can_filter_t filters[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, EXACT_STD),
    };

    mcp2515_filter_plan_t plan;
    CHECK(mcp2515_filter_plan(filters, 2, &plan));

    /* RXB0 gets exactly the two it was given. */
    CHECK_EQ_U32(plan.filter_id[0], filters[0].id);
    CHECK_EQ_U32(plan.filter_id[1], filters[1].id);

    /* RXB1's four slots hold nothing else. */
    const uint32_t accepted[] = { filters[0].id, filters[1].id };
    CHECK(plan_only_accepts(&plan, accepted, count_of_(accepted)));
    CHECK_EQ_U32(plan.mask[1], EXACT_STD);
}

TEST(three_filters_split_two_and_one_not_three_and_zero)
{
    /*
     * The other half of the old mistake: filters [0..2] were treated as one
     * bank, so the third went to RXF2 under RXM0 — a register that RXB1
     * actually masks with RXM1.
     */
    const can_filter_t filters[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, EXACT_STD),
        std_filter(0x300, EXACT_STD),
    };

    mcp2515_filter_plan_t plan;
    CHECK(mcp2515_filter_plan(filters, 3, &plan));

    CHECK_EQ_U32(plan.filter_id[0], filters[0].id);
    CHECK_EQ_U32(plan.filter_id[1], filters[1].id);

    /* The third is RXB1's, so every one of its four slots is that filter. */
    for (size_t slot = MCP2515_BANK0_SLOTS; slot < MCP2515_FILTER_SLOTS; slot++) {
        CHECK_EQ_U32(plan.filter_id[slot], filters[2].id);
    }
}

TEST(six_filters_land_one_per_slot)
{
    can_filter_t filters[MCP2515_FILTER_SLOTS];
    for (size_t i = 0; i < count_of_(filters); i++) {
        filters[i] = std_filter((uint32_t)(0x100 + i), EXACT_STD);
    }

    mcp2515_filter_plan_t plan;
    CHECK(mcp2515_filter_plan(filters, count_of_(filters), &plan));

    for (size_t i = 0; i < count_of_(filters); i++) {
        CHECK_EQ_U32(plan.filter_id[i], filters[i].id);
    }
    CHECK_EQ_U32(plan.mask[0], EXACT_STD);
    CHECK_EQ_U32(plan.mask[1], EXACT_STD);
}

TEST(the_two_banks_may_use_different_masks)
{
    /* The point of having two mask registers at all. */
    const uint32_t wide = 0x700u | CAN_FLAG_EXTENDED;
    const can_filter_t filters[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, EXACT_STD),
        std_filter(0x300, wide),
        std_filter(0x400, wide),
    };

    mcp2515_filter_plan_t plan;
    CHECK(mcp2515_filter_plan(filters, count_of_(filters), &plan));
    CHECK_EQ_U32(plan.mask[0], EXACT_STD);
    CHECK_EQ_U32(plan.mask[1], wide);
}

/* ---------------------------------------------------------------------------
 * What the hardware cannot express
 * -------------------------------------------------------------------------*/

TEST(filters_sharing_a_bank_must_share_a_mask)
{
    const can_filter_t bank0_disagrees[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, 0x700u | CAN_FLAG_EXTENDED),
    };
    CHECK(!mcp2515_filters_are_valid(bank0_disagrees, count_of_(bank0_disagrees)));

    const can_filter_t bank1_disagrees[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, EXACT_STD),
        std_filter(0x300, EXACT_STD),
        std_filter(0x400, EXACT_STD),
        std_filter(0x500, 0x700u | CAN_FLAG_EXTENDED),
    };
    CHECK(!mcp2515_filters_are_valid(bank1_disagrees, count_of_(bank1_disagrees)));
}

TEST(the_boundary_between_the_banks_is_after_the_second_filter)
{
    /*
     * Pins the split itself. Under the old 3/3 assumption the first list here
     * was rejected and the second accepted; both are the wrong way round for
     * the real hardware.
     */
    const uint32_t wide = 0x700u | CAN_FLAG_EXTENDED;

    /* [0..1] agree, [2..] agree separately: legal. */
    const can_filter_t split_after_two[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, EXACT_STD),
        std_filter(0x300, wide),
    };
    CHECK(mcp2515_filters_are_valid(split_after_two, count_of_(split_after_two)));

    /* [0..2] agree but the change falls inside RXB1's span: illegal. */
    const can_filter_t split_after_three[] = {
        std_filter(0x100, EXACT_STD),
        std_filter(0x200, EXACT_STD),
        std_filter(0x300, EXACT_STD),
        std_filter(0x400, wide),
    };
    CHECK(!mcp2515_filters_are_valid(split_after_three, count_of_(split_after_three)));
}

TEST(a_mask_must_compare_the_frame_type)
{
    /* Without CAN_FLAG_EXTENDED the caller is asking for something the
       controller cannot do: it always matches that bit exactly. */
    const can_filter_t no_type[] = { std_filter(0x100, CAN_STANDARD_ID_MAX) };
    CHECK(!mcp2515_filters_are_valid(no_type, 1));
}

TEST(a_mask_may_not_ask_for_a_remote_frame_filter)
{
    const can_filter_t rtr[] = { std_filter(0x100, EXACT_STD | CAN_FLAG_RTR) };
    CHECK(!mcp2515_filters_are_valid(rtr, 1));
}

TEST(more_filters_than_slots_is_rejected)
{
    can_filter_t filters[MCP2515_FILTER_SLOTS + 1];
    for (size_t i = 0; i < count_of_(filters); i++) {
        filters[i] = std_filter((uint32_t)(0x100 + i), EXACT_STD);
    }
    CHECK(!mcp2515_filters_are_valid(filters, count_of_(filters)));

    mcp2515_filter_plan_t plan;
    CHECK(!mcp2515_filter_plan(filters, count_of_(filters), &plan));
}

TEST(a_count_with_no_list_is_rejected)
{
    mcp2515_filter_plan_t plan;
    CHECK(!mcp2515_filters_are_valid(NULL, 1));
    CHECK(!mcp2515_filter_plan(NULL, 1, &plan));

    /* But no list and no count is the accept-everything default. */
    CHECK(mcp2515_filters_are_valid(NULL, 0));
}

TEST(a_rejected_list_leaves_the_plan_alone)
{
    mcp2515_filter_plan_t plan;
    memset(&plan, 0x5A, sizeof(plan));
    mcp2515_filter_plan_t before = plan;

    const can_filter_t no_type[] = { std_filter(0x100, CAN_STANDARD_ID_MAX) };
    CHECK(!mcp2515_filter_plan(no_type, 1, &plan));
    CHECK(memcmp(&plan, &before, sizeof(plan)) == 0);
}

TEST_MAIN(
    RUN(no_filters_accepts_everything);
    RUN(a_single_filter_still_fills_the_second_bank);
    RUN(two_filters_fill_both_banks);
    RUN(three_filters_split_two_and_one_not_three_and_zero);
    RUN(six_filters_land_one_per_slot);
    RUN(the_two_banks_may_use_different_masks);

    RUN(filters_sharing_a_bank_must_share_a_mask);
    RUN(the_boundary_between_the_banks_is_after_the_second_filter);
    RUN(a_mask_must_compare_the_frame_type);
    RUN(a_mask_may_not_ask_for_a_remote_frame_filter);
    RUN(more_filters_than_slots_is_rejected);
    RUN(a_count_with_no_list_is_rejected);
    RUN(a_rejected_list_leaves_the_plan_alone);
)
