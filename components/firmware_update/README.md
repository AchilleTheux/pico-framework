# firmware_update

The on-flash description of a firmware image, and the rule for deciding what to
boot.

This is the part of an over-the-wire update that is most expensive to get wrong
and cheapest to test, so it exists before any flash code does. Everything in it
is pure and verified on the host.

## The problem it solves

An update over a serial link can be interrupted at any point: the cable is
pulled, the battery sags, the sender crashes halfway. Every field here exists so
that a half-written image is recognisably half-written rather than something the
board tries to run.

## Image header

28 bytes, written to flash verbatim, so the layout is a contract between
whatever builds an image and the bootloader that reads one:

| Offset | Field | Purpose |
|--------|-------|---------|
| 0 | `magic` | `"PFW1"`. Neither erased flash (`0xFFFFFFFF`) nor blank flash (`0x00000000`) can be mistaken for it |
| 4 | `header_version` | refuse a layout we do not know rather than misread it |
| 6 | `header_size` | |
| 8 | `payload_size` | |
| 12 | `payload_crc32` | over the payload, from the `crc` component |
| 16 | `load_address` | |
| 20 | `build_id` | identifies this build; see below |
| 24 | `header_crc32` | over everything above |

Two checksums, not one, because they are checked at different times: the header
is 28 bytes and is validated constantly, while the payload is tens of kilobytes
and is streamed. A torn header can be told from a good one without reading the
payload at all.

## The boot decision

```c
switch (firmware_image_decide_boot(&application, &staged)) {
    case FIRMWARE_BOOT_RUN_APPLICATION: /* jump to it */
    case FIRMWARE_BOOT_INSTALL_STAGED:  /* copy staging across, then run */
    case FIRMWARE_BOOT_RECOVERY:        /* wait for an upload */
}
```

Pure by design: it takes two headers and returns what to do, so the rule is
tested exhaustively over every combination of slot states without a flash chip.
The bootloader reads the headers, calls this, and acts.

Three properties are deliberate:

* **A staged image is installed whenever its `build_id` *differs*, not only
  when it is greater.** Flashing a known-good older build back in the field has
  to work without a cable.
* **The decision clears itself.** Installing copies the header too, so
  afterwards the build ids agree and the next boot runs the application rather
  than installing again.
* **Interrupting an install is safe.** Staging is untouched by the copy, so the
  same decision is reached on the next boot and the copy simply restarts.

An invalid staged image never displaces a working application, and a valid
staged image is installed even over a ruined one — which is the recovery path
that matters.

## Status

The pure half is done and tested. The flash-backed half — staging a received
image and installing it — is not written yet and will live beside it in this
component. Until then nothing here touches flash.

## Testing

`make test` covers the layout, both checksums, and the boot rule. The cases
that earn their place are the ones an interrupted update actually produces:
erased flash, zeroed flash, a header with any single bit flipped anywhere in
its covered range, a truncated payload, and every combination of the two slots'
states.

One known gap, stated rather than hidden: removing the `memset()` in
`firmware_image_header_init()` does not fail any test, because the struct
happens to have no padding today. A test pins that property, and the comment on
the `memset` explains that it guards a future field rather than a present bug.
