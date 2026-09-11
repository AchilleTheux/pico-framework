#include <string.h>
#include "test.h"
#include "mcp2518fd_frame.h"

TEST(lengths_and_validation)
{
    const uint8_t expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (unsigned length = 0; length <= 255; length++) {
        bool legal = false;
        for (unsigned dlc = 0; dlc < 16; dlc++) {
            CHECK_EQ_U32(can_fd_dlc_to_length(dlc), expected[dlc]);
            if (length == expected[dlc]) legal = true;
        }
        uint8_t dlc = 0xff;
        CHECK(can_fd_length_to_dlc(length, &dlc) == legal);
        if (legal) CHECK_EQ_U32(can_fd_dlc_to_length(dlc), length);
        can_fd_message_t msg = {.id = 0x123, .fd = true, .length = length};
        CHECK(can_fd_message_is_valid(&msg) == legal);
    }
    CHECK_EQ_U32(can_fd_dlc_to_length(16), 0);
    CHECK(!can_fd_length_to_dlc(8, NULL));
    CHECK(!can_fd_message_is_valid(NULL));
    can_fd_message_t msg = {.fd = true, .remote = true};
    CHECK(!can_fd_message_is_valid(&msg));
    msg = (can_fd_message_t){.bitrate_switch = true};
    CHECK(!can_fd_message_is_valid(&msg));
    msg = (can_fd_message_t){.error_state_indicator = true};
    CHECK(!can_fd_message_is_valid(&msg));
    msg = (can_fd_message_t){.id = 0x800};
    CHECK(!can_fd_message_is_valid(&msg));
    msg.extended = true;
    CHECK(can_fd_message_is_valid(&msg));
    msg.id = 0x20000000;
    CHECK(!can_fd_message_is_valid(&msg));
}

TEST(wire_vectors)
{
    /* SID=0x123, EID=0x23456, not a contiguous 29-bit integer in RAM. */
    can_fd_message_t msg = {
        .id = (0x123u << 18) | 0x23456u, .extended = true,
        .fd = true, .bitrate_switch = true, .length = 64,
    };
    for (unsigned i = 0; i < 64; i++) msg.data[i] = i + 1;
    uint8_t object[72];
    CHECK(mcp2518fd_frame_pack(&msg, object));
    const uint8_t header[] = {0x23, 0xb1, 0xa2, 0x11, 0xdf, 0, 0, 0};
    CHECK(memcmp(object, header, sizeof(header)) == 0);
    CHECK(memcmp(object + 8, msg.data, 64) == 0);
    object[5] = 1; /* received ESI */
    object[7] = 0xf8; /* ignored filter/reserved bits */
    can_fd_message_t received;
    CHECK(mcp2518fd_frame_unpack(object, &received));
    CHECK_EQ_U32(received.id, msg.id);
    CHECK(received.error_state_indicator);
    CHECK(received.fd && received.extended && received.bitrate_switch);
    CHECK(memcmp(received.data, msg.data, 64) == 0);
    CHECK(mcp2518fd_frame_pack(&received, object));
    CHECK_EQ_U32(object[5], 0); /* ESI must not be forwarded on TX */

    msg = (can_fd_message_t){.id = 0x321, .length = 8, .remote = true};
    memset(msg.data, 0xff, sizeof(msg.data));
    CHECK(mcp2518fd_frame_pack(&msg, object));
    CHECK_EQ_U32(object[4], 0x28);
    for (unsigned i = 8; i < sizeof(object); i++) CHECK_EQ_U32(object[i], 0);
    object[4] = 0x2f; /* Classic DLC=15 still means eight bytes. */
    CHECK(mcp2518fd_frame_unpack(object, &received));
    CHECK_EQ_U32(received.length, 8);
    CHECK(received.remote);
    object[4] = 0xaf; /* FD + RTR is invalid */
    CHECK(!mcp2518fd_frame_unpack(object, &received));
    CHECK(!mcp2518fd_frame_unpack(NULL, &received));
    CHECK(!mcp2518fd_frame_pack(NULL, object));
}

