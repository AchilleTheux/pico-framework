# json

Reading and writing small JSON documents, without allocating. Applications
link `pico_framework::json` and include `json.h`.

[`mqtt`](../mqtt/) moves opaque bytes, which is the right shape for a
transport — but almost everything on the far side of a broker speaks JSON.
This is the layer between them, in the same relationship
[`hex_parser`](../hex_parser/) has with the serial console: a format, decoded
where the transport ends.

## Scope

Deliberately not a general JSON library. There is no document object, no
allocation, and no round-tripping. Reading is a scan over a buffer the caller
already holds; writing appends text to a buffer the caller already sized. That
covers *pull four fields out of a command message* and *build a status
document*, which is the whole of what firmware here does with JSON, in a
couple of hundred bytes of stack and no heap at all.

## Why not `strstr()`

The obvious shortcut is to search the payload for `"brightness"` and read the
number after it. It goes wrong quietly in four ways, all of which occur in
real payloads:

```text
{"effect":"brightness test"}    the key appears inside a value
{"color":{"r":10}}              a nested key matches at the top level
{"brightness_scale":100}        one key is a prefix of another
{"brightness":42               a truncated message still yields a field
```

`json_find()` looks only at the keys of the object it is given, at that
object's own level, and matches them whole. Nested objects are stepped over
rather than searched — to read `color.r`, find `color` and then search the
value it hands back. And the object is checked all the way to its closing
brace before any member is returned, so a message cut short in transit yields
nothing instead of the fields that happened to arrive before the cut.

Each of those four is a test in `tests/components/json_test.c`, in the group
named for them. They are the reason this component exists.

## Reading

```c
json_value_t value;
json_value_t colour;
int32_t brightness;

if (json_find(payload, "state", &value) && json_string_equals(&value, "ON")) {
    light_on();
}
if (json_find(payload, "brightness", &value) && json_get_int(&value, &brightness)) {
    light_set_brightness((uint8_t)brightness);
}
if (json_find(payload, "color", &colour)) {
    int32_t rgb[3];
    if (json_find_in(&colour, "r", &value) && json_get_int(&value, &rgb[0])) {
        /* ... */
    }
}
```

A `json_value_t` points into the caller's buffer and copies nothing, so it is
valid exactly as long as that buffer is. Inside an `mqtt` `on_message`
callback the payload is valid for the duration of the call and no longer;
anything that must outlive it has to be copied out with `json_get_string()` or
converted with `json_get_int()`.

`json_find_n()` takes a pointer and a length rather than requiring a
terminator, which is the shape an MQTT payload actually arrives in.

Conversions refuse rather than guess. A number too large for an `int32_t` is
rejected instead of wrapped, an exponent is rejected instead of rescaled, a
string too long for the destination is rejected instead of truncated, and
`1` is not a boolean. A wrapped brightness or a half-copied effect name is
worse than a missing one, because it looks like it worked.

## Writing

```c
char payload[1024];
json_writer_t writer;

json_writer_init(&writer, payload, sizeof(payload));
json_writer_object_open(&writer, NULL);
json_writer_string(&writer, "state", on ? "ON" : "OFF");
json_writer_int(&writer, "brightness", brightness);
json_writer_array_open(&writer, "rgb_color");
json_writer_int(&writer, NULL, r);
json_writer_int(&writer, NULL, g);
json_writer_int(&writer, NULL, b);
json_writer_array_close(&writer);
json_writer_object_close(&writer);

if (json_writer_finish(&writer)) {
    mqtt_publish_message(&mqtt, topic, payload, (uint16_t)json_writer_length(&writer), 1, true);
}
```

No call returns an error. A write that does not fit sets a sticky flag and is
dropped; `json_writer_finish()` reports once, at the end, whether everything
fit and every container was closed. A twenty-field discovery document built as
twenty ignored return values is how truncation gets missed in practice, so
there is one check instead of twenty.

`json_writer_raw()` inserts already-formatted JSON verbatim, for a fragment
that is a constant or was built elsewhere. Nothing validates it.

## Buffer sizing, and lwIP

A Home Assistant discovery document for one light runs to several hundred
bytes — the one `apps/tests/mqtt_test` builds measures 464. lwIP defaults
`MQTT_OUTPUT_RINGBUF_SIZE` to 256, and a publish larger than the ring buffer
is not split or queued — it fails with `ERR_MEM`, reaching the caller as
`MQTT_ERR_FAILED` with nothing on the wire to explain it.
`components/wifi/include/lwipopts.h` raises it to 1 KiB for exactly this
reason. See the comment there before sending anything larger.

## Testing

Pure string and integer work with no SDK dependency, so `json.c` is compiled
directly into the host tests and the whole component is verified without
hardware. `make test` covers the four `strstr` traps above, escaped strings
and structural characters inside values, `\uXXXX` inside and outside ASCII,
nesting past `JSON_MAX_DEPTH`, the `int32_t` extremes and what lies past them,
writer overflow and imbalance, and a full-sized Home Assistant discovery
document built and then read back with the same component.

There is no hardware bench: there is nothing here for hardware to tell us that
the host cannot. The component reaches real payloads through
[`mqtt`](../mqtt/), whose bench exercises the transport underneath it.
