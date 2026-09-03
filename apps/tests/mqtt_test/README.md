# mqtt_test

Bench for the [`mqtt`](../../../components/mqtt/) component, riding on top of
[`wifi`](../../../components/wifi/).

## Required hardware

A **Pico W or Pico 2 W**. It builds for any board, but on one without a radio
it says so and does nothing else, same as `wifi_test`.

## Setting up, once

```text
mqtt> ssid robot-net
mqtt> password the-actual-password
mqtt> broker test.mosquitto.org
mqtt> clientid my-pico
mqtt> save
mqtt> connect
[wifi] connecting
[wifi] connected as 192.168.1.47
mqtt> mqttconnect
[mqtt] resolving
[mqtt] connecting
[mqtt] connected
```

`save` puts every setting -- WiFi credentials and broker details alike -- in
flash through `persistent_config`, so after a power cycle both reconnect on
their own with no console involved. `mqttuser`/`mqttpass` are there if the
broker needs them; leave them unset for an anonymous connection like
`test.mosquitto.org`.

**`password` and `mqttpass` are never echoed back.** Given with no argument
they report `<set>` or `<unset>` and nothing more.

## Commands

```text
mqtt> ssid [name]              wifi ssid, show or set
mqtt> password [value]         wifi passphrase, set only; never printed
mqtt> hostname [name]          how the robot appears on the network
mqtt> connect                  associate, and keep reconnecting
mqtt> disconnect               stop trying
mqtt> wifistatus                wifi state, address, signal

mqtt> broker [host]            broker hostname or address, show or set
mqtt> port [n]                 broker port, show or set (default 1883)
mqtt> clientid [id]            mqtt client id, show or set
mqtt> mqttuser [name]          broker username, show or set
mqtt> mqttpass [value]         broker password, set only; never printed
mqtt> save                     keep every setting in flash

mqtt> mqttconnect              connect to the broker, and keep retrying
mqtt> mqttdisconnect           stop trying
mqtt> mqttstatus               mqtt state, broker, attempts, dropped
mqtt> sub <topic> [qos]        subscribe
mqtt> unsub <topic>            unsubscribe
mqtt> pub <topic> <message>    publish, qos 0
mqtt> drop                     close and reopen the session
mqtt> discovery <topic>        publish a large JSON document, qos 1, retained
```

Both the WiFi link and the MQTT session announce their state changes as they
happen. An incoming message on a subscribed topic prints as
`[mqtt] <topic>: <payload>` without needing to poll anything.

## The test that matters

Connecting is the easy part; the round trip is what this component is for.
Against a public broker (`test.mosquitto.org`) with no other client involved:

```text
mqtt> sub pico-framework/test 0
ok
mqtt> pub pico-framework/test hello
ok

[mqtt] pico-framework/test: hello
```

A broker echoes a publish back to every matching subscriber, the publisher
included, so subscribing and publishing to the same topic is a complete
loopback test needing nothing else. `unsub` should then stop delivery to that
topic.

### Subscriptions after a reconnect

lwIP always connects with the clean-session flag set, so the broker forgets
this client's subscriptions the instant the session drops. Nothing reports
that: `mqttstatus` still says `connected` and messages simply stop arriving.
`drop` closes and reopens the session on demand so that failure can be
provoked rather than waited for.

```text
mqtt> sub pico-framework/test 1
ok
mqtt> drop
dropped after session 2; watch for the next one

[mqtt] connected, session 3
[mqtt] resubscribe pico-framework/test qos 1: ok
```

The re-subscription is not the component's doing -- it is this application
replaying its own list from `on_connect`, which is the pattern every real
client needs. `mqttstatus` lists the standing subscriptions it will replay.
A publish to that topic from somewhere else afterwards must arrive; if it does
not, the replay is broken.

### A publish larger than lwIP's output buffer

