#!/usr/bin/env bash
#
# Check that the host and the device compute the same checksum for an image.
#
# This is the property the whole serial update rests on. The device checksums
# the flash it wrote; the host has to predict exactly what that will be —
# records at their offsets, gaps left as 0xFF because erased flash reads that
# way, and a span running to the highest byte written. If the two ever disagree
# a perfectly good transfer fails verification, and the failure gives no clue
# which side is wrong.
#
# So both are run over real built images and compared: scripts/serial_update.py
# on one side, the device's own firmware_receive on the other, compiled for the
# host from the same sources it ships in.
#
# Usage: check-hex-agreement.sh <image.hex> [more.hex ...]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export AGREEMENT_ROOT="$ROOT"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/harness.c" <<'EOF'
/* Runs a .hex through the device's receive path and prints what it would
   report: the image size and the CRC-32 of the staged bytes. */
#include <stdio.h>
#include <string.h>
#include "firmware_receive.h"
#include "crc.h"

static uint8_t staged[4u * 1024u * 1024u];

static bool write_page(void *ctx, uint32_t offset, const uint8_t *page, uint32_t size) {
    (void)ctx;
    if ((size_t)offset + size > sizeof(staged)) return false;
    memcpy(&staged[offset], page, size);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    memset(staged, 0xFF, sizeof(staged));   /* as erased flash reads */

    firmware_receive_t rx;
    const firmware_receive_config_t config = {
        .base_address = 0x10000000u, .capacity = sizeof(staged),
        .write_page = write_page, .write_ctx = NULL,
    };
    if (firmware_receive_begin(&rx, &config) != FIRMWARE_RECEIVE_OK) return 1;

    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        firmware_receive_result_t r = firmware_receive_line(&rx, line);
        if (r != FIRMWARE_RECEIVE_OK && r != FIRMWARE_RECEIVE_COMPLETE) {
            fprintf(stderr, "receive failed: %s\n", firmware_receive_result_name(r));
            return 1;
        }
    }
    fclose(f);
    if (!firmware_receive_is_complete(&rx)) return 1;

    const uint32_t size = firmware_receive_image_size(&rx);
    printf("%u %08X\n", size, crc32(staged, size));
    return 0;
}
EOF

"${CC:-cc}" -O1 -o "$WORK/harness" "$WORK/harness.c" \
    "$ROOT/components/firmware_update/firmware_receive.c" \
    "$ROOT/components/hex_parser/hex_parser.c" \
    "$ROOT/components/crc/crc.c" \
    -I"$ROOT/components/firmware_update/include" \
    -I"$ROOT/components/hex_parser/include" \
    -I"$ROOT/components/crc/include"

failures=0
for image in "$@"; do
    [[ -f "$image" ]] || { echo "  skipped (not built): $image"; continue; }

    device="$("$WORK/harness" "$image")"
    host="$(python3 - "$image" <<'PY'
import importlib.util, os, sys, zlib
root = os.environ["AGREEMENT_ROOT"]
spec = importlib.util.spec_from_file_location("su", os.path.join(root, "scripts", "serial_update.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
image = module.parse_hex(sys.argv[1])
print(f"{len(image)} {zlib.crc32(image) & 0xFFFFFFFF:08X}")
PY
)"

    # build/<board>/<app...>/<profile>/apps/... — take board and profile.
    # .../build/<board>/<app...>/<profile>/apps/... — report board and profile.
    trimmed="${image##*build/}"
    board="${trimmed%%/*}"
    rest="${trimmed#*/}"
    rest="${rest%%/apps/*}"
    name="$board $rest"
    if [[ "$device" == "$host" ]]; then
        printf '  %-34s agree: %s\n' "$name" "$device"
    else
        printf '  %-34s DISAGREE: device %s, host %s\n' "$name" "$device" "$host"
        failures=$((failures + 1))
    fi
done

exit $(( failures > 0 ))
