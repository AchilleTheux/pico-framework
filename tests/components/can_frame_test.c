/* Host-side tests for CAN identifiers, filters, DLC handling, and RX queue. */

#include <string.h>

#include "test.h"

#include "can_frame.h"

static can_message_t message(uint32_t id, bool extended, uint8_t length)
{
    can_message_t result = {
        .id = id,
        .length = length,
        .extended = extended,
    };
    for (uint8_t i = 0; i < CAN_MAX_DATA_LENGTH; i++) {
        result.data[i] = (uint8_t)(0xA0u + i);
    }
    return result;
}

TEST(identifier_widths_are_validated)
{
    can_message_t standard_max = message(CAN_STANDARD_ID_MAX, false, 8);
    can_message_t standard_too_large = message(CAN_STANDARD_ID_MAX + 1u, false, 8);
    can_message_t extended_max = message(CAN_EXTENDED_ID_MAX, true, 8);
    can_message_t extended_too_large = message(CAN_EXTENDED_ID_MAX + 1u, true, 8);

    CHECK(can_message_is_valid(&standard_max));
    CHECK(!can_message_is_valid(&standard_too_large));
    CHECK(can_message_is_valid(&extended_max));
    CHECK(!can_message_is_valid(&extended_too_large));
    CHECK(!can_message_is_valid(NULL));
}

TEST(message_length_must_fit_classic_can)
{
    can_message_t eight = message(1, false, 8);
    can_message_t nine = message(1, false, 9);
    CHECK(can_message_is_valid(&eight));
    CHECK(!can_message_is_valid(&nine));
}

TEST(identifiers_pack_and_unpack_with_flags)
{
    const uint32_t standard = can_id_pack(0x456, false, true);
    CHECK_EQ_U32(can_id_value(standard), 0x456);
    CHECK(!can_id_is_extended(standard));
    CHECK(can_id_is_remote(standard));

    const uint32_t extended = can_id_pack(0x1234567, true, false);
    CHECK_EQ_U32(can_id_value(extended), 0x1234567);
    CHECK(can_id_is_extended(extended));
    CHECK(!can_id_is_remote(extended));
}

TEST(packing_masks_bits_outside_the_identifier_width)
{
    CHECK_EQ_U32(can_id_value(can_id_pack(0xFABC, false, false)), 0x2BC);
    CHECK_EQ_U32(can_id_value(can_id_pack(0xE1234567, true, false)), 0x01234567);
}

TEST(dlc_is_clamped_to_the_eight_bytes_classic_can_carries)
{
    for (uint32_t dlc = 0; dlc <= 8; dlc++) {
        CHECK_EQ_INT(can_dlc_to_length(dlc), dlc);
    }
    CHECK_EQ_INT(can_dlc_to_length(9), 8);
    CHECK_EQ_INT(can_dlc_to_length(15), 8);
    CHECK_EQ_INT(can_dlc_to_length(UINT32_MAX), 8);
}

TEST(an_empty_filter_set_accepts_everything)
{
    const can_filter_t unused = { .id = 0x123, .mask = CAN_STANDARD_ID_MAX };
    CHECK(can_filters_accept(NULL, 0, 0x777));
    CHECK(can_filters_accept(&unused, 0, 0x777));
    CHECK(can_filters_accept(NULL, 4, 0x777));
}

TEST(filters_match_only_masked_bits)
{
    const can_filter_t block = {
        .id = can_id_pack(0x120, false, false),
        .mask = 0x7F8u | CAN_FLAG_EXTENDED,
    };
    CHECK(can_filter_matches(&block, can_id_pack(0x120, false, false)));
    CHECK(can_filter_matches(&block, can_id_pack(0x127, false, true)));
    CHECK(!can_filter_matches(&block, can_id_pack(0x128, false, false)));
    CHECK(!can_filter_matches(&block, can_id_pack(0x120, true, false)));
    CHECK(!can_filter_matches(NULL, 0));
}

TEST(any_filter_may_accept_a_frame)
{
    const can_filter_t filters[] = {
        { .id = 0x100, .mask = 0x7FFu | CAN_FLAG_EXTENDED },
        { .id = 0x200, .mask = 0x7FFu | CAN_FLAG_EXTENDED },
    };
    CHECK(can_filters_accept(filters, count_of_(filters), 0x200));
    CHECK(!can_filters_accept(filters, count_of_(filters), 0x300));
}

TEST(flags_can_be_filtered_independently)
{
    const can_filter_t data_only = { .id = 0, .mask = CAN_FLAG_RTR };
    CHECK(can_filter_matches(&data_only, can_id_pack(0x42, false, false)));
    CHECK(!can_filter_matches(&data_only, can_id_pack(0x42, false, true)));
}

TEST(queue_init_requires_room_for_one_complete_record)
{
    can_queue_t queue;
    uint8_t too_small[CAN_QUEUE_RECORD_SIZE];
    uint8_t enough[CAN_QUEUE_STORAGE_SIZE(1)];

    CHECK(!can_queue_init(NULL, enough, sizeof(enough)));
    CHECK(!can_queue_init(&queue, NULL, sizeof(enough)));
    CHECK(!can_queue_init(&queue, too_small, sizeof(too_small)));
    CHECK(can_queue_init(&queue, enough, sizeof(enough)));
    CHECK_EQ_INT(can_queue_capacity(&queue), 1);
}

