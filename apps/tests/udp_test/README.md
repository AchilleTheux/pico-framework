# udp_test

Bench for the [`udp`](../../../components/udp/) component: a serial command line
that binds a port, sends datagrams, and prints what arrives.

Hardware tests are manual; this file is the procedure (DESIGN_DOC.md section 19).

## Required hardware

* A board with a radio — `pico2_w` or `pico_w`. It builds without one and says
  so, but there is nothing to test.
* A WiFi network both the board and a computer can reach.
* A computer on that network. `nc -u` is enough for everything here; a second
  board running this same firmware is better for the broadcast test.

No wiring. The board needs USB for the console and nothing else.

## Running

```bash
make BOARD=pico2_w APP=tests/udp_test PROFILE=default
make BOARD=pico2_w APP=tests/udp_test PROFILE=default flash
picocom -b 115200 /dev/ttyACM0
```

## Setting up, once

```text
udp> ssid my-network
udp> password my-passphrase
udp> peer 192.168.1.20        # the computer
udp> peerport 5005
udp> localport 5005
udp> save
udp> connect                  # associate
udp> bind                     # open the socket
```

## Commands

| Command | Does |
|---------|------|
| `ssid` / `password` | WiFi credentials |
| `connect` | associate with the stored network |
| `wifistatus` | radio and link state |
| `peer` / `peerport` | where `send` goes; `peer` takes a name or a dotted quad |
| `localport` | port to bind; 0 asks lwIP for an ephemeral one |
| `bind` / `close` | open and close the socket |
| `send <text>` | one datagram to the peer |
| `bcast <text>` | one datagram to every host on the network |
| `toolong` | one datagram past `UDP_MAX_PAYLOAD`, which must be refused |
| `echo` | toggle replying to every datagram received |
| `status` | socket state and counters |

Every datagram received is printed with its sender's address and port.

## The tests that matter

### 1. A datagram out

On the computer:

```bash
nc -u -l 5005
```

Then `send hello`. `hello` appears there. `status` shows `sent 1`.

### 2. A datagram in, and the sender's address

From the computer, aiming at the board's address (from `wifistatus`):

```bash
echo "from the laptop" | nc -u -w1 192.168.1.31 5005
```

The console prints the sender's address and port and the payload. **The address
must be the computer's**, not zeros and not the board's — that line is the whole
`udp_endpoint_t` translation, from lwIP's `ip_addr_t` through
`udp_ipv4_format()`.

### 3. Echo, which makes a round trip visible

`echo` on, then from the computer:

```bash
nc -u 192.168.1.31 5005
```

Type a line; it comes straight back. This exercises
`udp_socket_send_to_endpoint()` from inside the receive callback, which is the
path a request/response protocol uses and the one most likely to go wrong if the
callback's rules are not respected.

### 4. Broadcast

The one that needs a second listener. On the computer:

```bash
nc -u -l 5005
```

Then `bcast anyone there`. It must arrive **without the board having been told
the computer's address** — that is the property discovery depends on. With a
second board running this firmware and bound to the same port, it should print
the datagram too.

Note that many access points do not forward broadcast between wireless clients,
and some networks block it outright. A failure here is as likely to be the
network as the firmware; test against a simple home router before concluding
anything.

### 5. A hostname, and the resolving contract

Set `peer` to a name (`peer my-laptop.local`, or any resolvable host) and `send`.
The first attempt should report `resolving; run send again in a moment` and send
nothing. Run `send` again: it goes.

That is the documented contract, not a bug — see [`udp`'s
README](../../../components/udp/README.md). If the first `send` succeeds, the
name was already in lwIP's cache, which is also correct; clear it by
power-cycling to see the two-step form.

### 6. Refusals that must not be silent

* `toolong` sends `UDP_MAX_PAYLOAD + 1` bytes and must print
  `longer than one datagram` rather than sending a fragment.
* With a strict address, `peer 192.168.1.010` then `send`: the parser must
  reject it rather than addressing 192.168.1.**8**. This is the whole reason the
  component has its own parser instead of lwIP's.

### 7. An oversized datagram in

Send the board more than `UDP_MAX_DATAGRAM` (512) bytes:

```bash
head -c 1000 /dev/zero | tr '\0' 'a' | nc -u -w1 192.168.1.31 5005
```

Nothing is printed and `status` shows `dropped 1`. Counted, not truncated: a
fragment handed over as if it were the whole message is worse than no callback.

## Expected result

| Step | Expect |
|------|--------|
| boot | `udp_test board pico2_w radio present` |
| `connect` | `[wifi] connected as 192.168.x.y` |
| `bind` | `listening on port 5005` |
| `send hello` | `hello` at the computer |
| datagram in | `[rx] <computer's address>:<port>` and the payload |
| `echo` on | typed lines come back |
| `bcast` | arrives with no address configured |
| hostname `send` | `resolving` first, sent on the retry |
| `toolong` | `longer than one datagram` |
| 1000-byte datagram in | nothing printed, `dropped 1` |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `bind` reports an error | something else holds the port; try `localport 0` |
| nothing arrives at the board | a firewall on the computer, or the wrong address — check `wifistatus` |
| sender address prints as `0.0.0.0` | the `ip_addr_t` translation, not the network |
| `bcast` never arrives | very likely the access point, not the firmware; try a different network |
| `send` always says `resolving` | DNS is not answering at all; a dotted quad isolates it |

## What this proves, and what it does not

It proves the component on one network with one peer. It says nothing about
behaviour under datagram loss (nothing here induces any), about several sockets
at once, about `MEMP_NUM_UDP_PCB` exhaustion, or about multicast — which is not
supported and is refused rather than tested.

## Status

**Not yet run.** The application builds warning-free for `pico2_w`, `pico_w` and
`pico2` (no radio) in the CI matrix. Nothing above has been executed on a
network. Record results here when it is.