TEST(all_frames_round_trip)
{
    const uint32_t ids[] = {0, 1, 0x7ff, 0x800, 0x1abcdef, 0x1fffffff};
    for (unsigned ext = 0; ext < 2; ext++) {
        for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
            if (!ext && ids[i] > CAN_STANDARD_ID_MAX) continue;
            for (unsigned dlc = 0; dlc < 16; dlc++) {
                can_fd_message_t msg = {.id = ids[i], .extended = ext, .fd = true,
                    .length = can_fd_dlc_to_length(dlc), .bitrate_switch = true};
                for (unsigned j = 0; j < msg.length; j++) msg.data[j] = j ^ 0x5a;
                uint8_t object[72];
                can_fd_message_t result;
                CHECK(mcp2518fd_frame_pack(&msg, object));
                CHECK(mcp2518fd_frame_unpack(object, &result));
                CHECK_EQ_U32(result.id, msg.id);
                CHECK_EQ_U32(result.length, msg.length);
                CHECK(result.extended == msg.extended);
                CHECK(memcmp(result.data, msg.data, 64) == 0);
            }
        }
    }
}

TEST(classic_conversion)
{
    can_message_t classic = {.id = 0x123, .length = 3, .data = {1, 2, 3}};
    can_fd_message_t fd;
    CHECK(can_fd_message_from_classic(&classic, &fd));
    CHECK(!fd.fd && !fd.bitrate_switch);
    can_message_t result;
    CHECK(can_fd_message_to_classic(&fd, &result));
    CHECK_EQ_U32(result.id, classic.id);
    CHECK(memcmp(result.data, classic.data, 8) == 0);
    fd.fd = true;
    CHECK(!can_fd_message_to_classic(&fd, &result));
    classic.length = 9;
    CHECK(!can_fd_message_from_classic(&classic, &fd));
    CHECK(!can_fd_message_from_classic(NULL, &fd));
    CHECK(!can_fd_message_to_classic(NULL, &result));
}

TEST(filters)
{
    uint32_t object, mask;
    can_filter_t filter = {.id = 0x123, .mask = CAN_FLAG_EXTENDED | 0x7ff};
    CHECK(mcp2518fd_filter_pack(&filter, &object, &mask));
    CHECK_EQ_U32(object, 0x123);
    CHECK_EQ_U32(mask, 0x400007ff);
    filter = (can_filter_t){.id = CAN_FLAG_EXTENDED | (0x123u << 18) | 0x23456u,
                           .mask = CAN_FLAG_EXTENDED | CAN_EXTENDED_ID_MAX};
    CHECK(mcp2518fd_filter_pack(&filter, &object, &mask));
    CHECK_EQ_U32(object, 0x51a2b123);
    CHECK_EQ_U32(mask, 0x5fffffff);
    filter.mask |= CAN_FLAG_RTR;
    CHECK(!mcp2518fd_filter_pack(&filter, &object, &mask));
    filter = (can_filter_t){.id = 0, .mask = 0x7ff};
    CHECK(!mcp2518fd_filter_pack(&filter, &object, &mask));
    filter.mask = CAN_FLAG_EXTENDED | 0x800;
    CHECK(!mcp2518fd_filter_pack(&filter, &object, &mask));
    CHECK(!mcp2518fd_filter_pack(NULL, &object, &mask));
}

TEST(crc_vectors)
{
    /* CRC-16/CMS check vector (same parameters as MCP2518FD SPI). */
    const uint8_t check[] = "123456789";
    CHECK_EQ_U32(mcp2518fd_crc16(0xffff, check, 9), 0xaee7);
    uint16_t partial = mcp2518fd_crc16(0xffff, check, 3);
    CHECK_EQ_U32(mcp2518fd_crc16(partial, check + 3, 6), 0xaee7);
    CHECK_EQ_U32(mcp2518fd_crc16(0xffff, NULL, 0), 0xffff);
}

TEST_MAIN(
    RUN(lengths_and_validation);
    RUN(wire_vectors);
    RUN(all_frames_round_trip);
    RUN(classic_conversion);
    RUN(filters);
    RUN(crc_vectors);
)
