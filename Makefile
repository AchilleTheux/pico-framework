# pico-framework - developer frontend to CMake.
#
# CMake remains the real build system: this Makefile only selects a build
# directory and forwards to it. It deliberately contains no dependency or
# compilation logic of its own.
#
# A build is identified by three variables:
#
#   BOARD    hardware, passed through to the Pico SDK as PICO_BOARD
#   APP      the application under apps/ to build
#   PROFILE  the initial CMake cache under profiles/$(APP)/
#
# Each combination gets its own build directory, so configurations coexist
# instead of fighting over one CMake cache.
#
#   make                                              # the defaults below
#   make BOARD=pico2 APP=minimal PROFILE=debug
#   make BOARD=pico2 APP=minimal PROFILE=debug flash
#
# Make variables do not persist between invocations: every command for a
# non-default configuration must repeat all three values. In particular a bare
# `make flash` always means the default configuration.

BOARD   ?= pico
APP     ?= minimal
PROFILE ?= default

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------

ROOT         := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD_DIR    := $(ROOT)/build/$(BOARD)/$(APP)/$(PROFILE)
PROFILE_FILE := $(ROOT)/profiles/$(APP)/$(PROFILE).cmake
SDK_DIR      := $(ROOT)/lib/pico-sdk

# APP may name a nested directory, e.g. APP=tests/ws2812_test. Applications
# name their target app_<last path segment> (see DESIGN_DOC.md section 9), so
# the artifacts stay predictable from APP alone.
APP_TARGET := app_$(notdir $(APP))
ELF        := $(BUILD_DIR)/apps/$(APP)/$(APP_TARGET).elf
UF2        := $(BUILD_DIR)/apps/$(APP)/$(APP_TARGET).uf2

# Prefer Ninja when it is installed; fall back to Make.
GENERATOR ?= $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

# Extra flags forwarded to the configure step, e.g. CMAKE_ARGS=-DFOO=ON
CMAKE_ARGS ?=

# Parallel build jobs.
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

PICOTOOL ?= picotool

# --------------------------------------------------------------------------
# Serial port for flash-serial
#
# Accepts either form, because both read naturally:
#
#   make flash-serial PORT=/dev/ttyACM0
#   make flash-serial /dev/ttyACM0
#
# The second works by treating any goal that looks like a device path as the
# port rather than as something to build. Left unset, the script picks the port
# when exactly one is present and lists them when there is a choice.
# --------------------------------------------------------------------------

SERIAL_PORT_GOALS := $(filter /dev/%,$(MAKECMDGOALS))
PORT ?= $(firstword $(SERIAL_PORT_GOALS))

ifneq ($(SERIAL_PORT_GOALS),)
# Stop make trying to build the device path as a target of its own.
$(SERIAL_PORT_GOALS):
	@:
.PHONY: $(SERIAL_PORT_GOALS)
endif

.PHONY: all build configure reconfigure clean distclean flash flash-serial size \
        apps profiles test test-build ci help
.DEFAULT_GOAL := all

# --------------------------------------------------------------------------
# Preconditions
# --------------------------------------------------------------------------

# Checked by the goals that need them rather than at parse time, so `make help`
# works in a fresh clone.
define require_sdk
	@test -f "$(SDK_DIR)/pico_sdk_init.cmake" || { \
		echo "error: Pico SDK missing at $(SDK_DIR)"; \
		echo "       run: git submodule update --init --recursive"; \
		exit 1; }
endef

define require_app
	@test -f "$(ROOT)/apps/$(APP)/CMakeLists.txt" || { \
		echo "error: no application '$(APP)'"; \
		echo "       expected: apps/$(APP)/CMakeLists.txt"; \
		echo "       available:"; \
		$(MAKE) --no-print-directory apps | sed 's/^/         /'; \
		exit 1; }
endef

define require_profile
	@test -f "$(PROFILE_FILE)" || { \
		echo "error: no profile '$(PROFILE)' for app '$(APP)'"; \
		echo "       expected: profiles/$(APP)/$(PROFILE).cmake"; \
		echo "       available:"; \
		ls -1 "$(ROOT)/profiles/$(APP)"/*.cmake 2>/dev/null \
			| sed 's|.*/||;s/\.cmake$$//;s/^/         /' \
			| grep . || echo "         (none - profiles/$(APP)/ has no profiles)"; \
		exit 1; }
endef

# --------------------------------------------------------------------------
# Configure and build
# --------------------------------------------------------------------------

all: build

# Configure on first use only. The profile is an initial cache (`cmake -C`),
# which applies when the cache is created; changing BOARD, APP or PROFILE
# selects a different build directory rather than reusing an incompatible one.
$(BUILD_DIR)/CMakeCache.txt:
	$(require_sdk)
	$(require_app)
	$(require_profile)
	cmake -S "$(ROOT)" -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
		-C "$(PROFILE_FILE)" \
		-DPICO_BOARD=$(BOARD) \
		-DPICO_FRAMEWORK_APP=$(APP) \
		-DPICO_FRAMEWORK_PROFILE=$(PROFILE) \
		$(CMAKE_ARGS)

