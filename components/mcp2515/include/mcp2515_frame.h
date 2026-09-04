/*
 * mcp2515_frame - packing a can_message_t identifier and length into the
 * SIDH/SIDL/EID8/EID0/DLC byte layout the MCP2515 uses in every TX and RX
 * buffer, and back.
 *
 * SDK-independent so the bit-level layout — the part that fails quietly,
 * as a frame with the wrong identifier rather than an error — is
 * host-tested rather than trusted from an SPI capture the first time it
 * runs on hardware.
 */

#ifndef PICO_FRAMEWORK_MCP2515_FRAME_H
#define PICO_FRAMEWORK_MCP2515_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SIDH, SIDL, EID8, EID0, DLC — every TX/RX buffer starts with these five
   bytes, followed by up to CAN_MAX_DATA_LENGTH data bytes. */
#define MCP2515_FRAME_HEADER_SIZE 5u

/*
 * Pack `message`'s identifier, extended/remote flags, and length into the
 * five-byte header a TX buffer expects, starting at SIDH. Does not touch
 * `message->data`; the caller streams that separately when the frame is not
 * a remote request.
 */
void mcp2515_frame_pack_header(const can_message_t *message, uint8_t header[MCP2515_FRAME_HEADER_SIZE]);

/*
 * The reverse: fill `message`'s id/extended/remote/length from a five-byte
 * SIDH..DLC header read out of an RX buffer. Does not touch `message->data`;
 * the caller reads that separately when `!message->remote`.
 */
void mcp2515_frame_unpack_header(const uint8_t header[MCP2515_FRAME_HEADER_SIZE],
                                  can_message_t *message);

/*
 * Pack a filter identifier's value into the four-byte SIDH/SIDL/EID8/EID0
 * layout RXFn and RXMn registers share with TX/RX buffers (everything but
 * DLC). `packed` carries CAN_FLAG_EXTENDED as can_id_pack() produces it, and
 * selects both the value's width and the EXIDE bit written to SIDL — the
 * MCP2515 compares a filter's frame type exactly and cannot mask it off.
 *
 * For a mask, use mcp2515_frame_pack_mask() instead: a mask's own
 * CAN_FLAG_EXTENDED says only that the frame type is compared, never which
 * layout its bits are in.
 */
void mcp2515_frame_pack_id(uint32_t packed, uint8_t out[4]);

/*
 * Pack an acceptance *mask* into that same four-byte layout.
 *
 * A mask is a bit field over an identifier rather than an identifier, so it
 * has to be laid out the way the filters it applies to are: a standard
 * filter's significant bits are the 11 of SID, an extended filter's are the
 * 11 of SID plus the 18 of EID. `extended` therefore comes from the bank's
 * filters and not from the mask value; mcp2515_filter_plan() works it out
 * and reports it as mcp2515_filter_plan_t::mask_extended.
 *
 * Laying a standard bank's mask out as an extended one is not a near miss.
 * Every significant bit lands below the SID field, so RXM's SID bits all
 * become don't-care — and its EID bits, which the controller compares
 * against the first two *data* bytes of a standard frame, take their place.
 * The filter then ignores the identifier and matches on payload: it accepts
 * frames it was told to reject, and rejects the one id it was given.
 */
void mcp2515_frame_pack_mask(uint32_t mask, bool extended, uint8_t out[4]);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MCP2515_FRAME_H */
