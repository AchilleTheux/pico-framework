# MCP2518FD hardware bench

Sends a frame each second, drains received frames, and prints counters every
five seconds over USB/UART. Loopback compares each received identifier, format,
length, BRS flag and payload against the sent frame, reporting pass/failure
counters and a 500 ms receive timeout.

## Equipment and wiring

Use a Pico/Pico 2 and an **external MCP2518FD** module with a known crystal.
Default: 40 MHz; 20 MHz and 4 MHz are also supported. The MCP2515 and the
`rp2350_can` board's onboard XL2515 cannot run this driver.

| Pico GPIO | MCP2518FD signal |
|-----------|------------------|
| 10 | SCK |
| 11 | SDI / MOSI |
| 12 | SDO / MISO |
| 9 | CS, preferably with a pull-up |
| optional 8 | INT; set `MCP2518FD_TEST_INT_PIN=8` |
| GND | GND |

Power the module according to its schematic, with 3.3 V compatible MCU-facing
signals. SPI1 runs at 1 MHz by default. Internal loopback requires no CAN peer
or connected CANH/CANL. Normal mode requires a transceiver (FD-rated for FD),
another active controller, common ground, and 120-ohm termination at each bus
end. Set any module transceiver standby/enable pins appropriately; this app
does not control them.

## Build and run

Start with internal loopback:

```sh
make BOARD=pico APP=tests/mcp2518fd_test PROFILE=loopback \
    CMAKE_ARGS=-DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON
make BOARD=pico APP=tests/mcp2518fd_test PROFILE=loopback flash
```

Use `BOARD=pico2` for RP2350. Open USB serial or UART at the SDK default
115200 baud. Expected: `MCP2518FD: ok`, paired queued/RX lines, increasing
`loopback_pass`, `fail=0`, and zero CRC/error/overflow counters. The five-frame
rotation covers classic standard, classic extended, RTR, 12-byte FD without
BRS, and 64-byte extended FD with BRS. Run at least a minute.

| Profile | Purpose |
|---------|---------|
| `default` | Classic CAN, 500 kbit/s; three classic frame types |
| `fd` | Mixed classic/FD, 500 kbit/s nominal, 2 Mbit/s data |
| `loopback` | Internal loopback of all five frame types |
| `monitor` | FD-capable silent monitor; no transmissions or ACKs |
| `filter` | Classic bus; receive only standard ID 0x123 |

For classic interoperability, connect a peer running `can_test` or
`mcp2515_test` at 500 kbit/s, and use `PROFILE=default`. Both ends should
receive standard, extended and RTR frames with stable errors. Repeat at
1 Mbit/s with both ends configured to match.

For FD, use two FD-capable nodes with matching nominal/data timing and
`PROFILE=fd`. Verify all 64 bytes on the peer; local enqueue success alone does
not prove transmission. Remove classic-only controllers from FD test buses,
since they can generate error frames on FD traffic. `monitor` requires two
other active nodes to supply traffic and acknowledgements.

For filtering, send the rotating standard 0x123, extended 0x1abcde0 and RTR
0x321 frames from a peer. Only standard 0x123 should arrive with `PROFILE=filter`.
Check extended 0x123 is also rejected. RX filters do not restrict app transmits.

Override app settings on first configuration:

```sh
make BOARD=pico2 APP=tests/mcp2518fd_test PROFILE=loopback \
    CMAKE_ARGS="-DMCP2518FD_TEST_OSCILLATOR_HZ=20000000 -DMCP2518FD_TEST_INT_PIN=8"
```

Other settings: `SPI_INSTANCE`, `SCK_PIN`, `MOSI_PIN`, `MISO_PIN`, `CS_PIN`,
`SPI_BAUDRATE_HZ`, `BITRATE`, `DATA_BITRATE`, `FD`, `FILTER`, `MODE`, all with
the `MCP2518FD_TEST_` prefix. Choose legal SPI pin functions. Changing
`CMAKE_ARGS` on an already configured make build does not update its cache;
explicitly configure with CMake or use a new build directory/profile.

## Failures and limits

- `no device` / `SPI CRC failure`: check power, CS, SDI/SDO, SPI mode and speed.
- `invalid argument`: check oscillator, rate pair, GPIO values and filters.
- `controller timeout`: the requested mode or clock did not settle.
- `TX FIFO full`: no ACKs, wrong timing, transceiver standby or disconnected
  bus; inspect `TREC`. `queued` is not a completion counter.
- CRC, overflow or CAN errors: inspect SPI integrity, receive polling rate,
  termination and peer timing respectively.
- `RAM fault`: retain ECC diagnostics, inspect wiring, and reinitialise.
- Loopback failure: retain the serial trace; matching payloads are required.

Loopback checks the MCU/SPI/controller path, not the transceiver,
acknowledgements or bus timing. Host tests and cross-compilation have passed;
**physical bench checks remain to be run on MCP2518FD hardware**.
