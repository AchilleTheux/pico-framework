# logging

Levelled logging to one or more sinks — what DESIGN_DOC.md section 20 asks for.

```c
#define LOG_TAG "servo"        /* before the include; optional */
#include "log.h"

LOG_INFO("bus at %lu baud", baud);
LOG_WARN("timeout on id %u", id);
```

```text
[    12.345] W servo: timeout on id 3
 |            | |      |
 |            | |      the message
 |            | the tag, usually the component
 |            the level, one letter
 seconds since boot, to the millisecond
```

The timestamp is right-aligned so lines stay in columns for the first eleven
hours of running.

## Two filters, and the difference matters

| | |
|---|---|
| **Compile-time** | `LOG_COMPILE_LEVEL` removes calls below it entirely — no code, and **no format string left in flash** |
| **Runtime** | `log_set_level()` filters what survived compilation, and each sink has its own threshold on top |

The compile-time one is the interesting half on a part where strings are a real
cost. Verified rather than asserted: with `LOG_COMPILE_LEVEL=LOG_NONE_LEVEL`,
`strings` finds zero occurrences of a message that is present once in the
default build. A release build pays nothing for the trace logging that made
development bearable.

```bash
make CMAKE_ARGS=-DLOG_COMPILE_LEVEL=LOG_WARN_LEVEL
```

The level constants for `#if` are `LOG_TRACE_LEVEL` … `LOG_NONE_LEVEL` and are
deliberately *not* the enum. `#if` runs before the enum exists, so an enum name
there evaluates to 0 and quietly compiles in everything — which is exactly what
happened on the first attempt, caught by `-Wundef`. A static assertion now ties
the two together so they cannot drift.

## Sinks

```c
log_init(LOG_LEVEL_INFO);
log_add_stdio_sink(LOG_LEVEL_INFO);
log_add_memory_sink(buffer, sizeof(buffer), LOG_LEVEL_DEBUG);
```

| | |
|---|---|
| `log_add_stdio_sink` | wherever `pico_enable_stdio_*` points |
| `log_add_memory_sink` | a ring buffer, read back with `log_read_memory()` |
| `log_add_sink` | anything else. The signature matches `cli_stream_t`'s write, so a CLI transport works as a sink with no adapter |

Note the two thresholds in that example: the console shows info and above, while
the memory sink keeps debug as well. That is the point of per-sink levels.

**The memory sink is worth having on a robot.** The interesting lines are the
ones just before something went wrong, and they are no use if nobody was
watching the console. When it fills, the *oldest* output is dropped rather than
the newest refused — a log that stops recording once full keeps the least
interesting lines.

## What it does not do

Nothing allocates. A message is formatted into a stack buffer of
`LOG_LINE_LENGTH` and truncated if it does not fit, rather than split: a line
arriving in two pieces interleaved with another is worse than one that is short.

There is no flash sink yet. The data region has room after the configuration
slots, and the reference firmware this framework draws on logged to flash so a
match could be reviewed afterwards — worth adding, and not written.

## Status

**Untested on hardware.** The levels and the line prefix are host-tested; the
sinks are not.

## Testing

`make test` covers the ordering, parsing a level from a name or a single letter,
and the prefix. The cases that earn their place are the truncation ones: this is
called from paths that are already going wrong, so writing past a stack buffer
while reporting an error would turn a reported problem into an unreported one.
Every capacity from 1 to 31 is checked for termination and for not touching a
byte beyond what was asked.
