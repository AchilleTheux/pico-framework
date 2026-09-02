/* Host-side tests for MCP2515 SIDH/SIDL/EID8/EID0/DLC packing. */

#include "test.h"

#include "mcp2515_frame.h"

static can_message_t message(uint32_t id, bool extended, bool remote, uint8_t length)
{
    can_message_t result = {
        .id = id,
        .length = length,
        .extended = extended,
        .remote = remote,
    };
    for (uint8_t i = 0; i < CAN_MAX_DATA_LENGTH; i++) {
        result.data[i] = (uint8_t)(0x10u + i);
    }
    return result;
}

static void check_round_trip(uint32_t id, bool extended, bool remote, uint8_t length)
{
    const can_message_t original = message(id, extended, remote, length);
    uint8_t header[MCP2515_FRAME_HEADER_SIZE];
    mcp2515_frame_pack_header(&original, header);

    can_message_t decoded;
    mcp2515_frame_unpack_header(header, &decoded);

    CHECK_EQ_U32(decoded.id, id);
    CHECK_EQ_INT(decoded.extended, extended);
    CHECK_EQ_INT(decoded.remote, remote);
    CHECK_EQ_INT(decoded.length, length);
}

TEST(a_standard_data_frame_round_trips)
{
    check_round_trip(0x123, false, false, 8);
}

TEST(a_standard_id_of_zero_round_trips)
{
    check_round_trip(0, false, false, 0);
}

TEST(the_maximum_standard_id_round_trips)
{
    check_round_trip(CAN_STANDARD_ID_MAX, false, false, 4);
}

TEST(an_extended_data_frame_round_trips)
{
    check_round_trip(0x1ABCDEF, true, false, 8);
}

TEST(the_maximum_extended_id_round_trips)
{
    check_round_trip(CAN_EXTENDED_ID_MAX, true, false, 8);
}

TEST(a_standard_remote_frame_round_trips_with_no_payload_bytes_needed)
{
    check_round_trip(0x555, false, true, 8);
}

TEST(an_extended_remote_frame_round_trips)
{
    check_round_trip(0x1F00000, true, true, 3);
}

TEST(the_extended_flag_lives_in_a_specific_sidl_bit)
{
    /* SIDL bit 3 (0x08) is the sole carrier of standard-vs-extended; this
       pins the bit position so a future refactor cannot silently move it. */
    const can_message_t standard = message(0x123, false, false, 0);
    const can_message_t extended = message(0x123, true, false, 0);

    uint8_t std_header[MCP2515_FRAME_HEADER_SIZE];
    uint8_t ext_header[MCP2515_FRAME_HEADER_SIZE];
    mcp2515_frame_pack_header(&standard, std_header);
    mcp2515_frame_pack_header(&extended, ext_header);

    CHECK_EQ_U32(std_header[1] & 0x08u, 0);
    CHECK_EQ_U32(ext_header[1] & 0x08u, 0x08u);
}

TEST(the_remote_flag_lives_in_a_specific_dlc_bit)
{
    /* DLC bit 6 (0x40) is the sole carrier of the remote-request flag. */
    const can_message_t data = message(0x100, false, false, 5);
    const can_message_t remote = message(0x100, false, true, 5);

    uint8_t data_header[MCP2515_FRAME_HEADER_SIZE];
    uint8_t remote_header[MCP2515_FRAME_HEADER_SIZE];
    mcp2515_frame_pack_header(&data, data_header);
    mcp2515_frame_pack_header(&remote, remote_header);

    CHECK_EQ_U32(data_header[4] & 0x40u, 0);
    CHECK_EQ_U32(remote_header[4] & 0x40u, 0x40u);
    CHECK_EQ_U32(data_header[4] & 0x0Fu, 5u);
    CHECK_EQ_U32(remote_header[4] & 0x0Fu, 5u);
}

TEST(packing_a_filter_id_matches_packing_a_message_header)
{
    /* mcp2515_frame_pack_id() is the id/flags half of pack_header(); this
       checks the two stay consistent so a filter selects the frames a
       message with the same identifier would actually produce. */
    const can_message_t as_message = message(0x1ABCDEF, true, false, 0);
    uint8_t header[MCP2515_FRAME_HEADER_SIZE];
    mcp2515_frame_pack_header(&as_message, header);

    const uint32_t packed = can_id_pack(0x1ABCDEF, true, false);
    uint8_t filter_bytes[4];
    mcp2515_frame_pack_id(packed, filter_bytes);

    CHECK(memcmp(header, filter_bytes, sizeof(filter_bytes)) == 0);
}

TEST_MAIN(
    RUN(a_standard_data_frame_round_trips);
    RUN(a_standard_id_of_zero_round_trips);
    RUN(the_maximum_standard_id_round_trips);
    RUN(an_extended_data_frame_round_trips);
    RUN(the_maximum_extended_id_round_trips);
    RUN(a_standard_remote_frame_round_trips_with_no_payload_bytes_needed);
    RUN(an_extended_remote_frame_round_trips);
    RUN(the_extended_flag_lives_in_a_specific_sidl_bit);
    RUN(the_remote_flag_lives_in_a_specific_dlc_bit);
    RUN(packing_a_filter_id_matches_packing_a_message_header);
)
