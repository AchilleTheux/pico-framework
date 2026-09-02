# bt_console_test

The framework's CLI, over Bluetooth.

The point is how little there is in `main.c`: the CLI, its built-in commands and
everything else are unchanged, and only the `cli_stream_t` differs.

## Required hardware

A **Pico W or Pico 2 W**, and a laptop or phone with Bluetooth. It builds for any
board; without a radio it says so over USB and does nothing else.

## Pairing

The board reports over **USB** whether Bluetooth came up, because if it did not
there is nowhere else to say so. Watch that first:

```text
bt_console_test  board pico2_w  radio present
bt_console_init: ok
pair with "pico-framework", then open the serial port it offers
```

Then, on Linux:

```bash
bluetoothctl
  scan on                      # wait for "pico-framework"
  pair    XX:XX:XX:XX:XX:XX
  trust   XX:XX:XX:XX:XX:XX
  quit

sudo rfcomm bind 0 XX:XX:XX:XX:XX:XX 1
picocom /dev/rfcomm0
```

The `1` is the RFCOMM channel. On Windows and macOS the board appears as a
serial port once paired, with no equivalent of `rfcomm bind`.

You should get:

```text
bt_console_test - type help
bt>
```

## What to try

```text
bt> help          the framework's built-in commands, over Bluetooth
bt> version
bt> uptime
bt> btstatus      link state, and bytes dropped for want of buffer
bt> flood 200     far more output than one RFCOMM packet
```

`flood` is the interesting one. It produces output much faster than RFCOMM
drains it, which is exactly the case the buffering exists for, and then reports
how much had to be dropped. On the default 2 KB output buffer a few hundred lines
should come through intact; a much larger flood will start dropping, and it will
say so rather than truncating quietly.

`cmd_flood` in `main.c` polls `bt_console_poll()` and sleeps 1 ms between every
printed line for exactly this reason: `RFCOMM_EVENT_CAN_SEND_NOW` only arrives
after a real HCI round trip with the controller, so real time has to pass
between prints or the whole flood lands in the 2 KB buffer before BTstack ever
gets a turn to drain it — confirmed on hardware (2026-09-01): without that
sleep, `flood 200` delivered only 34 lines and never printed its own `done`
summary, because the summary competed for the same already-full buffer.

## Expected result (validated on a Pico 2 W, 2026-09-01)

| Step | Expect |
|------|--------|
| USB output at boot | `radio present`, `bt_console_init: ok` |
| scanning from a host | `pico-framework` appears — give it up to ~30s; a short scan window can legitimately miss it |
| pairing | succeeds without a PIN prompt on a modern host |
| opening the port | the greeting and a `bt>` prompt |
| `help` | the full command list, uninterrupted |
| `flood 200` | 200 numbered lines in order, `0 bytes dropped` |
| `flood 500` | may report dropped bytes; whatever arrives is in order from the start |
| closing the terminal | USB reports `bluetooth peer disconnected` |
| reopening | greeting again, with no stale text from the last session |

Re-run on the same board (2026-09-03) after the `bt_stream` partial-send fix,
driving RFCOMM from an `AF_BLUETOOTH`/`BTPROTO_RFCOMM` socket rather than
`rfcomm bind` (no root needed, and it makes the check scriptable):

| Step | Result |
|------|--------|
| `btstatus` | `radio present`, `link connected`, `0 out, 0 in` |
| `flood 200` | all 200 lines, in order, bodies intact, `0 bytes dropped` |
| `flood 500` | 472 of 500 arrived, line numbers strictly increasing, every body intact, `1673 bytes dropped` and `btstatus` agreeing — the tail went, nothing was transposed |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `radio none on this board` | not a W board |
| `bt_console_init: bluetooth stack would not start` | BTstack refused; check the USB output for its own log lines |
| never appears when scanning | `discoverable` is false, or the host is caching an old scan. Try `scan off` then `scan on` |
| pairs but offers no serial port | the SDP record is not being read as one — this has been validated once (Linux, 2026-09-01) but not against every host OS |
| port opens but nothing arrives | `RFCOMM_EVENT_CAN_SEND_NOW` is not reaching the handler, so nothing is ever flushed |
| output arrives with gaps | dropped bytes; `btstatus` will say so and the buffer is too small |
| output arrives out of order | a genuine bug in the flow control, and worth reporting in detail — the host tests cover this and would need to have missed something |
| first thing seen is text from a previous session | the disconnect handler is not clearing the buffers |

## A warning

Pairing here **authenticates nothing** — Secure Simple Pairing's "just works"
association is accepted without comparing a number, because a robot has no
screen. Anyone in range during pairing gets a full console, `reboot` and
`bootsel` included. Set `discoverable = false` once paired; see the component
README.