`discovery` builds a Home Assistant-shaped announcement with the
[`json`](../../../components/json/) component and publishes it retained at
QoS 1. It is a few hundred bytes, which matters: lwIP defaults
`MQTT_OUTPUT_RINGBUF_SIZE` to 256, and a publish larger than that ring buffer
is not split or queued -- it fails with `ERR_MEM`, which arrives as
`MQTT_ERR_FAILED` with nothing on the wire to explain it.
`components/wifi/include/lwipopts.h` raises it for this reason, so a
regression there shows up here and nowhere else.

```text
mqtt> discovery pico-framework/test/config
464 bytes
ok
```

The board's own subscription to that topic will count the message in
`dropped` rather than printing it -- `MQTT_MAX_MESSAGE_LENGTH` is 256, and the
component refuses to hand a caller a truncated payload. That is the expected
result, and it doubles as proof the document really did exceed 256 bytes.
To see the payload itself, watch the topic from a subscriber elsewhere.

## Expected result

| Step | Expect |
|------|--------|
| first boot, nothing set | `radio present`, `network present`, both `idle` |
| `mqttconnect` with no broker set | `set a broker first` |
| `connect`, `mqttconnect` | `connecting` for both, then `connected` within a few seconds |
| `sub <topic>`, then `pub` the same topic | the message prints back within about a second |
| `unsub`, then `pub` the same topic again | nothing prints |
| `mqttstatus` | `dropped` stays 0 for ordinary short messages |
| power cycle with settings saved | both reconnect with no console involved |
| `drop` while connected | `connected, session N+1`, then a `resubscribe` line per standing subscription |
| `discovery <topic>` while connected | a byte count over 256, then `ok` |
| `sub` that topic first, then `discovery` | `dropped` increments by one; the payload does not print |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `radio none on this board` / `this board has no network stack` | not a W board |
| `mqttconnect` reports `set a broker first` | `broker` was never set |
| stuck in `resolving` | DNS is not answering -- check the WiFi link is actually up, not just associated |
| stuck in `connecting`, then `waiting to retry` | wrong port, or the broker refuses this client id / anonymous auth |
| connects, but a published message never comes back | subscribed to a different topic than published, or `unsub` was called since |
| nothing ever happens, no state changes at all | `wifi_poll()` or `mqtt_poll()` is not being called every loop iteration |
| `*** PANIC ***` mentioning `MEMP_SYS_TIMEOUT` | see the mqtt component's README -- fixed by an `lwipopts.h` change, should not reproduce on a build after 2026-09-03 |
| `discovery` reports `failed` while `mqttstatus` says connected | `MQTT_OUTPUT_RINGBUF_SIZE` is back at lwIP's 256-byte default -- check `components/wifi/include/lwipopts.h` |
| after `drop`, no `resubscribe` lines | `on_connect` is not wired into `mqtt_config_t` |
| after `drop`, `resubscribe` prints but messages still do not arrive | the broker refused the subscription; check the QoS it returned |

## Status

Validated on a Pico 2 W against `broker.hivemq.com`, 2026-09-03: `on_connect`
firing on the first accepted session, `discovery` publishing 464 bytes and an
independent subscriber receiving all 464 intact, the board's own subscription
counting that same message in `dropped` instead of truncating it, and `drop`
producing a fresh session whose automatic re-subscription then carried a
message published from off-board through to `on_message`.

Validated on a Pico 2 W against `test.mosquitto.org`, 2026-09-03: connect from
a cold boot and from stored settings after reflash, publish reaching the
broker (cross-checked with an independent `paho-mqtt` subscriber), an
externally published message arriving and printing through `on_message`,
`unsub` stopping delivery, and `mqtt_messages_dropped()` staying at 0
throughout. Two bugs were found and fixed during this session: a missing
`MEMP_NUM_SYS_TIMEOUT` allowance for lwIP's MQTT timer (a hard panic on the
first connect attempt) and a missing `mqtt_set_inpub_callback()` registration
on the very first connection (incoming messages silently discarded, nothing
in the console to suggest why). Both are described in the component's
README.
