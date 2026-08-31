#!/usr/bin/env bash
#
# Everything CI checks, runnable locally with `make ci`.
#
# The build matrix lives here rather than in the workflow file so there is one
# definition of it, and so a developer can reproduce a CI failure without
# pushing (DESIGN_DOC.md section 19).
#
# Usage: scripts/ci.sh [--quick]
#   --quick  only the host tests and one board per architecture

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QUICK=0
[[ "${1:-}" == "--quick" ]] && QUICK=1

# BOARD:APP:PROFILE. Covers both architectures, a WiFi board, both components,
# and enough profiles to prove they reach the build.
MATRIX=(
    "pico:minimal:default"
    "pico2:minimal:default"
    "pico2:minimal:debug"
    "pico2_w:minimal:default"
    "pico:tests/ws2812_test:default"
    "pico2:tests/ws2812_test:default"
    "pico2:tests/ws2812_test:long_strip"
    "pico:tests/ws2812_test:dma"
    "pico2:tests/ws2812_test:dma"
    "pico:tests/cli_test:default"
    "pico2:tests/cli_test:default"
    "pico2:tests/cli_test:uart"
    "pico2_w:tests/cli_test:machine"
    "pico:tests/half_duplex_uart_test:default"
    "pico2:tests/half_duplex_uart_test:default"
    "pico2:tests/half_duplex_uart_test:transceiver"
    "pico:tests/servo_test:ax12"
    "pico2:tests/servo_test:ax12"
    "pico2:tests/servo_test:feetech_sts"
    "pico2:tests/servo_test:feetech_scs"
    "pico2:tests/servo_test:ax12_leds"
    # A build with most logging compiled out, to keep that path exercised.
    "pico2:tests/servo_test:quiet"
    "pico:tests/firmware_update_test:default"
    "pico2:tests/firmware_update_test:default"
    # A custom board header from boards/, to keep the board mechanism covered.
    "bras_attrape_caisse:minimal:default"
    "bras_attrape_caisse:tests/firmware_update_test:default"
    "pico2:tests/firmware_update_test:write_flash"
    "pico:tests/config_test:default"
    "pico2:tests/config_test:default"
    "pico:tests/i2c_test:default"
    "pico2:tests/i2c_test:default"
    "bras_attrape_caisse:tests/i2c_test:bras_left"
    "pico:tests/serial_update_test:default"
    "pico2:tests/serial_update_test:with_apply"
    "bras_attrape_caisse:tests/serial_update_test:with_apply"
)

QUICK_MATRIX=(
    "pico:minimal:default"
    "pico2:minimal:default"
)

if [[ $QUICK -eq 1 ]]; then
    MATRIX=("${QUICK_MATRIX[@]}")
fi

# CI must not accept a warning it would tolerate on a developer's bench.
export CI_CMAKE_ARGS="-DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON"

rule() { printf '%s\n' "------------------------------------------------------------"; }

# ---------------------------------------------------------------------------
# Toolchain provenance
#
# Section 5: a pinned SDK alone does not make a build reproducible, so record
# what actually produced these binaries.
# ---------------------------------------------------------------------------

rule
echo "toolchain"
rule
printf '  %-22s %s\n' "cmake"       "$(cmake --version | head -1)"
printf '  %-22s %s\n' "arm gcc"     "$(arm-none-eabi-gcc --version | head -1)"
printf '  %-22s %s\n' "host cc"     "$("${CC:-cc}" --version | head -1)"
printf '  %-22s %s\n' "ninja"       "$(ninja --version 2>/dev/null || echo 'not installed')"
printf '  %-22s %s\n' "picotool"    "$(picotool version 2>/dev/null | head -1 || echo 'not installed')"
printf '  %-22s %s\n' "python"      "$(python3 --version)"
printf '  %-22s %s\n' "pico-sdk"    "$(git -C lib/pico-sdk describe --tags --always) ($(git -C lib/pico-sdk rev-parse --short HEAD))"
echo

# ---------------------------------------------------------------------------
# Host tests
# ---------------------------------------------------------------------------

rule
echo "host tests"
rule
make test
echo

# ---------------------------------------------------------------------------
# Build matrix
# ---------------------------------------------------------------------------

rule
echo "build matrix (warnings as errors)"
rule

failures=()
results=()

for entry in "${MATRIX[@]}"; do
    IFS=: read -r board app profile <<< "$entry"
    label="$board $app $profile"
    printf '  %-44s ' "$label"

    log="$(mktemp)"
    if make BOARD="$board" APP="$app" PROFILE="$profile" \
            CMAKE_ARGS="$CI_CMAKE_ARGS" > "$log" 2>&1; then
        elf="build/$board/$app/$profile/apps/$app/app_$(basename "$app").elf"
        read -r text data bss _ <<< "$(arm-none-eabi-size "$elf" | tail -1)"
        printf 'ok    flash %6s B   ram %6s B\n' "$((text + data))" "$((data + bss))"
        results+=("$label|$((text + data))|$((data + bss))")
    else
        echo "FAILED"
        echo "--- output ---"
        tail -40 "$log"
        echo "--------------"
        failures+=("$label")
    fi
    rm -f "$log"
done

echo

# ---------------------------------------------------------------------------
# Host and device must agree on the image checksum
#
# The serial update rests on it: the board checksums the flash it wrote and the
# host predicts what that will be. If the two drift apart, every transfer fails
# verification and neither side says which is wrong.
# ---------------------------------------------------------------------------

if [[ ${#failures[@]} -eq 0 ]]; then
    rule
    echo "host and device checksum agreement"
    rule
    agreement_images=()
    for entry in "${MATRIX[@]}"; do
        IFS=: read -r board app profile <<< "$entry"
        [[ "$app" == *serial_update_test* ]] || continue
        agreement_images+=("build/$board/$app/$profile/apps/$app/app_$(basename "$app").hex")
    done

    if [[ ${#agreement_images[@]} -gt 0 ]]; then
        if ! "$ROOT/scripts/check-hex-agreement.sh" "${agreement_images[@]}"; then
            failures+=("host/device checksum agreement")
        fi
    fi
    echo
fi

rule
if [[ ${#failures[@]} -eq 0 ]]; then
    echo "all ${#results[@]} configurations built"
    rule
    exit 0
fi

echo "${#failures[@]} of $((${#results[@]} + ${#failures[@]})) configurations failed:"
printf '  %s\n' "${failures[@]}"
rule
exit 1
