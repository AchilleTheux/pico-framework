# cli_test

Hardware test for the `cli` component. Hardware tests are manual; this file is
the procedure (DESIGN_DOC.md section 19).

Most of the interpreter is covered by host tests (`make test`). What this
application adds is the part those cannot reach: that the transports work
against real silicon, and that the whole thing behaves under a terminal.

## Required hardware

* Any RP2040 or RP2350 board.
* For `PROFILE=uart`, a USB-to-serial adapter on GPIO 0/1.

## Wiring

Nothing, for the default profile — the CLI runs on stdio over USB.

For `PROFILE=uart`:

| Adapter | Board |
|---------|-------|
| RX      | GPIO 0 (board TX) |
| TX      | GPIO 1 (board RX) |
| GND     | GND |

## Running

```bash
make BOARD=pico2 APP=tests/cli_test PROFILE=default
make BOARD=pico2 APP=tests/cli_test PROFILE=default flash
picocom -b 115200 /dev/ttyACM0
```

Profiles:

| Profile | Transport |
|---------|-----------|
| `default` | stdio (USB CDC and the default UART), echo on |
| `uart` | `uart0` on GPIO 0/1 at 115200, independent of stdio |
| `machine` | stdio with echo off, for a script on the other end |

## Procedure

The banner `cli_test - type help` and a `> ` prompt should appear. Then:

| Type | Expect |
|------|--------|
| `help` | the command list, each with its one-line description |
| `?` | the same list |
| `ping` | `pong` |
| `PING` | `pong` — names are case-insensitive |
| `info` | board, SDK version, uptime, and the transport in use |
| `led on` / `led off` | the board LED follows (absent on boards without a plain GPIO LED, such as `pico_w`) |
| `add 2 3` | `5` |
| `add 0x10 0x20` | `48` — `0x` prefixes are hex |
| `add 2` | `usage: ...` then `error 1` |
| `add 2 xyz` | `usage: ...` then `error 1` — a partly-numeric token is rejected |
| `add 1,2` | `3` — commas separate arguments too |
| `parse -5 ff 1.25` | `i32=-5 hex=0x000000FF float=1.2500` |
| `echo hello  world` | `[hello  world]` — inner spacing preserved |
| `fail` | `error 4` |
| `nosuch` | `unknown command: nosuch` |
| (empty line) | just a new prompt, no error |
| typing then backspace | the character is erased on screen and from the line |
| a line over 127 characters | `line too long`, and the next command still works |
| `bootsel` | the board re-enumerates as RPI-RP2 for flashing |

With `PROFILE=machine`, nothing is echoed: only replies come back.

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| no banner over USB | the console attached after the 2 s startup delay — reconnect and press enter |
| every command `unknown` | the terminal is sending something other than CR/LF, or is in local-echo-only mode |
| characters doubled on screen | the terminal is echoing locally as well; use `PROFILE=machine` or turn off local echo |
| nothing at all on `PROFILE=uart` | TX/RX swapped, or the adapter's ground is not tied to the board's |
| `led` missing from `help` | expected on boards with no `PICO_DEFAULT_LED_PIN`, such as `pico_w` |
