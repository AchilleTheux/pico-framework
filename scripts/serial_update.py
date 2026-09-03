#!/usr/bin/env python3
"""Send a firmware image to a board over a serial port.

Drives the exchange that firmware_service implements on the device:

    fwbegin <size>      erase staging
    :...                the Intel HEX records, streamed
    fwverify <crc32>    check what landed against what was sent
    fwapply             install it and reboot          (only with --apply)

The checksum is the part worth being careful about. The device computes it by
reading back the flash it wrote, so this has to predict exactly what will be
there: records placed at their offsets, gaps left as 0xFF because that is what
erased flash reads as, and the span running from offset 0 to the highest byte
written. Anything else and a perfectly good transfer fails verification.

Uses no third-party modules: the port is an ordinary file, configured with
stty by the shell wrapper.
"""

import argparse
import os
import select
import sys
import time
import zlib

# An image is linked to run from the start of flash, so its HEX records carry
# XIP addresses. Offset 0 of the image corresponds to this.
IMAGE_BASE = 0x10000000

ERASED = 0xFF


class HexError(Exception):
    pass


def parse_hex(path):
    """Return the image bytes, laid out exactly as the device will store them.

    Gaps are 0xFF, and the result runs from offset 0 to the highest byte in the
    file — which is what the device checksums.
    """
    chunks = {}
    base = 0
    highest = -1
    seen_eof = False

    with open(path, "r", encoding="ascii") as handle:
        for number, raw in enumerate(handle, start=1):
            line = raw.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise HexError(f"{path}:{number}: not a hex record")
            if seen_eof:
                raise HexError(f"{path}:{number}: record after end of file")

            try:
                body = bytes.fromhex(line[1:])
            except ValueError as exc:
                raise HexError(f"{path}:{number}: {exc}") from exc

            if len(body) < 5:
                raise HexError(f"{path}:{number}: record too short")
            if sum(body) & 0xFF:
                raise HexError(f"{path}:{number}: bad checksum")

            count, hi, lo, kind = body[0], body[1], body[2], body[3]
            data = body[4:-1]
            if len(data) != count:
                raise HexError(f"{path}:{number}: length disagrees with byte count")

            if kind == 0x00:
                address = base + ((hi << 8) | lo)
                if address < IMAGE_BASE:
                    raise HexError(
                        f"{path}:{number}: address 0x{address:08X} is below the "
                        f"image base 0x{IMAGE_BASE:08X}")
                offset = address - IMAGE_BASE
                for index, value in enumerate(data):
                    chunks[offset + index] = value
                highest = max(highest, offset + len(data) - 1)
            elif kind == 0x01:
                seen_eof = True
            elif kind == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            elif kind == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif kind in (0x03, 0x05):
                pass  # start address; carries no image data
            else:
                raise HexError(f"{path}:{number}: unknown record type {kind:#04x}")

    if not seen_eof:
        raise HexError(f"{path}: no end-of-file record; the file is truncated")
    if highest < 0:
        raise HexError(f"{path}: no data records")

    image = bytearray([ERASED]) * (highest + 1)
    for offset, value in chunks.items():
        image[offset] = value
    return bytes(image)


