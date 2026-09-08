.PHONY: all frontend source-stack smoke reference-smoke verify-sources test \
	transfer-pak-save-test transfer-smoke \
	interactive-stop-test stop-request-file-test gui-smoke process-test cart-test keymap-test \
	hotkeys-test remote-input-test remote-input-hook-test remote-media-ipc-test custom-input-test n64-save-layout-test \
	config-load-test release-macos ai-context


CC ?= cc
CFLAGS ?= -O2
CFLAGS += $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)
N64_RUNTIME_CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic $(CFLAGS)
N64_RUNTIME_CPPFLAGS = -Ibuild/prefix/include/mupen64plus -Isrc $$(sdl2-config --cflags)
EXEEXT :=
TRANSFER_PAK_SOURCES = src/transfer_pak/storage.c \
	src/transfer_pak/save_stage.c src/transfer_pak/media_session.c
TRANSFER_PAK_HEADERS = src/transfer_pak/storage.h \
	src/transfer_pak/save_stage.h src/transfer_pak/media_session.h
N64_RUNTIME_SOURCES = src/main.c src/platform/dynlib.c src/platform/thread.c \
	src/platform/process.c src/platform/ime.c src/gui/menu.c src/gui/text.c src/gui/cart.c \
	src/gui/keymap.c src/gui/hotkeys.c src/remote_input.c \
	src/remote_media_ipc.c \
	$(TRANSFER_PAK_SOURCES)

N64_RUNTIME_EXPORT_LDFLAGS = -Wl,-export_dynamic

ifeq ($(shell uname -s),Linux)
N64_RUNTIME_EXPORT_LDFLAGS = -Wl,--export-dynamic
N64_RUNTIME_LDLIBS = -ldl -pthread $$(sdl2-config --libs)
else ifneq ($(OS),Windows_NT)
N64_RUNTIME_LDLIBS = -pthread $$(sdl2-config --libs)
endif

ifeq ($(shell uname -s),Darwin)
N64_RUNTIME_LDLIBS += -framework Carbon
endif

ifeq ($(OS),Windows_NT)
N64_RUNTIME_LDLIBS = $$(sdl2-config --libs)
N64_RUNTIME_EXPORT_LDFLAGS =
EXEEXT := .exe
endif

all: frontend

ai-context:
	sh tools/build_ai_context_bundle.sh

frontend: source-stack build/integral_n64_runtime_frontend$(EXEEXT)

build/integral_n64_runtime_frontend$(EXEEXT): $(N64_RUNTIME_SOURCES) $(TRANSFER_PAK_HEADERS) src/platform/dynlib.h src/platform/thread.h
	mkdir -p build
	$(CC) $(N64_RUNTIME_CPPFLAGS) $(N64_RUNTIME_CFLAGS) \
		$(N64_RUNTIME_SOURCES) -o $@ $(N64_RUNTIME_EXPORT_LDFLAGS) $(N64_RUNTIME_LDLIBS)

source-stack:
	tools/build_source_stack.sh

smoke: frontend
	tools/direct_smoke_test.sh

reference-smoke: source-stack
	tools/smoke_test.sh

verify-sources:
	tools/verify_sources.sh

build/test-transfer-pak-save-stage: tests/transfer_pak_save_stage_test.c $(TRANSFER_PAK_SOURCES) $(TRANSFER_PAK_HEADERS)
	mkdir -p build
	$(CC) -Isrc $(N64_RUNTIME_CFLAGS) tests/transfer_pak_save_stage_test.c \
		$(TRANSFER_PAK_SOURCES) -o $@

transfer-pak-save-test: build/test-transfer-pak-save-stage
	tools/test_transfer_pak_save_stage.sh

transfer-smoke: frontend
	tools/transfer_pak_smoke_test.sh

interactive-stop-test: build/integral_n64_runtime_frontend
	tools/test_interactive_stop.sh

stop-request-file-test: build/integral_n64_runtime_frontend
	@tools/test_stop_request_file.sh; test_result=$$?; \
		[ $$test_result -eq 0 ] || [ $$test_result -eq 77 ]

build/test-process: tests/process_test.c src/platform/process.c src/platform/process.h \
	src/platform/thread.c src/platform/thread.h
	mkdir -p build
	$(CC) -Isrc $(N64_RUNTIME_CFLAGS) tests/process_test.c \
		src/platform/process.c src/platform/thread.c -o $@ $(N64_RUNTIME_LDLIBS)

process-test: build/test-process
	build/test-process

build/test-gui-cart: tests/gui_cart_test.c src/gui/cart.c src/gui/cart.h \
	$(TRANSFER_PAK_SOURCES) $(TRANSFER_PAK_HEADERS)
	mkdir -p build
	$(CC) -Isrc $$(sdl2-config --cflags) $(N64_RUNTIME_CFLAGS) \
		tests/gui_cart_test.c src/gui/cart.c $(TRANSFER_PAK_SOURCES) \
		-o $@ $$(sdl2-config --libs)

cart-test: build/test-gui-cart
	tools/test_gui_cart.sh

build/test-gui-keymap: tests/gui_keymap_test.c src/gui/keymap.c src/gui/keymap.h
	mkdir -p build
	$(CC) -Isrc $$(sdl2-config --cflags) $(N64_RUNTIME_CFLAGS) \
		tests/gui_keymap_test.c src/gui/keymap.c -o $@ $$(sdl2-config --libs)

keymap-test: build/test-gui-keymap
	build/test-gui-keymap

build/test-gui-hotkeys: tests/gui_hotkeys_test.c src/gui/hotkeys.c \
	src/gui/hotkeys.h src/gui/keymap.c src/gui/keymap.h
	mkdir -p build
	$(CC) -Isrc $$(sdl2-config --cflags) $(N64_RUNTIME_CFLAGS) \
		tests/gui_hotkeys_test.c src/gui/hotkeys.c src/gui/keymap.c \
		-o $@ $$(sdl2-config --libs)

hotkeys-test: build/test-gui-hotkeys
	build/test-gui-hotkeys

config-load-test: frontend
	tools/test_config_load.sh

build/test-remote-input: tests/remote_input_test.c src/remote_input.c src/remote_input.h
	mkdir -p build
	$(CC) -Isrc $(N64_RUNTIME_CFLAGS) tests/remote_input_test.c \
		src/remote_input.c -o $@

remote-input-test: build/test-remote-input
	build/test-remote-input

remote-input-hook-test: frontend build/test-remote-input
	tools/test_remote_input_hook.sh

build/test-remote-media-ipc: tests/remote_media_ipc_test.c src/remote_media_ipc.c src/remote_media_ipc.h
	mkdir -p build
	$(CC) -Isrc $(N64_RUNTIME_CFLAGS) tests/remote_media_ipc_test.c \
		src/remote_media_ipc.c -o $@

remote-media-ipc-test: build/test-remote-media-ipc
	build/test-remote-media-ipc build/remote-media-ipc-test.bin

custom-input-test: frontend
	tools/test_custom_input.sh

n64-save-layout-test: frontend
	tools/test_n64_save_layout.sh

release-macos:
	tools/build_release_macos.sh

gui-smoke: frontend
	tools/test_gui_smoke.sh

test: transfer-pak-save-test process-test cart-test keymap-test hotkeys-test remote-input-test remote-media-ipc-test config-load-test gui-smoke
