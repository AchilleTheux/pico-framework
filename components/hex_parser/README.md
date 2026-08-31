# hex_parser

Intel HEX record decoding — the format a linker emits alongside an ELF, and the
one a firmware update arrives in over a serial link.

```text
:LLAAAATT[DD...]CC
 |  |   |  |    checksum: two's complement of the sum of every byte
 |  |   |  data, LL bytes
 |  |   record type
 |  16-bit address, or a payload for the address-extension types
 byte count
```

## Why the address extension matters

A record's address field is 16 bits, which cannot reach RP2040 flash at
`0x10000000`. Real images are therefore a type 04 record setting the upper half
of the address, followed by type 00 records carrying data at offsets below it:

```text
:020000041000EA                              <- base becomes 0x10000000
:10000000000102030405060708090A0B0C0D0E0F78  <- data at 0x10000000
:10001000101112131415161718191A1B1C1D1E1F68  <- data at 0x10000010
:00000001FF                                  <- end of file
```

The parser tracks that base, so a caller sees one flat 32-bit address per data
record. Getting this wrong does not fail loudly — it writes the image to page
zero instead of to flash — which is why the address-extension cases are the
most thoroughly tested part of the component.

| Type | Meaning | Handled |
|------|---------|---------|
| 00 | data | address base applied |
| 01 | end of file | sets the complete flag |
| 02 | extended segment address | base = value × 16 |
| 03 | start segment address | recorded, does not affect the base |
| 04 | extended linear address | base = value << 16 |
| 05 | start linear address | recorded, does not affect the base |

## Usage

```c
hex_parser_t parser;
hex_parser_reset(&parser);

for (each line) {
    hex_record_t record;
    const hex_result_t result = hex_parser_feed(&parser, line, &record);

    if (result != HEX_OK) {
        /* hex_result_name(result) says what was wrong */
        break;
    }
    if (record.type == HEX_RECORD_DATA) {
        write(record.address, record.data, record.length);
    }
}

if (!hex_parser_is_complete(&parser)) {
    /* no end-of-file record: the transfer was cut short */
}
```

Leading whitespace and a trailing CR or LF are tolerated, so a line read
straight off a serial link needs no cleaning up. Blank lines report
`HEX_ERR_NO_START_CODE` rather than being skipped silently — a caller streaming
a file usually wants to ignore them, but that is the caller's decision.

## What it refuses

Everything here happens on a real serial link, so each is a case worth naming:

| | |
|---|---|
| a bad checksum | one byte mangled in transit |
| a line shorter than its byte count | truncated, would otherwise read past the data |
| a line longer than its byte count | trailing rubbish hidden after a valid record |
| an odd number of digits | a dropped character |
| an extension record not carrying exactly two bytes | would set a nonsense address base |
| an end-of-file record carrying data | malformed |
| any record after the end-of-file record | line noise after the transfer finished |

A rejected line leaves the parser usable, so one bad line on a noisy link does
not poison the rest of the transfer.

## Testing

`make test` covers the format against real records — generated independently
and checked against the format's own rules, not captured from this parser. The
malformed cases are built with *valid* checksums where possible, so each is
rejected by the check it is meant to exercise rather than accidentally by the
checksum.