class Port:
    """The serial port, as a pair of file descriptors with a line reader."""

    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        self.pending = b""

    def close(self):
        os.close(self.fd)

    def write(self, text):
        data = text.encode("ascii")
        while data:
            data = data[os.write(self.fd, data):]

    def read_line(self, timeout):
        """One line, or None on timeout. Trailing CR/LF removed."""
        deadline = time.monotonic() + timeout
        while True:
            newline = self.pending.find(b"\n")
            if newline >= 0:
                line = self.pending[:newline]
                self.pending = self.pending[newline + 1:]
                return line.decode("ascii", "replace").rstrip("\r")

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            if not select.select([self.fd], [], [], remaining)[0]:
                return None
            block = os.read(self.fd, 4096)
            if block:
                self.pending += block

    def drain(self, seconds=0.2):
        """Discard whatever is already buffered, so a reply cannot be mistaken
        for an answer to the next command."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if select.select([self.fd], [], [], 0.02)[0]:
                os.read(self.fd, 4096)
        self.pending = b""

    def command(self, text, timeout, expect=None, verbose=False):
        """Send a command and collect reply lines until the prompt returns.

        Returns the lines. When `expect` is given and no line contains it, the
        exchange is treated as a failure.
        """
        self.write(text + "\r\n")

        lines = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(min(1.0, max(0.05, deadline - time.monotonic())))
            if line is None:
                continue
            stripped = line.strip()
            if verbose and stripped:
                print(f"    {stripped}")
            if stripped:
                lines.append(stripped)
            # The prompt marks the end of the reply. It arrives without a
            # newline, so it shows up as a prefix of the next read instead.
            if expect is not None and any(expect in item for item in lines):
                return lines
            if expect is None and lines:
                # Give a moment for any further lines, then stop.
                extra = self.read_line(0.15)
                while extra is not None:
                    if extra.strip():
                        lines.append(extra.strip())
                        if verbose:
                            print(f"    {extra.strip()}")
                    extra = self.read_line(0.15)
                return lines

        return lines


def stream_records(port, path, quiet, record_delay):
    """Send every record, reporting progress on one rewritten line.

    record_delay paces the stream rather than firing records back-to-back.
    The reason: every 256 bytes staged, the device does a blocking flash page
    program (interrupts off, the other core parked — flash cannot be read
    for code or data while it is being written to). The CLI's UART reader is
    plain-polled, so all that is left to absorb bytes arriving during that
    stall is the UART's 32-byte hardware FIFO — about 2.8 ms at 115200 baud.

    None of which applies over USB CDC, where the board is the USB device: its
    endpoint NAKs when it cannot accept more and the host retries, so a page
    flush stalls the transfer without losing anything. flash-serial.sh reads
    that off the port name and passes 0 for a /dev/ttyACM* port, which is
    about eleven times faster.
    Sent flat out, this driver used to outrun that comfortably: a page-flush
    stall drops a byte, which corrupts that one HEX record's checksum, which
    the device treats as a fatal transfer error — and every record after
    that point is then rejected too ("no transfer in progress"), silently,
    since replies are drained here without being read. One dropped byte
    partway through a transfer this way used to cost everything sent after
    it, surfacing only as a CRC mismatch at fwverify, with no clue where or
    why.
    """
    with open(path, "r", encoding="ascii") as handle:
        lines = [line.strip() for line in handle if line.strip()]

    total = len(lines)
    started = time.monotonic()

    for index, line in enumerate(lines, start=1):
        port.write(line + "\r\n")
        if record_delay > 0:
            time.sleep(record_delay)

        # The device answers only occasionally, and anything it does say is
        # drained here so its buffer cannot fill and stall the transfer.
        while port.read_line(0) is not None:
            pass

        if not quiet and (index % 200 == 0 or index == total):
            elapsed = time.monotonic() - started
            rate = index / elapsed if elapsed > 0 else 0
            print(f"\r  sent {index}/{total} records ({100 * index // total}%), "
                  f"{rate:.0f}/s", end="", flush=True)

    if not quiet:
        print()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="the .hex file to send")
    parser.add_argument("port", help="serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--apply", action="store_true",
                        help="install the image after verifying it (overwrites "
                             "the running firmware)")
    parser.add_argument("--erase-timeout", type=float, default=30.0,
                        help="seconds to allow for the staging erase")
    parser.add_argument("--record-delay", type=float, default=0.005,
                        help="seconds to pause after each record, so a page "
                             "flush's blocking flash write cannot overrun the "
                             "UART's hardware FIFO and silently drop a byte "
                             "(default 5 ms; 4 ms was the measured minimum on "
                             "the reference board — see stream_records()). "
                             "0 over USB CDC, which cannot drop a byte; "
                             "flash-serial.sh picks that from the port name")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    try:
        image = parse_hex(args.image)
    except HexError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    crc = zlib.crc32(image) & 0xFFFFFFFF
    print(f"image {len(image)} bytes, crc32 0x{crc:08X}")

    try:
        port = Port(args.port)
    except OSError as exc:
        print(f"error: cannot open {args.port}: {exc}", file=sys.stderr)
        return 1

    try:
        port.drain()

        print("starting transfer")
        reply = port.command(f"fwbegin {len(image)}", args.erase_timeout,
                             expect="ready", verbose=not args.quiet)
        if not any("ready" in line for line in reply):
            print("error: the board did not report ready.", file=sys.stderr)
            if reply:
                print("  it said: " + "; ".join(reply), file=sys.stderr)
            else:
                print("  it said nothing. Is this firmware built with the "
                      "firmware update service?", file=sys.stderr)
            return 1

        stream_records(port, args.image, args.quiet, args.record_delay)

        port.drain()
        print("verifying")
        reply = port.command(f"fwverify {crc:08X}", 20.0, verbose=not args.quiet)
        if not any("verified" in line for line in reply):
            print("error: verification failed.", file=sys.stderr)
            for line in reply:
                print(f"  {line}", file=sys.stderr)
            return 1

        if not args.apply:
            print("staged and verified. Send fwapply, or re-run with --apply, "
                  "to install it.")
            return 0

        print("installing")
        reply = port.command("fwapply", 10.0, verbose=not args.quiet)
        if any("refusing" in line or "error" in line for line in reply):
            print("error: the board refused to install.", file=sys.stderr)
            for line in reply:
                print(f"  {line}", file=sys.stderr)
            return 1

        print("the board is installing and will reboot; it will be quiet for a "
              "few seconds.")
        return 0

    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
