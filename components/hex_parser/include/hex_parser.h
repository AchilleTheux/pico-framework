/*
 * hex_parser - Intel HEX record decoding.
 *
 * The format a linker emits alongside an ELF, and the one the reference
 * firmware shipped its updates in. One record per line:
 *
 *     :LLAAAATT[DD...]CC
 *      |  |   |  |    checksum: two's complement of the sum of every byte
 *      |  |   |  data, LL bytes
 *      |  |   record type
 *      |  16-bit address, or a payload for the address-extension types
 *      byte count
 *
 * A 16-bit address cannot reach RP2040 flash at 0x10000000, so real images are
 * a sequence of type 04 records setting the upper half of the address followed
 * by type 00 records carrying data at offsets below it. The parser tracks that
 * base, so callers see one flat 32-bit address per data record and never have
 * to reassemble it themselves.
 *
 * Stateless with respect to the data: it decodes one line at a time into a
 * caller-owned record and keeps only the address base. No Pico SDK dependency,
 * so it is unit-tested on the host against real records.
 */

#ifndef PICO_FRAMEWORK_HEX_PARSER_H
#define PICO_FRAMEWORK_HEX_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The byte-count field is one byte, so this is the format's own maximum.
   Most tools emit 16 or 32. */
#define HEX_MAX_RECORD_DATA 255u

/* Longest line the format can produce: ':' + count + address + type + data +
   checksum, two characters per byte, plus a terminator. */
#define HEX_MAX_LINE_LENGTH (1u + 2u * (HEX_MAX_RECORD_DATA + 5u) + 1u)

typedef enum {
    HEX_RECORD_DATA = 0x00,
    HEX_RECORD_EOF = 0x01,
    HEX_RECORD_EXTENDED_SEGMENT = 0x02,
    HEX_RECORD_START_SEGMENT = 0x03,
    HEX_RECORD_EXTENDED_LINEAR = 0x04,
    HEX_RECORD_START_LINEAR = 0x05,
} hex_record_type_t;

typedef enum {
    HEX_OK = 0,
    HEX_ERR_INVALID_ARG,
    HEX_ERR_NO_START_CODE,   /* blank line, or one not beginning with ':' */
    HEX_ERR_BAD_CHARACTER,   /* something that is not a hex digit */
    HEX_ERR_BAD_LENGTH,      /* the line does not match its own byte count */
    HEX_ERR_BAD_CHECKSUM,
    HEX_ERR_UNKNOWN_TYPE,
    HEX_ERR_MALFORMED_RECORD, /* right shape, wrong payload size for its type */
    HEX_ERR_AFTER_EOF,       /* a record following the end-of-file record */
} hex_result_t;

typedef struct {
    hex_record_type_t type;

    /* For a data record, the full 32-bit address with the current base
       applied. Zero for every other type. */
    uint32_t address;

    uint8_t data[HEX_MAX_RECORD_DATA];
    uint8_t length;
} hex_record_t;

typedef struct {
    uint32_t address_base;    /* from the most recent type 02 or 04 record */
    uint32_t start_address;   /* from a type 03 or 05 record */
    bool have_start_address;
    bool seen_eof;
} hex_parser_t;

/* Clears the address base and the end-of-file state. Call before each file. */
void hex_parser_reset(hex_parser_t *parser);

/*
 * Decode one line.
 *
 * `line` is NUL-terminated; leading whitespace and a trailing CR or LF are
 * ignored, so a line read straight off a serial link needs no cleaning up.
 * `record` is filled only when HEX_OK is returned.
 *
 * Blank lines report HEX_ERR_NO_START_CODE rather than being skipped
 * silently — a caller streaming a file usually wants to ignore them, but
 * deciding that is the caller's business, not the parser's.
 */
hex_result_t hex_parser_feed(hex_parser_t *parser, const char *line,
                             hex_record_t *record);

/* True once an end-of-file record has been accepted. */
static inline bool hex_parser_is_complete(const hex_parser_t *parser)
{
    return parser->seen_eof;
}

/* Human-readable name for a result code. Never NULL. */
const char *hex_result_name(hex_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_HEX_PARSER_H */