configure: $(BUILD_DIR)/CMakeCache.txt

# Discard this configuration's cache and configure it again. Needed after
# editing the profile, since an initial cache is only applied to a fresh cache.
reconfigure:
	rm -rf "$(BUILD_DIR)"
	$(MAKE) configure BOARD=$(BOARD) APP=$(APP) PROFILE=$(PROFILE)

# CMake decides what is out of date; this target never inspects sources.
build: configure
	cmake --build "$(BUILD_DIR)" --parallel $(JOBS)

# --------------------------------------------------------------------------
# Host tests
#
# A separate CMake project built with the host compiler, independent of BOARD,
# APP and PROFILE. See tests/CMakeLists.txt.
# --------------------------------------------------------------------------

HOST_TEST_DIR := $(ROOT)/build/host-tests

$(HOST_TEST_DIR)/CMakeCache.txt:
	cmake -S "$(ROOT)/tests" -B "$(HOST_TEST_DIR)" -G "$(GENERATOR)"

test-build: $(HOST_TEST_DIR)/CMakeCache.txt
	cmake --build "$(HOST_TEST_DIR)" --parallel $(JOBS)

test: test-build
	ctest --test-dir "$(HOST_TEST_DIR)" --output-on-failure

# Everything CI checks: host tests plus the whole build matrix, with warnings
# as errors. The matrix lives in the script so it has one definition.
ci:
	@"$(ROOT)/scripts/ci.sh"

# --------------------------------------------------------------------------
# Cleaning
# --------------------------------------------------------------------------

# Removes the selected configuration only. Other configurations survive.
clean:
	@test -d "$(BUILD_DIR)" \
		&& { echo "removing $(BUILD_DIR)"; rm -rf "$(BUILD_DIR)"; } \
		|| echo "nothing to clean at $(BUILD_DIR)"

# Removes every configuration.
distclean:
	rm -rf "$(ROOT)/build"

# --------------------------------------------------------------------------
# Flashing and inspection
# --------------------------------------------------------------------------

# Loads the selected configuration's UF2 over USB. -f puts a running board
# into BOOTSEL first; -x starts the firmware afterwards.
flash: build
	@command -v $(PICOTOOL) >/dev/null 2>&1 || { \
		echo "error: $(PICOTOOL) not found in PATH"; exit 1; }
	$(PICOTOOL) load -f -x "$(UF2)"

# Flashes the board on a named serial port, without needing BOOTSEL. Asks that
# board to reboot into the bootloader, then loads over USB; see the script for
# what that does and does not cover.
flash-serial: build
	@"$(ROOT)/scripts/flash-serial.sh" "$(UF2)" "$(PORT)"

size: build
	@arm-none-eabi-size "$(ELF)"

# --------------------------------------------------------------------------
# Discovery
# --------------------------------------------------------------------------

apps:
	@cd "$(ROOT)/apps" && find . -name CMakeLists.txt -printf '%h\n' \
		| sed 's|^\./||' | sort

profiles:
	@ls -1 "$(ROOT)/profiles/$(APP)"/*.cmake 2>/dev/null \
		| sed 's|.*/||;s/\.cmake$$//' \
		| grep . || echo "(no profiles for app '$(APP)')"

help:
	@echo "pico-framework"
	@echo ""
	@echo "usage: make [BOARD=<board>] [APP=<app>] [PROFILE=<profile>] [target]"
	@echo ""
	@echo "targets:"
	@echo "  build (default)  configure if needed, then build"
	@echo "  configure        configure the build directory only"
	@echo "  reconfigure      delete and re-configure (after editing a profile)"
	@echo "  flash            build, then load over USB with picotool"
	@echo "  flash-serial     reboot the board on PORT, then load. Takes"
	@echo "                   PORT=/dev/ttyACM0 or a bare /dev/ttyACM0"
	@echo "  size             build, then report section sizes"
	@echo "  test             build and run the host-side unit tests"
	@echo "  ci               everything CI checks: tests plus the build matrix"
	@echo "  clean            remove this configuration's build directory"
	@echo "  distclean        remove build/ entirely"
	@echo "  apps             list available applications"
	@echo "  profiles         list profiles for APP"
	@echo ""
	@echo "current configuration:"
	@echo "  BOARD     = $(BOARD)"
	@echo "  APP       = $(APP)"
	@echo "  PROFILE   = $(PROFILE)"
	@echo "  build dir = build/$(BOARD)/$(APP)/$(PROFILE)"
	@echo "  generator = $(GENERATOR)"
	@echo ""
	@echo "note: make variables do not persist between invocations; repeat all"
	@echo "      three for every command, including flash."
