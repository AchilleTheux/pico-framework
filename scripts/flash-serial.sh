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
# Send it
# ---------------------------------------------------------------------------

# Raw mode: the driver does its own line handling, and any terminal processing
# in between would mangle the records.
stty -F "$PORT" raw "$BAUD" -echo -echoe -echok -echoctl -echoke 2>/dev/null \
    || die "cannot configure $PORT at $BAUD baud"

args=("$IMAGE" "$PORT")
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
    - does the board's console run at $BAUD baud? A USB CDC port ignores the
      rate, a real UART does not. Set SERIAL_UPDATE_BAUD to change it.
    - was the image built for this board? An image linked for a different
      flash size can be larger than the staging region.
EOF
    exit 1
fi
