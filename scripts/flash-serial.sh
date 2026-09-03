#!/usr/bin/env bash
#
# Send a firmware image to a board over a serial port.
#
# Usage: flash-serial.sh <image.hex> [port]
#
# This is a genuine serial upload, not a way to avoid the BOOTSEL button: the
# board needs no USB connection at all. The firmware on it receives the image
# over the same link that carries its console, writes it to a staging region of
# flash, checks it against the sender's checksum, and — if the install step was
# built in — copies it over itself and reboots.
#
# The firmware must have been built with the firmware update service, which the
# serial_update_test application demonstrates. A board running anything else
# will not answer fwbegin, and this reports that rather than hanging.
#
# By default the image is staged and verified but *not* installed, because the
# install is the only step that can leave a board unbootable. Pass --apply, or
# send fwapply yourself once you are satisfied with what fwstatus reports.
#
# The protocol itself is in serial_update.py, which also predicts the checksum
# the board will compute. That prediction is checked against the board's own
# implementation by check-hex-agreement.sh, on every CI run.

set -euo pipefail

IMAGE="${1:-}"
PORT="${2:-}"

# Whether to install after verifying. Off by default; see the header.
APPLY="${SERIAL_UPDATE_APPLY:-0}"

# Line rate. Ignored by a USB CDC port; a CLI on a real UART runs at whatever
# its profile configured.
BAUD="${SERIAL_UPDATE_BAUD:-115200}"

# Pause after each record. Chosen from the port below, once it is known;
# SERIAL_UPDATE_RECORD_DELAY overrides that choice.
RECORD_DELAY="${SERIAL_UPDATE_RECORD_DELAY:-}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER="$ROOT/scripts/serial_update.py"

die() { echo "error: $*" >&2; exit 1; }

[[ -n "$IMAGE" ]] || die "no image given"
[[ -f "$IMAGE" ]] || die "image not found: $IMAGE"
[[ "$IMAGE" == *.hex ]] || die "expected a .hex file, got: $IMAGE"
[[ -f "$DRIVER" ]] || die "missing $DRIVER"

command -v python3 >/dev/null 2>&1 || die "python3 not found in PATH"
command -v stty >/dev/null 2>&1 || die "stty not found in PATH"

# ---------------------------------------------------------------------------
# Choose a port
# ---------------------------------------------------------------------------

# Overridable so the selection logic can be exercised without real hardware.
SERIAL_PORT_GLOB="${SERIAL_PORT_GLOB:-/dev/ttyACM* /dev/ttyUSB*}"

list_ports() {
    # Nothing matching is not an error here, so the glob failing is fine.
    # shellcheck disable=SC2086
    ls $SERIAL_PORT_GLOB 2>/dev/null || true
}

if [[ -z "$PORT" ]]; then
    mapfile -t candidates < <(list_ports)

    case "${#candidates[@]}" in
        0) die "no serial port given and none found (looked for /dev/ttyACM* and /dev/ttyUSB*)" ;;
        1) PORT="${candidates[0]}"
           echo "using the only serial port found: $PORT" ;;
        *) echo "error: several serial ports found; name the one you want:" >&2
           printf '  %s\n' "${candidates[@]}" >&2
           echo "  e.g. make flash-serial ${candidates[0]}" >&2
           exit 1 ;;
    esac
fi

[[ -e "$PORT" ]] || die "no such serial port: $PORT"
[[ -w "$PORT" ]] || die "cannot write to $PORT (a dialout/uucp group membership is usually what is missing)"

# ---------------------------------------------------------------------------
# Pacing
# ---------------------------------------------------------------------------
# Records need pacing on a real UART and not over USB, and the port name says
# which this is. What matters is what sits on the *board* side of the link:
#
#   /dev/ttyACM*, /dev/cu.usbmodem*   the board is itself the USB device, so
#                                     its CDC endpoint NAKs when it cannot
#                                     accept more and the host retries. A
#                                     blocking flash write stalls the transfer
#                                     for a moment; it cannot lose a byte.
#
#   /dev/ttyUSB*, and anything else   a USB-to-serial bridge, so the board is
#                                     receiving on a real UART with a 32-byte
#                                     hardware FIFO and no flow control. A
#                                     page flush that outlasts the FIFO drops a
#                                     byte, which fails the whole transfer.
#                                     See stream_records() in serial_update.py.
#
# The difference is worth having: over USB CDC the paced default costs about
# eleven times the wall clock -- 197 records/s against 2200, two minutes
# against twelve seconds for a 380 KiB image.
if [[ -z "$RECORD_DELAY" ]]; then
    case "$PORT" in
        /dev/ttyACM*|/dev/cu.usbmodem*) RECORD_DELAY=0 ;;
        *)                              RECORD_DELAY=0.005 ;;
    esac
fi

# ---------------------------------------------------------------------------
# Send it
# ---------------------------------------------------------------------------

# Raw mode: the driver does its own line handling, and any terminal processing
# in between would mangle the records.
stty -F "$PORT" raw "$BAUD" -echo -echoe -echok -echoctl -echoke 2>/dev/null \
    || die "cannot configure $PORT at $BAUD baud"

args=("$IMAGE" "$PORT" --record-delay "$RECORD_DELAY")
if [[ "$APPLY" == "1" ]]; then
    args+=(--apply)
fi

if ! python3 "$DRIVER" "${args[@]}"; then
    cat >&2 <<EOF

  The transfer did not complete. Things worth checking:

    - is the firmware on $PORT built with the firmware update service?
      Only then does it answer fwbegin. The serial_update_test application
      is the reference; anything else will simply not reply.
    - is this the right port? Several boards look alike; 'make flash-serial'
      with no port lists what it can see.
    - is another terminal or IDE serial monitor using $PORT? Two readers split
      the reply between them, producing missing fragments and false timeouts.
    - does the board's console run at $BAUD baud? A USB CDC port ignores the
      rate, a real UART does not. Set SERIAL_UPDATE_BAUD to change it.
    - was the image built for this board? An image linked for a different
      flash size can be larger than the staging region.
    - a verification failure a few hundred bytes short of the image size
      usually means a page-flush's flash write outran the record-delay pacing
      and dropped a byte. Try a larger SERIAL_UPDATE_RECORD_DELAY (this run
      used ${RECORD_DELAY}s, chosen from the port name) before suspecting the
      link itself. A USB CDC port defaults to 0 because its flow control
      makes that impossible; if this port is a bridge to a real UART despite
      its name, set 0.005.
EOF
    exit 1
fi
