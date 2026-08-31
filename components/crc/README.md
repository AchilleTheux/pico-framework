# crc

CRC-32 and CRC-16, in their standard parameterisations.

| | Parameters | Check value for `"123456789"` |
|---|---|---|
| `crc32` | CRC-32/ISO-HDLC: poly `0xEDB88320` reflected, init and final xor `0xFFFFFFFF` | `0xCBF43926` |
| `crc16_ccitt` | CRC-16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`, no final xor | `0x29B1` |

Standard parameterisations matter more than the algorithm here. A CRC is only
useful if the value computed on the microcontroller matches the one computed by
whatever built the firmware image, so the tests check against the *published*
check values rather than against this code's own output.

## Incremental use

The reason both come in an incremental form: a firmware image is checksummed as
it arrives in chunks and again as it is read back from flash, and neither fits
in RAM.

```c
uint32_t state = crc32_begin();
while (read_chunk(buffer, &len)) {
    state = crc32_update(state, buffer, len);
}
uint32_t image_crc = crc32_end(state);
```

**The running state is not the CRC.** It is the raw register, still inverted;
only `crc32_end()` produces a comparable value. Storing a running state and
calling it a checksum is the classic way to end up with two tools disagreeing.

## Why bitwise, not a table

A 256-entry CRC-32 table costs 1 KiB of flash to save about seven cycles per
byte. Neither caller is in a hot path — checksumming a 64 KiB image bitwise
takes a few milliseconds against a transfer that takes seconds.

Both RP2040 and RP2350 can also compute CRC-32 in hardware through the DMA
sniffer, which is far faster again. It is not used here because it would make
the component claim a DMA channel and stop being host-testable, which is a poor
trade for the speed this actually needs.

## Testing

`make test` checks the published check values, several known short inputs,
that incremental and one-shot agree across many chunk sizes, and that single
bit flips and truncation are detected. Two properties matter for firmware
images specifically and are tested directly: leading zero bytes change the
result (which is why the init is `0xFFFFFFFF`, not 0), and erased flash — all
`0xFF` — does not checksum to 0 or to `0xFFFFFFFF`.
