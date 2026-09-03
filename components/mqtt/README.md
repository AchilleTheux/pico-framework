# mqtt

A thin, poll-driven wrapper around lwIP's native MQTT client.

Sits where DESIGN_DOC.md section 17's WiFi note says higher-level networking
belongs: separate from connection management. [`wifi`](../wifi/) gets a link
and an address; this component gets a broker session on top of it, the same
relationship [`ax12`](../ax12/) has with [`half_duplex_uart`](../half_duplex_uart/)
for a transport it does not implement itself (section 7).

## Non-blocking, like everything else here

```c
mqtt_t mqtt;
mqtt_init(&mqtt);

const mqtt_config_t config = {
    .broker_host = "test.mosquitto.org", .broker_port = 1883,
    .client_id = "pico-1",
    .on_message = handle_message, .on_message_arg = &my_state,
    .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
};
mqtt_connect(&mqtt, &config);          /* returns immediately */

while (true) {
    wifi_poll(&wifi);                  /* someone has to be polling the link */
    mqtt_poll(&mqtt);                  /* drives this component's own retry */
    /* ... the control loop ... */
}
```

`mqtt_poll()` does not touch the radio or lwIP's core processing itself --
that already happens as a side effect of whatever polls the link, `wifi_poll()`
in this framework. What `mqtt_poll()` drives is this component's own
reconnect timer. Calling `mqtt_connect()` before the link is up is not a
special case: the DNS lookup or TCP connect just fails and the retry loop
--- described below --- starts immediately, the same as any later drop.

## Reconnection reuses wifi's backoff, not a copy of it

A dropped broker session is retried on `wifi_retry_t`
([`wifi_policy.h`](../wifi/include/wifi_policy.h)) rather than a second
implementation of "wait longer after each failure, cap it, survive the
millisecond counter's wrap" -- that is exactly the same decision for a broker
as for an access point, already written and already host-tested. This
component depends on `wifi` for the network stack in any case, so reusing its
retry policy costs nothing extra and avoids maintaining two backoff schedules
that are supposed to behave identically.

## Clean sessions, and why on_connect is not optional

lwIP always sends CONNECT with the clean-session flag set, and there is no way
to ask it not to. The broker therefore discards this client's subscriptions
the moment the session drops. After a reconnect the topics are simply gone:
messages stop arriving, `mqtt_is_connected()` reports true throughout, and
nothing anywhere reports an error. It looks exactly like a broker that stopped
publishing.

`mqtt_config_t::on_connect` is called once for every accepted session -- the
first one and every reconnection -- which is where re-subscribing belongs:

```c
static void on_connect(void *arg)
{
    my_app_t *app = arg;

    mqtt_subscribe_topic(&app->mqtt, "homeassistant/light/pico1/set", 1);
    publish_discovery(app);              /* retained, so it must be resent */
    mqtt_publish_message(&app->mqtt, availability_topic, "online", 6, 1, true);
}
```

Anything a device announces about itself belongs here for the same reason. It
runs from lwIP's connection callback with the session already up, so
`mqtt_subscribe_topic()` and `mqtt_publish_message()` both work from inside it;
the usual callback discipline applies, so do the small thing and leave the long
one to the main loop.

`mqtt_sessions()` counts accepted sessions if you would rather notice the
change by polling than by callback.

## Naming

`mqtt.c` includes lwIP's own `lwip/apps/mqtt.h`, which already defines
`mqtt_publish()`, `mqtt_disconnect()` as real functions and `mqtt_subscribe()`
/ `mqtt_unsubscribe()` as macros. This component's equivalents are
`mqtt_publish_message()`, `mqtt_close()`, `mqtt_subscribe_topic()` and
`mqtt_unsubscribe_topic()` instead of colliding with them -- the subscribe
macros in particular would not just shadow a symbol, they would mangle this
header's own declarations wherever both are visible in the same translation
unit.

## Topic and client-id validation

lwIP's own `mqtt_publish()` / `mqtt_sub_unsub()` send whatever string they are
given. A `'#'` in a publish topic, or a `'+'` sharing a level with other text
in a subscribe filter, is not rejected there -- it either does nothing useful
at the broker or subscribes to more or less than intended. `mqtt_policy.c`
checks both before anything is sent, free of lwIP and the Pico SDK, so it is
host-tested (DESIGN_DOC.md section 19) rather than caught by a broker doing
something unexpected.

## Reassembling incoming publishes

lwIP splits an incoming publish into a topic-arrival callback and one or more
data-arrival callbacks, because a publish can be larger than it wants to
buffer. This component hides that split behind one `mqtt_message_cb_t`,
buffering up to `MQTT_MAX_MESSAGE_LENGTH` bytes (256 by default) per message.
A message that arrives longer than that is not delivered truncated --
truncating silently is worse than not delivering at all when a caller expects
the whole payload -- it is dropped and counted in `mqtt_messages_dropped()`
instead.

## Boards without a radio

