# config_test

Bench for the `persistent_config` component.

## Required hardware

Any RP2040 or RP2350 board. No wiring.

## The test that matters

Set something, save it, **pull the power**, and check it comes back. That is the
one thing a host test cannot do.

```text
cfg> set wifi_ssid robot-net
cfg> set servo_baud 1000000
cfg> save
saved to slot 1, sequence 1
   ... unplug the board, plug it back in ...
cfg> list
  wifi_ssid            robot-net
  servo_baud           1000000
```

The banner reports what `load()` found at startup, which is the same thing.

## Commands

```text
cfg> set <key> <value>     in the working copy only
cfg> get <key>
cfg> unset <key>
cfg> list                  everything, with bytes used
cfg> save                  write to flash
cfg> load                  re-read from flash, discarding unsaved changes
cfg> wipe                  erase both slots
cfg> cfginfo               slot addresses, current slot, sequence
cfg> churn 20              save 20 times and check the slot alternates
```

`set` changes only the working copy — `save` is what reaches flash. That is
deliberate: it means `load` can discard unsaved changes, which is a useful thing
to be able to do.

## Expected result

| Step | Expect |
|------|--------|
| first boot on a new board | `load: nothing saved yet` |
| `set` then `list` | the value, and a byte count that grew |
| `save` | `saved to slot N, sequence M`, with M one higher each time |
| power cycle, then `list` | the same values |
| `save` twice | the slot number alternates between 0 and 1 |
| `churn 20` | `20 saves, 20 alternated, 0 failed` and `alternation correct` |
| `wipe`, then `load` | `nothing saved yet` |

## Testing the interruption

The property the two slots exist for is that a save can be interrupted without
losing the previous configuration. To provoke it:

1. `save` a known-good configuration.
2. `set` something different.
3. Start `save` and cut the power **during** it — a second or so.
4. Power up and `list`.

The result should be the configuration from step 1, not a mixture and not an
empty store. This is worth trying a few times, since the window is short.

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `nothing saved yet` after a save and power cycle | the save reported success but did not land — check `cfginfo` for a plausible slot address |
| `saved data is damaged` | both slots failed their checks. Something else is writing the data region |
| sequence resets to 1 | the load is not finding the slot the save wrote |
| `alternation WRONG` from `churn` | saves are going to the same slot, so an interrupted save would destroy the only copy |
| `no room for two slots` | the flash layout's data region is under two sectors — a very small chip |
| a mixture of old and new values after an interrupted save | the property this component is built around is broken; worth reporting in detail |
