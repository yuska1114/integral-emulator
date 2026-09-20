# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later

CC ?= cc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Werror
CFLAGS += $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)
LIBMOBILE_ROOT ?= $(GB_ROOT)/third_party/libmobile
LIBMOBILE_BUILD ?= $(GB_ROOT)/build_exp/libmobile
PYTHON ?= python3

GB_SRC := ..
GB_ROOT := ../..
PROJECT_ROOT := $(abspath $(GB_ROOT)/../..)
SAMEBOY_DIR := $(GB_ROOT)/third_party/SameBoy
SAMEBOY_LIB := $(SAMEBOY_DIR)/build/lib/libsameboy.a
BUILD_DIR := $(GB_ROOT)/build_exp/mobile
SDL2_CONFIG ?= sdl2-config
SDL2_CFLAGS := $(shell $(SDL2_CONFIG) --cflags)
SDL2_LIBS := $(shell $(SDL2_CONFIG) --libs)
CPPFLAGS += -I. -I$(GB_SRC)/server -I$(GB_SRC)/common -I$(SAMEBOY_DIR)/Core -I$(LIBMOBILE_ROOT)
MOBILE_PLATFORM := $(shell uname -s)
MOBILE_PLATFORM_LIBS :=
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(MOBILE_PLATFORM)),)
RPATH_FLAG :=
MOBILE_PLATFORM_LIBS := -lws2_32 -liphlpapi
else ifeq ($(MOBILE_PLATFORM),Darwin)
RPATH_FLAG := -Wl,-rpath,@loader_path/libmobile
else
RPATH_FLAG := -Wl,-rpath,'$$ORIGIN/libmobile'
endif
LIBMOBILE_LIBS := -L$(LIBMOBILE_BUILD) -lmobile

RUNTIME_BIN := $(GB_ROOT)/build_exp/integral_gb_runtime_mobile_runtime
LIBMOBILE_STAMP := $(LIBMOBILE_BUILD)/.integral-built
MOBILE_CORE_TEST_BIN := $(BUILD_DIR)/mobile_core_test
MOBILE_RUNTIME_HOST_TEST_BIN := $(BUILD_DIR)/mobile_runtime_host_test
MOBILE_ROUTE_ENGINE_TEST_BIN := $(BUILD_DIR)/mobile_route_engine_test

COMMON_SOURCES := \
	$(GB_SRC)/common/display_scale.c \
	$(GB_SRC)/common/file_util.c \
	$(GB_SRC)/common/key_config.c \
	$(GB_SRC)/common/net_compat.c \
	$(GB_SRC)/common/sdl_text.c \
	$(GB_SRC)/common/string_util.c

RUNTIME_SOURCES := \
	mobile_runtime_host.c \
	mobile_runtime.c \
	mobile_adapter_bridge.c \
	mobile_bounded_buffer.c \
	mobile_session_manifest.c \
	mobile_route_engine.c \
	$(GB_SRC)/serial/serial_peripheral.c \
	$(GB_SRC)/server/slot.c \
	$(GB_SRC)/server/audio_player.c \
	$(GB_SRC)/server/screenshot.c \
	$(GB_SRC)/server/video_window.c \
	$(GB_SRC)/server/input_router.c \
	$(GB_SRC)/server/rom_profile.c \
	$(GB_SRC)/server/content_hash.c \
	$(GB_SRC)/server/sgb_boot_resource.c \
	$(COMMON_SOURCES)

.PHONY: all runtime test check-dependency verify-dependency clean

all: runtime

runtime: $(RUNTIME_BIN)

test: $(MOBILE_CORE_TEST_BIN) $(MOBILE_RUNTIME_HOST_TEST_BIN) $(MOBILE_ROUTE_ENGINE_TEST_BIN)
	$(MOBILE_CORE_TEST_BIN)
	$(MOBILE_RUNTIME_HOST_TEST_BIN)
	$(MOBILE_ROUTE_ENGINE_TEST_BIN)

verify-dependency:
	$(PYTHON) $(PROJECT_ROOT)/scripts/verify_libmobile_dependency.py $(LIBMOBILE_ROOT)

check-dependency: verify-dependency
	@test -f "$(LIBMOBILE_ROOT)/mobile.h" || (echo "mobile.h not found under LIBMOBILE_ROOT" >&2; exit 2)
	@test -e "$(LIBMOBILE_BUILD)/libmobile.dylib" -o \
		-e "$(LIBMOBILE_BUILD)/libmobile.so" -o \
		-e "$(LIBMOBILE_BUILD)/libmobile.dll" || \
		(echo "shared libmobile not found" >&2; exit 2)

$(RUNTIME_BIN): ../common/utf8_file.h ../../../common/screenshot.h ../../../common/screenshot_path.h  $(RUNTIME_SOURCES) ../../../common/window_focus.h $(SAMEBOY_LIB) $(LIBMOBILE_STAMP)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DINTEGRAL_GB_RUNTIME_MOBILE_PRODUCT_BUILD=1 $(SDL2_CFLAGS) $(CFLAGS) $(RUNTIME_SOURCES) $(SAMEBOY_LIB) $(LIBMOBILE_LIBS) $(RPATH_FLAG) $(SDL2_LIBS) $(MOBILE_PLATFORM_LIBS) -lm -o $@

$(LIBMOBILE_STAMP):
	cmake -S $(LIBMOBILE_ROOT) -B $(LIBMOBILE_BUILD) -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
	cmake --build $(LIBMOBILE_BUILD)
	touch $@

$(MOBILE_CORE_TEST_BIN): mobile_core_test.c mobile_adapter_eeprom.c mobile_bounded_buffer.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(MOBILE_RUNTIME_HOST_TEST_BIN): mobile_runtime_host.c mobile_runtime_host_stub_test.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(MOBILE_ROUTE_ENGINE_TEST_BIN): mobile_route_engine_test.c mobile_route_engine.c mobile_session_manifest.c $(GB_SRC)/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	$(RM) $(RUNTIME_BIN) $(MOBILE_CORE_TEST_BIN) $(MOBILE_RUNTIME_HOST_TEST_BIN) $(MOBILE_ROUTE_ENGINE_TEST_BIN)
