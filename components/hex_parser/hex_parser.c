#include <string.h>

#include "hex_parser.h"

/* Bytes before the data: count, two address bytes, type. Plus the trailing
   checksum, which is why sizing uses +5. */
#define PREFIX_BYTES 4u
#define FRAME_BYTES (PREFIX_BYTES + 1u)

#define OFFSET_COUNT 0u
#define OFFSET_ADDRESS_HIGH 1u
#define OFFSET_ADDRESS_LOW 2u
#define OFFSET_TYPE 3u

void hex_parser_reset(hex_parser_t *parser)
{
    if (parser != NULL) {
        *parser = (hex_parser_t){ 0 };
    }
}

static bool decode_nibble(char c, uint8_t *out)
{
    if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
    } else if (c >= 'A' && c <= 'F') {
        *out = (uint8_t)(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
        *out = (uint8_t)(c - 'a' + 10);
    } else {
        return false;
    }
    return true;
}

static bool decode_byte(const char *text, uint8_t *out)
{
    uint8_t high, low;

    if (!decode_nibble(text[0], &high) || !decode_nibble(text[1], &low)) {
        return false;
    }
    *out = (uint8_t)((high << 4) | low);
    return true;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Length of the line with any trailing whitespace, CR or LF removed. */
static size_t trimmed_length(const char *line)
{
    size_t len = strlen(line);
    while (len > 0 && is_space(line[len - 1])) {
        len--;
    }
    return len;
}

/*
 * The extension records carry a 16-bit value, so anything else is malformed
 * even when its checksum is right.
 */
static hex_result_t apply_extension(hex_parser_t *parser, const hex_record_t *record)
{
    if (record->length != 2) {
        return HEX_ERR_MALFORMED_RECORD;
    }

    const uint32_t value = ((uint32_t)record->data[0] << 8) | record->data[1];

    switch (record->type) {
        case HEX_RECORD_EXTENDED_LINEAR:
            /* Upper 16 bits of a 32-bit address. This is what puts an image at
               0x10000000. */
            parser->address_base = value << 16;
            break;
        case HEX_RECORD_EXTENDED_SEGMENT:
            /* The 8086 form: a paragraph number, so 16 times the value. */
            parser->address_base = value << 4;
            break;
        default:
            return HEX_ERR_UNKNOWN_TYPE;
    }
    return HEX_OK;
}

static hex_result_t apply_start_address(hex_parser_t *parser, const hex_record_t *record)
{
    if (record->length != 4) {
        return HEX_ERR_MALFORMED_RECORD;
    }

    parser->start_address = ((uint32_t)record->data[0] << 24) |
                            ((uint32_t)record->data[1] << 16) |
                            ((uint32_t)record->data[2] << 8) |
                            (uint32_t)record->data[3];
    parser->have_start_address = true;
    return HEX_OK;
}

hex_result_t hex_parser_feed(hex_parser_t *parser, const char *line,
                             hex_record_t *record)
{
    if (parser == NULL || line == NULL || record == NULL) {
        return HEX_ERR_INVALID_ARG;
    }

    /* Leading whitespace is tolerated; the record itself starts at ':'. */
    while (*line != '\0' && is_space(*line)) {
        line++;
    }
    if (*line != ':') {
        return HEX_ERR_NO_START_CODE;
    }

    const char *body = line + 1;
    const size_t body_length = trimmed_length(body);

    /* Every field is two characters, so an odd count cannot be a record. */
    if (body_length < 2u * FRAME_BYTES || (body_length % 2u) != 0) {
        return HEX_ERR_BAD_LENGTH;
    }

    uint8_t bytes[FRAME_BYTES + HEX_MAX_RECORD_DATA];
    const size_t byte_count = body_length / 2u;

    if (byte_count > sizeof(bytes)) {
        return HEX_ERR_BAD_LENGTH;
    }

    for (size_t i = 0; i < byte_count; i++) {
        if (!decode_byte(&body[i * 2u], &bytes[i])) {
            return HEX_ERR_BAD_CHARACTER;
        }
    }

    /* The line must be exactly as long as its own byte count claims: too short
       would read past the data, too long would hide trailing rubbish. */
    const uint8_t declared = bytes[OFFSET_COUNT];
    if (byte_count != (size_t)declared + FRAME_BYTES) {
        return HEX_ERR_BAD_LENGTH;
    }

    /* Every byte including the checksum sums to zero. */
    uint8_t sum = 0;
    for (size_t i = 0; i < byte_count; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    if (sum != 0) {
        return HEX_ERR_BAD_CHECKSUM;
    }

    /* Trailing rubbish after a complete file would otherwise be applied as if
       it were part of the image. */
    if (parser->seen_eof) {
        return HEX_ERR_AFTER_EOF;
    }

    const uint8_t type = bytes[OFFSET_TYPE];
    const uint16_t offset = (uint16_t)(((uint16_t)bytes[OFFSET_ADDRESS_HIGH] << 8) |
                                       bytes[OFFSET_ADDRESS_LOW]);

    *record = (hex_record_t){ 0 };
    record->type = (hex_record_type_t)type;
    record->length = declared;
    if (declared > 0) {
        memcpy(record->data, &bytes[PREFIX_BYTES], declared);
    }

    switch (type) {
        case HEX_RECORD_DATA:
            record->address = parser->address_base + offset;
            return HEX_OK;

        case HEX_RECORD_EOF:
            if (declared != 0) {
                return HEX_ERR_MALFORMED_RECORD;
            }
            parser->seen_eof = true;
            return HEX_OK;

        case HEX_RECORD_EXTENDED_SEGMENT:
        case HEX_RECORD_EXTENDED_LINEAR:
            return apply_extension(parser, record);

        case HEX_RECORD_START_SEGMENT:
        case HEX_RECORD_START_LINEAR:
            return apply_start_address(parser, record);

        default:
            return HEX_ERR_UNKNOWN_TYPE;
    }
}

const char *hex_result_name(hex_result_t result)
{
    switch (result) {
        case HEX_OK:                    return "ok";
        case HEX_ERR_INVALID_ARG:       return "invalid argument";
        case HEX_ERR_NO_START_CODE:     return "no ':' start code";
        case HEX_ERR_BAD_CHARACTER:     return "not a hex digit";
        case HEX_ERR_BAD_LENGTH:        return "length does not match byte count";
        case HEX_ERR_BAD_CHECKSUM:      return "bad checksum";
        case HEX_ERR_UNKNOWN_TYPE:      return "unknown record type";
        case HEX_ERR_MALFORMED_RECORD:  return "malformed record";
        case HEX_ERR_AFTER_EOF:         return "record after end of file";
        default:                        return "unknown";
    }
}