TEST(a_data_frame_round_trips_without_struct_padding)
{
    uint8_t storage[CAN_QUEUE_STORAGE_SIZE(2)];
    can_queue_t queue;
    can_queue_init(&queue, storage, sizeof(storage));

    can_message_t sent = message(0x1234567, true, 5);
    CHECK(can_queue_push(&queue, &sent));
    CHECK_EQ_INT(can_queue_count(&queue), 1);

    can_message_t received;
    memset(&received, 0xEE, sizeof(received));
    CHECK(can_queue_pop(&queue, &received));
    CHECK_EQ_U32(received.id, sent.id);
    CHECK_EQ_INT(received.length, sent.length);
    CHECK(received.extended);
    CHECK(!received.remote);
    CHECK(memcmp(received.data, sent.data, sent.length) == 0);
    CHECK_EQ_INT(received.data[5], 0); /* bytes outside the DLC are cleared */
    CHECK_EQ_INT(can_queue_count(&queue), 0);
}

TEST(remote_frames_preserve_dlc_but_have_no_payload)
{
    uint8_t storage[CAN_QUEUE_STORAGE_SIZE(1)];
    can_queue_t queue;
    can_queue_init(&queue, storage, sizeof(storage));

    can_message_t sent = message(0x321, false, 6);
    sent.remote = true;
    CHECK(can_queue_push(&queue, &sent));

    can_message_t received;
    CHECK(can_queue_pop(&queue, &received));
    CHECK(received.remote);
    CHECK_EQ_INT(received.length, 6);
    for (uint8_t i = 0; i < CAN_MAX_DATA_LENGTH; i++) {
        CHECK_EQ_INT(received.data[i], 0);
    }
}

TEST(the_queue_is_fifo_across_many_wraps)
{
    uint8_t storage[CAN_QUEUE_STORAGE_SIZE(3)];
    can_queue_t queue;
    can_queue_init(&queue, storage, sizeof(storage));

    for (uint32_t round = 0; round < 100; round++) {
        for (uint32_t i = 0; i < 3; i++) {
            can_message_t sent = message((round * 3u + i) & CAN_STANDARD_ID_MAX, false, 1);
            CHECK(can_queue_push(&queue, &sent));
        }
        for (uint32_t i = 0; i < 3; i++) {
            can_message_t received;
            CHECK(can_queue_pop(&queue, &received));
            CHECK_EQ_U32(received.id, (round * 3u + i) & CAN_STANDARD_ID_MAX);
        }
    }
}

TEST(a_full_queue_drops_a_whole_frame_and_counts_it)
{
    uint8_t storage[CAN_QUEUE_STORAGE_SIZE(1)];
    can_queue_t queue;
    can_queue_init(&queue, storage, sizeof(storage));
    can_message_t first = message(1, false, 1);
    can_message_t second = message(2, false, 1);

    CHECK(can_queue_push(&queue, &first));
    CHECK(!can_queue_push(&queue, &second));
    CHECK_EQ_U32(queue.dropped, 1);
    CHECK_EQ_INT(can_queue_count(&queue), 1);

    can_message_t received;
    CHECK(can_queue_pop(&queue, &received));
    CHECK_EQ_U32(received.id, 1);
}

TEST(invalid_frames_are_rejected_without_being_counted_as_congestion)
{
    uint8_t storage[CAN_QUEUE_STORAGE_SIZE(1)];
    can_queue_t queue;
    can_queue_init(&queue, storage, sizeof(storage));
    can_message_t invalid = message(0x800, false, 1);

    CHECK(!can_queue_push(&queue, &invalid));
    CHECK_EQ_U32(queue.dropped, 0);
    CHECK_EQ_INT(can_queue_count(&queue), 0);
}

TEST(pop_empty_and_clear_are_safe)
{
    uint8_t storage[CAN_QUEUE_STORAGE_SIZE(2)];
    can_queue_t queue;
    can_queue_init(&queue, storage, sizeof(storage));
    can_message_t received;
    CHECK(!can_queue_pop(&queue, &received));

    can_message_t sent = message(1, false, 0);
    can_queue_push(&queue, &sent);
    can_queue_clear(&queue);
    CHECK_EQ_INT(can_queue_count(&queue), 0);
    CHECK(!can_queue_pop(&queue, &received));
}

TEST_MAIN(
    RUN(identifier_widths_are_validated);
    RUN(message_length_must_fit_classic_can);
    RUN(identifiers_pack_and_unpack_with_flags);
    RUN(packing_masks_bits_outside_the_identifier_width);
    RUN(dlc_is_clamped_to_the_eight_bytes_classic_can_carries);
    RUN(an_empty_filter_set_accepts_everything);
    RUN(filters_match_only_masked_bits);
    RUN(any_filter_may_accept_a_frame);
    RUN(flags_can_be_filtered_independently);
    RUN(queue_init_requires_room_for_one_complete_record);
    RUN(a_data_frame_round_trips_without_struct_padding);
    RUN(remote_frames_preserve_dlc_but_have_no_payload);
    RUN(the_queue_is_fifo_across_many_wraps);
    RUN(a_full_queue_drops_a_whole_frame_and_counts_it);
    RUN(invalid_frames_are_rejected_without_being_counted_as_congestion);
    RUN(pop_empty_and_clear_are_safe);
)