This component compiles for every board, the same as `wifi`: `MQTT_SUPPORTED`
tracks `WIFI_SUPPORTED`, and without a radio every call returns
`MQTT_ERR_UNSUPPORTED`.

## MEMP_NUM_SYS_TIMEOUT -- found on hardware, not in review

lwIP's MQTT client keeps a cyclic timer running via `sys_timeout()` for as
long as a session is open. lwIP's own default sizing for that timer pool
(`LWIP_NUM_SYS_TIMEOUT_INTERNAL`) has no idea the MQTT app exists -- it counts
only TCP, ARP, DHCP, DNS and similar core timers. lwIP's own documentation
says as much (`lib/pico-sdk/lib/lwip/doc/mqtt_client.txt`):

> You need to increase MEMP_NUM_SYS_TIMEOUT by one if you use MQTT!

`wifi`'s shared [`lwipopts.h`](../wifi/include/lwipopts.h) --- the one this
component also builds against, since it depends on `wifi` for the network
stack --- predates this component and never accounted for it. The result,
reproduced on a Pico 2 W running `mqtt_test`: `mqttconnect` panicked outright
the first time a broker connection was attempted --

```text
*** PANIC ***
sys_timeout: timeout != NULL, pool MEMP_SYS_TIMEOUT is empty
```

`lwipopts.h` now sets `MEMP_NUM_SYS_TIMEOUT` explicitly to the default for
this configuration plus one. See that file for the exact arithmetic and why
it is a literal rather than computed from `LWIP_NUM_SYS_TIMEOUT_INTERNAL`
(that macro is not yet defined at the point `lwipopts.h` is read). A second
concurrent `mqtt_t` connection needs one more.

## What this cannot tell you

Whether the broker itself is configured correctly -- ACLs, TLS, retained
messages beyond what this component sets on its own publishes. This gets a
session and moves bytes; broker-side policy is out of scope, same spirit as
`wifi` stopping at "there is a working link."

**No TLS.** Only plain `mqtt_client_connect()`, matching what `wifi`'s
`lwipopts.h` builds (`LWIP_ALTCP` is off). Adding it is a `pico_lwip_mbedtls`
dependency and an `altcp_tls_config`, not a change to this component's shape.

## Testing

* Host: `make test` covers publish-topic and subscribe-filter validation
  (including wildcard placement), client-id limits, and QoS range checking.
* Hardware: `make BOARD=pico2_w APP=tests/mqtt_test flash`, then set
  `ssid`/`password`/`broker`, `save`, `connect`, `mqttconnect`. Validated on a
  Pico 2 W against `test.mosquitto.org`, 2026-09-03:
  * `mqttconnect` reaches `[mqtt] connected` from a cold boot and from stored
    settings after a power cycle.
  * `pub` reaches the broker -- confirmed with an independent subscriber
    (`paho-mqtt`) watching the same topic.
  * A message published from outside arrives at the board and is printed
    through `on_message` -- this exposed the `mqtt_set_inpub_callback()` bug
    below before it was fixed.
  * `unsub` stops delivery to that topic; a message published to it
    afterwards does not arrive.
  * `mqtt_messages_dropped()` stayed at 0 throughout.
* `on_connect` and large publishes were validated on the same board against
  `broker.hivemq.com`, 2026-09-03:
  * `on_connect` fires on the first accepted session, ahead of the main loop
    noticing the state change.
  * `discovery` builds a 464-byte document with [`json`](../json/) and
    publishes it; an independent subscriber received all 464 bytes intact.
    That is past lwIP's 256-byte default `MQTT_OUTPUT_RINGBUF_SIZE`, so it
    also confirms the override in `wifi`'s `lwipopts.h` -- without it the
    publish fails with `ERR_MEM` and nothing reaches the wire.
  * The board's own subscription to that topic counted the 464-byte message in
    `mqtt_messages_dropped()` rather than delivering it truncated, which is
    `MQTT_MAX_MESSAGE_LENGTH` behaving as documented and independent
    confirmation that the payload really did exceed 256 bytes end to end.
  * `drop` closes and reopens the session: `on_connect` fires again for
    session 3 and replays the standing subscription, and a message published
    from off-board afterwards arrives at `on_message`. Without the callback
    that subscription would have been silently lost -- the failure this whole
    section is about.

### mqtt_set_inpub_callback() on the very first connect -- also found on hardware

`start_connect()` creates the lwIP client lazily on first use.
`mqtt_set_inpub_callback()` was only being called from `mqtt_connect()`,
guarded by `mqtt->client != NULL` -- which is exactly backwards for the first
connection: the client does not exist yet at that point, so the guard skipped
registering the callback and every incoming publish was silently discarded by
lwIP with no error anywhere. `pub` and `mqttstatus` looked completely normal
throughout; only an independent publish from off-board, arriving nowhere,
gave it away. The callback is now registered once, in `start_connect()`,
right after the client is created -- covering both the first connection and
every reconnect that reuses it -- and `mqtt_connect()` keeps its own
registration for the case where a caller reconnects with a new `on_message`
on a client that already exists.
