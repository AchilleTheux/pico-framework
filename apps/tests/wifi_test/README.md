# wifi_test

Bench for the `wifi` component.

## Required hardware

A **Pico W or Pico 2 W**. It builds for any board, but on one without a radio it
says so and does nothing else — which is itself worth checking.

## Setting up, once

```text
wifi> ssid robot-net
wifi> password the-actual-password
wifi> save
wifi> connect
[wifi] connecting
[wifi] connected as 192.168.1.47
```

`save` puts the credentials in flash through `persistent_config`, so after a
power cycle it associates on its own with no console involved — which is what a
deployed robot needs.

**The passphrase is never echoed back.** `password` with no argument reports
`<set>` or `<unset>` and nothing more; a console log is the last place a
passphrase should end up.

## Commands

```text
wifi> ssid [name]           show or set
wifi> password [value]      set only; never printed
wifi> hostname [name]       how the robot appears on the network
wifi> save                  keep them in flash
wifi> connect               associate, and keep reconnecting
wifi> disconnect            stop trying
wifi> wifistatus            state, address, signal, attempts
```

State changes are announced as they happen, so an outage and its recovery are
visible without polling `wifistatus`.

## The test that matters

Association is the easy part. What this component is actually for is what
happens next:

1. `connect`, and wait for `[wifi] connected as ...`
2. **Turn the access point off.** Within a few seconds:
   `[wifi] waiting to retry`
3. Leave it off for a minute. `wifistatus` should show the attempt count
   climbing and the delay lengthening — 1 s, 2 s, 4 s, up to the 15 s ceiling.
4. **Turn it back on.** It should reconnect within one retry interval, with no
   intervention.
5. Power-cycle the board. It should associate on its own from stored
   credentials.

Step 3 is the one worth being patient for: a component that retries every
100 ms forever floods the air, and one that gives up leaves a robot offline.

## Expected result

| Step | Expect |
|------|--------|
| first boot, no credentials | `radio present`, state `idle` |
| `password short` | refused: passphrase shorter than 8 characters |
| `connect` with no ssid | `credentials: no ssid` |
| `connect` | `connecting`, then `connected as <address>` within a few seconds |
| `wifistatus` when connected | an address and an RSSI, typically −40 to −70 dBm |
| access point off | `waiting to retry`, attempts climbing |
| access point back | `connected` again, attempts back to 0 |
| power cycle | associates without a console |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `radio none on this board` | not a W board. Build for `pico_w` or `pico2_w` |
| `wifi_init: the radio would not start` | the CYW43 firmware did not upload — usually a genuine hardware fault |
| stuck in `connecting`, then `waiting to retry` | wrong passphrase, or a 5 GHz-only network. The CYW43439 is 2.4 GHz only |
| `connected` but no address | DHCP did not answer. State goes back to retrying |
| connects, then drops repeatedly | signal. Check the RSSI; below about −80 dBm is marginal |
| nothing ever happens, no state changes at all | `wifi_poll()` is not being called. In poll mode nothing runs without it |
| RSSI reads 0 | only reported while connected |

## What this cannot tell you

Whether anything can be *done* over the link. This component gets an address;
TCP and UDP are deliberately somebody else's job, so there is nothing here that
sends a packet.
