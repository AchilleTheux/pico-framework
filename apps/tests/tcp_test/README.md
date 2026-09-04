# tcp_test

Bench for the [`tcp`](../../../components/tcp/) component: a serial command line
that opens a TCP connection to a peer and pushes bytes through it.

Hardware tests are manual; this file is the procedure (DESIGN_DOC.md section 19).

## Required hardware

* A board with a radio — `pico2_w` or `pico_w`. It builds without one and says
  so, but there is nothing to test.
* A WiFi network both the board and a computer can reach.
* A computer on that network running a listener. Anything will do; `nc` is
  everywhere:

```bash
nc -l 5000          # BSD/macOS: nc -l 5000, Linux: nc -l -p 5000
```

No wiring. The board needs USB for the console and nothing else.

## Running

```bash
make BOARD=pico2_w APP=tests/tcp_test PROFILE=default
make BOARD=pico2_w APP=tests/tcp_test PROFILE=default flash
picocom -b 115200 /dev/ttyACM0
```

## Setting up, once

Settings live in flash, not in the source (DESIGN_DOC.md section 13):

```text
tcp> ssid my-network
tcp> password my-passphrase
tcp> host 192.168.1.20        # the computer running nc
tcp> port 5000
tcp> save
tcp> connect                  # associate
tcp> open                     # connect to the peer
```

After `save`, a reboot associates on its own; `open` is still explicit, because
a bench that dials out unprompted is harder to reason about than one that does
not.

## Commands

| Command | Does |
|---------|------|
| `ssid` / `password` | WiFi credentials |
| `connect` | associate with the stored network |
| `wifistatus` | radio and link state |
| `host` / `port` | the peer to connect to; `host` takes a name or a dotted quad |
| `save` | write settings to flash |
| `open` / `close` | open and close the connection |
| `send <text>` | one line to the peer |
| `flood <lines>` | far more than the outgoing buffer holds |
| `status` | state, sessions, attempts, byte counters, pending, unread, dropped |
| `reconnect` | toggle automatic reconnection (re-`open` to apply) |

Anything the peer sends is printed as `[rx] ...` as it arrives.

## The tests that matter

Each of these exercises something the host tests cannot reach.

### 1. A connection, and bytes both ways

`open`, then `send hello`. `hello` appears in the `nc` window. Type something
into `nc` and press enter; it appears on the console as `[rx]`. `status` shows
`sent` and `received` moving.

This is the whole raw-API path: PCB creation, DNS or a literal address,
`tcp_write`, `tcp_output`, `tcp_recved`, and the receive callback.

### 2. `flood`, which is what the ordering logic is for

`flood 500` queues far more than the 512-byte outgoing buffer holds, so the
component has to spread it across many partial sends while lwIP's window opens
and closes.

**What to check is the far end, not the board.** The lines must arrive
numbered `line 0` to `line 499`, in order, none missing and none garbled. Piping
`nc` to a file and checking is the honest way:

```bash
nc -l 5000 > flood.txt
# then, after it settles:
seq 0 499 | sed 's/^/line /' | diff - <(tr -d '\r' < flood.txt) && echo "in order"
```

A reordering here would be the bug `tcp_stream`'s peek-then-discard exists to
prevent — the same one that was a real finding against `bt_stream` in
REVIEW.md. Host tests pin it against a fake link; this is the version with a
real window.

### 3. Back-pressure, by not reading

Leave the board connected and push a few kilobytes at it from `nc` faster than
the console drains — pasting a large file works. The receive buffer fills, the
component declines segments with `ERR_MEM`, and TCP's window closes.

What must **not** happen is lost bytes: whatever `nc` sent should come out of
the `[rx]` prints in order, just slowly. `status` has no receive drop counter
because there is nothing to count; if bytes go missing here, the back-pressure
path is wrong.

### 4. Reconnection

With `reconnect` on and the connection open, kill `nc` (Ctrl-C). The console
prints `[tcp] closed by the far end`, then the retry backoff, and `status` shows
`attempts` climbing. Restart `nc`; the board reconnects on its own and `sessions`
becomes 2.

Then `reconnect` off, `open` again, and kill `nc`: it must go to `idle` and stay
there.

### 5. A peer that is not there

Set `port 5001` with nothing listening and `open`. It must fail within about ten
seconds — the component's `connect_timeout_ms`, not lwIP's own timeout, which is
minutes — and enter the backoff.

Set `host` to a name that does not resolve. Same outcome, from the DNS path.

## Expected result

| Step | Expect |
|------|--------|
| boot | `tcp_test board pico2_w radio present` |
| `connect` | `[wifi] connected as 192.168.x.y` |
| `open` | `[tcp] connecting` then `[tcp] connected` |
| `send hello` | `hello` in the `nc` window |
| typing into `nc` | `[rx] ...` on the console |
| `flood 500` | 500 lines at the far end, in order, none missing |
| kill `nc` | `[tcp] closed by the far end`, then retries |
| restart `nc` | reconnects, `sessions` 2 |
| unlistened port | fails in ~10 s, not minutes |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `open` sits in `resolving` | DNS is not answering; try a dotted quad to isolate it |
| `connecting` then backoff, repeatedly | wrong address or port, or a firewall on the computer |
| `flood` lines out of order | a reordering bug in the send path — the serious one |
| `flood` lines missing | look at `status`'s `dropped`: nonzero means the buffer filled and the tail went, which is the documented behaviour, not a bug |
| bytes lost when not reading | the receive back-pressure path, which must never drop |
| connects but nothing arrives | the peer is connected to a different board; check `wifistatus` |

## What this proves, and what it does not

It proves the component against one peer on one network. It says nothing about
several connections at once (the component is one per instance by design), about
behaviour on a congested or lossy link, or about `MEMP_NUM_TCP_PCB` exhaustion.
It is also not a throughput benchmark: the console printing every received chunk
is the bottleneck long before the stack is.

## Status

**Not yet run.** The application builds warning-free for `pico2_w`, `pico_w` and
`pico2` (no radio) in the CI matrix. Nothing above has been executed against a
real peer. Record results here when it is.
