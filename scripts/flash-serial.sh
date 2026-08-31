#!/usr/bin/env bash
#
# Flash a board identified by its serial port, without touching BOOTSEL.
#
# Usage: flash-serial.sh <image.uf2> [port]
#
# What this does today, stated plainly because the name promises more than the
# mechanism delivers:
#
#   1. Asks the firmware on `port` to reboot into the bootloader. Two ways are
#      tried, because which one works depends on how the board is attached:
#
#        - the CLI command `bootsel`, for firmware built with the cli component
#          and a command that calls reset_usb_boot(). Works over a real UART.
#        - a 1200-baud touch, which the Pico SDK turns into a reboot for any
#          firmware built with pico_enable_stdio_usb. USB CDC only.
#
#   2. Waits for the board to re-appear in BOOTSEL, then loads the image with
#      picotool over USB.
#
# So the *upload* is still USB. What naming a port buys is knowing which board
# you are flashing: `picotool load -f` on a bench with three Picos plugged in
# will pick one of them, whereas rebooting through one specific port puts
# exactly one board into BOOTSEL.
#
# A genuine serial-only upload needs the resident bootloader that is not built
# yet. When it exists it belongs here, as a third mechanism tried before the
# USB path.

set -euo pipefail

IMAGE="${1:-}"
PORT="${2:-}"

# Seconds to wait for the board to come back in BOOTSEL.
RESET_TIMEOUT="${SERIAL_RESET_TIMEOUT:-10}"

# Command sent to a CLI-equipped firmware to ask it to reboot.
RESET_COMMAND="${SERIAL_RESET_COMMAND:-bootsel}"

# Rate to talk to that CLI at. Ignored by a USB CDC port, but a CLI on a real
# UART runs at whatever its profile configured.
RESET_BAUD="${SERIAL_RESET_BAUD:-115200}"

# Baud rate the SDK treats as "reboot into BOOTSEL" on a USB CDC port.
MAGIC_BAUD=1200

die() { echo "error: $*" >&2; exit 1; }

[[ -n "$IMAGE" ]] || die "no image given"
[[ -f "$IMAGE" ]] || die "image not found: $IMAGE"

command -v picotool >/dev/null 2>&1 || die "picotool not found in PATH"

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
# Ask the board to reboot
# ---------------------------------------------------------------------------

echo "asking the board on $PORT to reboot into the bootloader"

# The CLI route. Harmless when the firmware has no such command: it answers
# "unknown command" and carries on.
if stty -F "$PORT" raw "$RESET_BAUD" -echo >/dev/null 2>&1; then
    printf '%s\r\n' "$RESET_COMMAND" > "$PORT" 2>/dev/null || true
    sleep 0.3
fi

# The 1200-baud route. The SDK enables this by default for anything built with
# pico_enable_stdio_usb, so it works even for firmware with no CLI at all. It
# does nothing on a real UART, where the baud rate is just a baud rate.
stty -F "$PORT" "$MAGIC_BAUD" >/dev/null 2>&1 || true
sleep 0.3

# ---------------------------------------------------------------------------
# Wait for BOOTSEL, then load
# ---------------------------------------------------------------------------

echo -n "waiting for the board to appear in BOOTSEL"
deadline=$(( SECONDS + RESET_TIMEOUT ))
found=0
while (( SECONDS < deadline )); do
    if picotool info >/dev/null 2>&1; then
        found=1
        break
    fi
    echo -n "."
    sleep 0.3
done
echo

if (( found == 0 )); then
    cat >&2 <<EOF
error: no board in BOOTSEL after ${RESET_TIMEOUT}s.

  The reboot request went out on $PORT but nothing came back. Likely causes:

    - the firmware on that port has neither a '$RESET_COMMAND' command nor USB
      stdio, so it never saw the request. Check with 'make flash' instead,
      which resets over picotool's own vendor interface.
    - the port is a USB-to-serial adapter on a real UART. The reboot may well
      have worked, but the upload still needs a USB connection to the board,
      which does not exist yet without a resident bootloader.
    - picotool cannot see USB devices. Try it directly: picotool info
EOF
    exit 1
fi

echo "loading $(basename "$IMAGE")"
picotool load -x "$IMAGE"
