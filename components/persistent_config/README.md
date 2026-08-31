# persistent_config

Key/value settings that survive power-off — and, more to the point, survive
being interrupted while they are being written.

## The property worth having

> **A save can be interrupted at any point without losing the previous
> configuration.**

That is what the two slots are for. Each save erases and rewrites the slot that
is *not* current, then a higher sequence number in its header makes it current.
Until that last write lands, the old slot is untouched and still the one that
loads — so a battery that sags mid-save costs the change and nothing else.

Within a save, records are written before the header. Until the header lands the
slot has no valid magic, so an interruption leaves it unreadable rather than
readable and wrong.

Alternating also halves the wear on either sector, which matters for a part
rated around 100,000 erase cycles if anything ever saves in a loop.

```text
+----------------+  data region
|  slot A        |  one sector
+----------------+
|  slot B        |  one sector
+----------------+
|  the rest      |  for logs
+----------------+
```

## Usage

```c
static uint8_t buffer[4096 - 256];       /* caller-owned working copy */
static persistent_config_t config;

persistent_config_load(&config, buffer, sizeof(buffer));

config_store_t *store = persistent_config_store(&config);
config_set_string(store, "wifi_ssid", "robot-net");
config_set_u32(store, "servo_baud", 1000000);

persistent_config_save(&config);
```

Reading, with a default for when nothing is stored:

```c
char ssid[33];
config_get_string(store, "wifi_ssid", ssid, sizeof(ssid), "fallback-net");

uint32_t baud;
config_get_u32(store, "servo_baud", &baud, 1000000);
```

`load()` distinguishes **nothing saved yet** (`ERR_EMPTY`, normal on a new
board) from **both slots damaged** (`ERR_CORRUPT`, something went wrong). Both
leave an empty usable store, so a caller that only wants defaults can ignore the
difference — but only one of them is worth logging.

`save()` deliberately does not check whether anything changed. A caller that
saves every loop iteration will wear the flash out, and hiding that behind a
dirty flag would make it harder to notice rather than less likely.

## Format

All of it, and every operation on it, is in `config_store.c` — no SDK
dependency, so it is host-tested. Records sit in one buffer:

```text
[key length][value length][key bytes][value bytes] ...
```

The buffer is a valid image of the whole set at every moment, which is what lets
a save be one write. Setting a key therefore shuffles the buffer when the value
changes length. An append-only log would shuffle less but would make every read
a scan and every recovery a replay, and there is no shortage of time here — a
configuration is saved when something changes, not in a loop.

Keys are up to 31 characters, values up to 255 bytes. A `u32` is stored
little-endian explicitly rather than by copying the native type, so a value
written by one build reads the same in another.

## Two choices worth knowing

**A string too long for the caller's buffer falls back rather than truncating.**
Half a WiFi password is worse than the default, because it looks like a value.
The true length is still reported, so a caller can size a buffer and retry.

**Reading a value at the wrong width reports it rather than reassembling.**
`config_get_u32()` on a key holding a string returns `ERR_CORRUPT`, whether the
stored value is longer or shorter than four bytes — the same mistake either way.

## Secrets

This is the mechanism DESIGN_DOC.md section 13 asks for. WiFi credentials
belong here, set once over the console, rather than in a board header or a
profile that gets committed.

## Status

**Untested on hardware.** The format and every operation on it are host-tested;
the two-slot behaviour on real flash is not, and the thing it exists for —
surviving a power loss mid-save — needs someone to pull the power.

## Testing

* Host: `make test` covers the format, resizing values in place, filling the
  buffer exactly, and rejecting a truncated image — which is what an interrupted
  save leaves behind.
* Hardware: `make APP=tests/config_test flash` — set, save, power-cycle, check.
  Its `churn` command saves repeatedly and verifies the slot alternates.
