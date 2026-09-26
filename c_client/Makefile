CC ?= cc
PKG_CONFIG ?= pkg-config
PYTHON ?= python3
WINDRES ?= windres
CFLAGS ?= -O2 -Wall -Wextra -Werror
CFLAGS += $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)
PNG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpng)
PNG_LIBS := $(shell $(PKG_CONFIG) --libs libpng)
HAS_SDL_TTF := $(shell $(PKG_CONFIG) --exists SDL2_ttf 2>/dev/null && echo 1)
SDL_TTF_CFLAGS := $(shell $(PKG_CONFIG) --cflags SDL2_ttf 2>/dev/null)
SDL_TTF_LIBS := $(shell $(PKG_CONFIG) --libs-only-L SDL2_ttf 2>/dev/null) -lSDL2_ttf
HAS_OPENSSL := $(shell $(PKG_CONFIG) --exists openssl 2>/dev/null && echo 1)
OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)
HAS_OPENH264 := $(shell $(PKG_CONFIG) --exists openh264 2>/dev/null && echo 1)
OPENH264_CFLAGS := $(shell $(PKG_CONFIG) --cflags openh264 2>/dev/null)
OPENH264_LIBS := $(shell $(PKG_CONFIG) --libs openh264 2>/dev/null)
UNAME_S := $(shell uname -s)
PLATFORM_LIBS :=
EXEEXT :=
WINDOWS_CLIENT_RESOURCE :=
INTEGRAL_EMULATOR_ICON_ICO ?= ../assets/public/integral_emulator_icon.ico
ifeq ($(HAS_SDL_TTF),1)
CFLAGS += -DINTEGRAL_USE_SDL_TTF
SDL_CFLAGS += $(SDL_TTF_CFLAGS)
SDL_LIBS += $(SDL_TTF_LIBS)
endif
ifeq ($(HAS_OPENSSL),1)
CFLAGS += -DINTEGRAL_USE_OPENSSL
SDL_CFLAGS += $(OPENSSL_CFLAGS)
SDL_LIBS += $(OPENSSL_LIBS)
endif
ifeq ($(UNAME_S),Darwin)
PLATFORM_LIBS += -framework Security -framework CoreText -framework CoreGraphics -framework CoreFoundation \
	-framework CoreMedia -framework CoreVideo -framework VideoToolbox
endif
ifeq ($(UNAME_S),Linux)
ifeq ($(HAS_OPENH264),1)
CFLAGS += -DINTEGRAL_USE_OPENH264
SDL_CFLAGS += $(OPENH264_CFLAGS)
PLATFORM_LIBS += $(OPENH264_LIBS)
else
$(error Linux ROOM support requires the openh264 development package)
endif
CFLAGS += -Wno-format-truncation
endif
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)),)
CFLAGS += -Wno-format-truncation -Wno-unused-function
PLATFORM_LIBS += -lws2_32 -liphlpapi -lbcrypt -ladvapi32 -lmfplat -lmfuuid -lmf -lole32 -luuid
EXEEXT := .exe
WINDOWS_CLIENT_RESOURCE := build/integral_client.res.o
endif

BUILD_DIR := build
SAMEBOY_DIR := ../runtimes/gb/third_party/SameBoy
SAMEBOY_LIB := $(SAMEBOY_DIR)/build/lib/libsameboy.a
TARGET := $(BUILD_DIR)/integral_client$(EXEEXT)
MEDIA_RELAY_SMOKE_TARGET := $(BUILD_DIR)/media_relay_smoke$(EXEEXT)
MEDIA_CODEC_TEST_TARGET := $(BUILD_DIR)/media_codec_test$(EXEEXT)
MEDIA_FOUNDATION_PROBE_TARGET := $(BUILD_DIR)/media_foundation_probe$(EXEEXT)
MEDIA_H264_TEST_TARGET := $(BUILD_DIR)/media_h264_test$(EXEEXT)
MEDIA_H264_INTEROP_TARGET := $(BUILD_DIR)/media_h264_interop$(EXEEXT)
GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET := $(BUILD_DIR)/integral_gb_runtime_fixed_host$(EXEEXT)
GB_RUNTIME_SGB2_BOOT := $(BUILD_DIR)/bootroms/sgb2_boot.bin
GB_RUNTIME_FIXED_HOST_RUNTIME_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_runtime_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_PRODUCT_RUNTIME_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_product_runtime_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_SNAPSHOT_IPC_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_snapshot_ipc_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_RESULT_IPC_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_result_ipc_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_ABNORMAL_EXIT_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_abnormal_exit_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_CHILD_RUNTIME_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_child_runtime_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_SCHEDULER_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_scheduler_test$(EXEEXT)
ROOM_POLL_WORKER_TEST_TARGET := $(BUILD_DIR)/room_poll_worker_test$(EXEEXT)
CURRENT_API_CONTRACT_TEST_TARGET := $(BUILD_DIR)/current_api_contract_test$(EXEEXT)
GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET := $(BUILD_DIR)/gb_runtime_fixed_host_rom_resolver_test$(EXEEXT)
ROM_METADATA_TEST_TARGET := $(BUILD_DIR)/rom_metadata_test$(EXEEXT)
ROOM_MATCHING_RESPONSE_TEST_TARGET := $(BUILD_DIR)/room_matching_response_test$(EXEEXT)
MOBILE_SESSION_CONTRACT_TEST_TARGET := $(BUILD_DIR)/mobile_session_contract_test$(EXEEXT)
CLIENT_CONFIG_CONTROLLER_TEST_TARGET := $(BUILD_DIR)/client_config_controller_test$(EXEEXT)
CREDENTIAL_STORE_TEST_TARGET := $(BUILD_DIR)/credential_store_test$(EXEEXT)
HTTP_SOURCES := http_client.c http_payload_codec.c http_transport.c api_account.c api_rom_save.c api_room.c api_mobile.c api_fixed_host.c
HTTP_HEADERS := http_client.h http_client_internal.h http_payload_codec.h http_transport.h client_version.h
SOURCES := login_client.c client_app.c client_local.c client_view.c client_runtime_support.c client_log.c client_diagnostics.c client_save_sync.c client_save_outbox.c client_launch_n64_local.c client_launch_gb_mobile_prepare.c client_launch_gb_mobile.c client_launch_gb_local.c client_account.c client_user_config.c client_rom_catalog.c client_rom_editor.c client_rom_registration.c client_file_io.c client_key_config.c client_ui_common.c client_ui_account.c client_ui_menu.c sdl_text.c sdl_unicode_text.c $(HTTP_SOURCES) client_config.c credential_store.c rom_metadata.c \
	client_room_common.c client_room_link.c client_room_n64.c ../runtimes/common/transfer_sav_ipc.c \
	media_relay_client.c media_codec.c media_h264.c n64_runtime_media_stream.c room_poll_worker.c \
	mobile_session_contract.c gb_runtime_fixed_host_rom_resolver.c gb_runtime_fixed_host_runtime.c gb_runtime_fixed_host_product_runtime.c gb_runtime_fixed_host_snapshot_ipc.c gb_runtime_fixed_host_result_ipc.c gb_runtime_fixed_host_child_runtime.c \
	../runtimes/gb/src/server/gb_link_engine.c ../runtimes/gb/src/server/secure_memory.c ../runtimes/gb/src/server/content_hash.c \
	../runtimes/gb/src/server/rom_profile.c ../runtimes/gb/src/server/sgb_boot_resource.c ../runtimes/gb/src/server/slot.c \
	../runtimes/gb/src/serial/serial_peripheral.c ../runtimes/gb/src/common/file_util.c ../runtimes/gb/src/common/net_compat.c \
	../runtimes/gb/src/common/display_scale.c ../runtimes/gb/src/common/key_config.c ../runtimes/gb/src/common/string_util.c \
	../runtimes/n64/src/remote_media_ipc.c

SAMEBOY_HEADERS := $(wildcard $(SAMEBOY_DIR)/Core/*.h)
SOURCES += client_screenshots.c
GB_ENGINE_HEADERS := ../runtimes/common/screenshot.h ../runtimes/common/screenshot_path.h  ../runtimes/common/window_focus.h ../runtimes/gb/src/common/display_scale.h ../runtimes/gb/src/common/file_util.h ../runtimes/gb/src/common/key_config.h \
	../runtimes/gb/src/common/net_compat.h ../runtimes/gb/src/common/protocol.h \
	../runtimes/gb/src/common/string_util.h ../runtimes/gb/src/server/content_hash.h \
	../runtimes/gb/src/server/gb_link_engine.h ../runtimes/gb/src/server/rom_profile.h \
	../runtimes/gb/src/server/secure_memory.h ../runtimes/gb/src/server/sgb_boot_resource.h \
	../runtimes/gb/src/server/slot.h ../runtimes/gb/src/serial/serial_peripheral.h \
	../runtimes/n64/src/remote_media_ipc.h $(SAMEBOY_HEADERS)
GB_ENGINE_HEADERS += ../runtimes/gb/src/common/utf8_file.h
CLIENT_HEADERS := client_state.h client_version.h client_app.h client_local.h client_view.h client_runtime_support.h client_log.h client_diagnostics.h client_save_outbox.h client_launch_n64_local.h client_launch_gb_mobile.h client_save_sync.h client_launch_gb_local.h client_config.h client_account.h client_user_config.h client_rom_catalog.h client_rom_editor.h client_rom_registration.h client_file_io.h client_key_config.h client_ui_common.h client_ui_account.h client_ui_menu.h credential_store.h $(HTTP_HEADERS) mobile_session_contract.h \
	client_room_common.h client_room_link.h client_room_n64.h \
	room_poll_worker.h media_relay_client.h n64_runtime_media_stream.h media_codec.h media_h264.h \
	rom_metadata.h sdl_text.h sdl_unicode_text.h gb_runtime_fixed_host_rom_resolver.h \
	gb_runtime_fixed_host_runtime.h gb_runtime_fixed_host_product_runtime.h \
	gb_runtime_fixed_host_snapshot_ipc.h gb_runtime_fixed_host_result_ipc.h \
	gb_runtime_fixed_host_child_runtime.h $(GB_ENGINE_HEADERS)
CLIENT_HEADERS += client_screenshots.h windows_process.h ../runtimes/gb/src/common/utf8_file.h
CLIENT_HEADERS += ../runtimes/common/transfer_sav_ipc.h

.PHONY: all gb-runtime-fixed-host-product gb-runtime-fixed-host-product-smoke gb-runtime-fixed-host-trade-terminal-smoke gb-runtime-fixed-host-runtime-test gb-runtime-fixed-host-product-runtime-test gb-runtime-fixed-host-snapshot-ipc-test gb-runtime-fixed-host-result-ipc-test gb-runtime-fixed-host-scheduler-test gb-runtime-fixed-host-rom-resolver-test rom-metadata-test client-config-controller-test credential-store-test clean smoke media-relay-smoke media-codec-test media-foundation-probe media-h264-test media-h264-interop room-poll-worker-test current-api-contract-test room-matching-response-test mobile-session-contract-test n64-runtime-stop-process-test

all: $(TARGET) $(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET)

ifeq ($(OS),Windows_NT)
$(BUILD_DIR)/windows_boundary_test.exe: windows_boundary_test.c windows_launcher.c windows_process.h rom_metadata.c client_file_io.c ../runtimes/gb/src/common/utf8_file.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -municode -o $@ windows_boundary_test.c rom_metadata.c client_file_io.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

.PHONY: windows-boundary-test
windows-boundary-test: $(BUILD_DIR)/windows_boundary_test.exe
	cd $(BUILD_DIR) && ./windows_boundary_test.exe
endif

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCES) $(CLIENT_HEADERS) client_screenshots.h $(SAMEBOY_LIB) $(WINDOWS_CLIENT_RESOURCE) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(PNG_CFLAGS) -std=gnu11 -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ $(SOURCES) $(WINDOWS_CLIENT_RESOURCE) $(SAMEBOY_LIB) -lm $(SDL_LIBS) $(PNG_LIBS) $(PLATFORM_LIBS)

.PHONY: screenshots-test
$(BUILD_DIR)/client_screenshots_test$(EXEEXT): client_screenshots_test.c client_screenshots.c client_screenshots.h sdl_text.c sdl_text.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(PNG_CFLAGS) -std=gnu11 -o $@ client_screenshots_test.c client_screenshots.c sdl_text.c $(SDL_LIBS) $(PNG_LIBS)

screenshots-test: $(BUILD_DIR)/client_screenshots_test$(EXEEXT)
	$(PYTHON) ../tests/run_screenshots_test.py $<

ifneq ($(WINDOWS_CLIENT_RESOURCE),)
build/integral_emulator_icon.ico: $(INTEGRAL_EMULATOR_ICON_ICO) ../scripts/verify_app_icon_assets.py | $(BUILD_DIR)
	$(PYTHON) ../scripts/verify_app_icon_assets.py "$(INTEGRAL_EMULATOR_ICON_ICO)" --format ico
	cp -f "$(INTEGRAL_EMULATOR_ICON_ICO)" $@

$(WINDOWS_CLIENT_RESOURCE): windows_launcher.rc build/integral_emulator_icon.ico | $(BUILD_DIR)
	$(WINDRES) windows_launcher.rc -O coff -o $@
endif

$(SAMEBOY_LIB): $(wildcard $(SAMEBOY_DIR)/Core/*.c $(SAMEBOY_DIR)/Core/*.h)
	$(MAKE) -C $(SAMEBOY_DIR) lib CONF=release\
		$(if $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS),CC="$(CC) $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)")

N64_RUNTIME_STOP_TEST_DIR ?= $(BUILD_DIR)/n64_runtime_stop_process_test

n64-runtime-stop-process-test: $(TARGET)
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)),)
	mkdir -p $(N64_RUNTIME_STOP_TEST_DIR)
	./$(TARGET) --n64-stop-process-test $(N64_RUNTIME_STOP_TEST_DIR) \
		--log-file $(N64_RUNTIME_STOP_TEST_DIR)/client.log
	grep -Fx PASS $(N64_RUNTIME_STOP_TEST_DIR)/result.txt
else
	@echo "n64-runtime-stop-process-test requires Windows"
	@false
endif

$(MEDIA_RELAY_SMOKE_TARGET): media_relay_smoke.c media_relay_client.c media_relay_client.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ media_relay_smoke.c media_relay_client.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

media-relay-smoke: $(MEDIA_RELAY_SMOKE_TARGET)
	$(PYTHON) ../tests/run_media_relay_client_smoke.py ./$(MEDIA_RELAY_SMOKE_TARGET)

$(MEDIA_CODEC_TEST_TARGET): media_codec_test.c media_codec.c media_codec.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ media_codec_test.c media_codec.c -lm

media-codec-test: $(MEDIA_CODEC_TEST_TARGET)
	./$(MEDIA_CODEC_TEST_TARGET)

$(MEDIA_FOUNDATION_PROBE_TARGET): media_foundation_probe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ media_foundation_probe.c -lmfplat -lmfuuid -lole32

media-foundation-probe: $(MEDIA_FOUNDATION_PROBE_TARGET)
	./$(MEDIA_FOUNDATION_PROBE_TARGET)

$(MEDIA_H264_TEST_TARGET): media_h264_test.c media_h264.c media_h264.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ media_h264_test.c media_h264.c $(PLATFORM_LIBS)

media-h264-test: $(MEDIA_H264_TEST_TARGET)
	./$(MEDIA_H264_TEST_TARGET)

$(MEDIA_H264_INTEROP_TARGET): media_h264_interop.c media_h264.c media_h264.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ media_h264_interop.c media_h264.c $(PLATFORM_LIBS)

media-h264-interop: $(MEDIA_H264_INTEROP_TARGET)

GB_RUNTIME_FIXED_HOST_CORE_SOURCES := gb_runtime_fixed_host_runtime.c \
	../runtimes/gb/src/server/gb_link_engine.c \
	../runtimes/gb/src/server/secure_memory.c \
	../runtimes/gb/src/server/content_hash.c \
	../runtimes/gb/src/server/rom_profile.c \
	../runtimes/gb/src/server/sgb_boot_resource.c \
	../runtimes/gb/src/server/slot.c \
	../runtimes/gb/src/serial/serial_peripheral.c \
	../runtimes/gb/src/common/file_util.c \
	../runtimes/gb/src/common/net_compat.c

GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES := integral_gb_runtime_fixed_host.c gb_runtime_fixed_host_product_runtime.c gb_runtime_fixed_host_scheduler.c gb_runtime_fixed_host_snapshot_ipc.c gb_runtime_fixed_host_result_ipc.c $(GB_RUNTIME_FIXED_HOST_CORE_SOURCES) media_relay_client.c \
	media_codec.c media_h264.c n64_runtime_media_stream.c sdl_text.c \
	../runtimes/gb/src/common/display_scale.c ../runtimes/gb/src/common/key_config.c ../runtimes/gb/src/common/string_util.c \
	../runtimes/n64/src/remote_media_ipc.c

GB_RUNTIME_FIXED_HOST_PRODUCT_HEADERS := gb_runtime_fixed_host_product_runtime.h \
	gb_runtime_fixed_host_scheduler.h gb_runtime_fixed_host_snapshot_ipc.h \
	gb_runtime_fixed_host_result_ipc.h gb_runtime_fixed_host_runtime.h media_relay_client.h \
	media_codec.h media_h264.h n64_runtime_media_stream.h sdl_text.h $(GB_ENGINE_HEADERS)

$(GB_RUNTIME_SGB2_BOOT): ../runtimes/gb/assets/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	mkdir -p $(dir $@)
	cp -f $< $@

$(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET): $(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES) $(GB_RUNTIME_FIXED_HOST_PRODUCT_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR) $(GB_RUNTIME_SGB2_BOOT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) \
		-std=gnu11 -I../runtimes/gb/src/common \
		-I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ \
		$(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES) $(SAMEBOY_LIB) \
		-lm $(SDL_LIBS) $(PLATFORM_LIBS)

gb-runtime-fixed-host-product: $(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET)

$(BUILD_DIR)/fixed_host_util_test$(EXEEXT): fixed_host_util_test.c $(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES) $(GB_RUNTIME_FIXED_HOST_PRODUCT_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -std=gnu11 -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ fixed_host_util_test.c $(filter-out integral_gb_runtime_fixed_host.c,$(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES)) $(SAMEBOY_LIB) -lm $(SDL_LIBS) $(PLATFORM_LIBS)

$(BUILD_DIR)/media_capture_test$(EXEEXT): media_capture_test.c $(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES) $(GB_RUNTIME_FIXED_HOST_PRODUCT_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -std=gnu11 -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ media_capture_test.c $(filter-out integral_gb_runtime_fixed_host.c n64_runtime_media_stream.c,$(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES)) $(SAMEBOY_LIB) -lm $(SDL_LIBS) $(PLATFORM_LIBS)

.PHONY: util-keys-test
util-keys-test: $(BUILD_DIR)/fixed_host_util_test$(EXEEXT) $(BUILD_DIR)/media_capture_test$(EXEEXT)
	$(PYTHON) ../tests/run_room_util_test.py $(BUILD_DIR)/fixed_host_util_test$(EXEEXT) $(BUILD_DIR)/media_capture_test$(EXEEXT)

gb-runtime-fixed-host-product-smoke: $(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET) ../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb ../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb
	PYTHONPATH=../src $(PYTHON) ../tests/run_gb_runtime_fixed_host_product_smoke.py \
		./$(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET) \
		../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb \
		../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb

gb-runtime-fixed-host-trade-terminal-smoke: $(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET) ../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb ../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb
	PYTHONPATH=../src $(PYTHON) ../tests/run_gb_runtime_fixed_host_trade_terminal_smoke.py \
		./$(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET) \
		../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb \
		../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb

$(GB_RUNTIME_FIXED_HOST_RUNTIME_TEST_TARGET): gb_runtime_fixed_host_runtime_test.c $(GB_RUNTIME_FIXED_HOST_CORE_SOURCES) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -I../runtimes/gb/src/common \
		-I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ \
		gb_runtime_fixed_host_runtime_test.c $(GB_RUNTIME_FIXED_HOST_CORE_SOURCES) \
		$(SAMEBOY_LIB) -lm $(PLATFORM_LIBS)

gb-runtime-fixed-host-runtime-test: $(GB_RUNTIME_FIXED_HOST_RUNTIME_TEST_TARGET) ../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb ../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb
	./$(GB_RUNTIME_FIXED_HOST_RUNTIME_TEST_TARGET) \
		../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb \
		../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb

$(GB_RUNTIME_FIXED_HOST_PRODUCT_RUNTIME_TEST_TARGET): gb_runtime_fixed_host_product_runtime_test.c gb_runtime_fixed_host_product_runtime.c gb_runtime_fixed_host_product_runtime.h ../runtimes/gb/src/server/secure_memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -o $@ gb_runtime_fixed_host_product_runtime_test.c \
		gb_runtime_fixed_host_product_runtime.c ../runtimes/gb/src/server/secure_memory.c

gb-runtime-fixed-host-product-runtime-test: $(GB_RUNTIME_FIXED_HOST_PRODUCT_RUNTIME_TEST_TARGET)
	./$(GB_RUNTIME_FIXED_HOST_PRODUCT_RUNTIME_TEST_TARGET)

$(GB_RUNTIME_FIXED_HOST_SNAPSHOT_IPC_TEST_TARGET): gb_runtime_fixed_host_snapshot_ipc_test.c gb_runtime_fixed_host_snapshot_ipc.c gb_runtime_fixed_host_snapshot_ipc.h ../runtimes/gb/src/server/secure_memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -o $@ gb_runtime_fixed_host_snapshot_ipc_test.c \
		gb_runtime_fixed_host_snapshot_ipc.c ../runtimes/gb/src/server/secure_memory.c

gb-runtime-fixed-host-snapshot-ipc-test: $(GB_RUNTIME_FIXED_HOST_SNAPSHOT_IPC_TEST_TARGET)
	./$(GB_RUNTIME_FIXED_HOST_SNAPSHOT_IPC_TEST_TARGET)

$(GB_RUNTIME_FIXED_HOST_RESULT_IPC_TEST_TARGET): gb_runtime_fixed_host_result_ipc_test.c gb_runtime_fixed_host_result_ipc.c gb_runtime_fixed_host_snapshot_ipc.c ../runtimes/gb/src/server/content_hash.c ../runtimes/gb/src/server/secure_memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -o $@ gb_runtime_fixed_host_result_ipc_test.c \
		gb_runtime_fixed_host_result_ipc.c gb_runtime_fixed_host_snapshot_ipc.c \
		../runtimes/gb/src/server/content_hash.c ../runtimes/gb/src/server/secure_memory.c

gb-runtime-fixed-host-result-ipc-test: $(GB_RUNTIME_FIXED_HOST_RESULT_IPC_TEST_TARGET)
	./$(GB_RUNTIME_FIXED_HOST_RESULT_IPC_TEST_TARGET)

$(GB_RUNTIME_FIXED_HOST_ABNORMAL_EXIT_TEST_TARGET): gb_runtime_fixed_host_abnormal_exit_test.c gb_runtime_fixed_host_product_runtime.c gb_runtime_fixed_host_product_runtime.h ../runtimes/gb/src/server/secure_memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -o $@ gb_runtime_fixed_host_abnormal_exit_test.c \
		gb_runtime_fixed_host_product_runtime.c ../runtimes/gb/src/server/secure_memory.c

gb-runtime-fixed-host-abnormal-exit-test: $(GB_RUNTIME_FIXED_HOST_ABNORMAL_EXIT_TEST_TARGET)
	./$(GB_RUNTIME_FIXED_HOST_ABNORMAL_EXIT_TEST_TARGET)

$(GB_RUNTIME_FIXED_HOST_CHILD_RUNTIME_TEST_TARGET): gb_runtime_fixed_host_child_runtime_test.c gb_runtime_fixed_host_child_runtime.c gb_runtime_fixed_host_child_runtime.h gb_runtime_fixed_host_snapshot_ipc.c gb_runtime_fixed_host_product_runtime.c $(GB_RUNTIME_FIXED_HOST_CORE_SOURCES) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -I../runtimes/gb/src/common \
		-I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ \
		gb_runtime_fixed_host_child_runtime_test.c gb_runtime_fixed_host_child_runtime.c \
		gb_runtime_fixed_host_snapshot_ipc.c gb_runtime_fixed_host_product_runtime.c $(GB_RUNTIME_FIXED_HOST_CORE_SOURCES) \
		$(SAMEBOY_LIB) -lm $(PLATFORM_LIBS)

gb-runtime-fixed-host-child-runtime-test: $(GB_RUNTIME_FIXED_HOST_CHILD_RUNTIME_TEST_TARGET) ../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb ../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb
	./$(GB_RUNTIME_FIXED_HOST_CHILD_RUNTIME_TEST_TARGET) \
		../runtimes/gb/build_exp/test-fixtures/link_serial_a.gb \
		../runtimes/gb/build_exp/test-fixtures/link_serial_b.gb

$(GB_RUNTIME_FIXED_HOST_SCHEDULER_TEST_TARGET): gb_runtime_fixed_host_scheduler_test.c gb_runtime_fixed_host_scheduler.c gb_runtime_fixed_host_scheduler.h ../runtimes/gb/src/server/secure_memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -o $@ gb_runtime_fixed_host_scheduler_test.c \
		gb_runtime_fixed_host_scheduler.c ../runtimes/gb/src/server/secure_memory.c

gb-runtime-fixed-host-scheduler-test: $(GB_RUNTIME_FIXED_HOST_SCHEDULER_TEST_TARGET)
	./$(GB_RUNTIME_FIXED_HOST_SCHEDULER_TEST_TARGET)

$(ROOM_POLL_WORKER_TEST_TARGET): room_poll_worker_test.c room_poll_worker.c room_poll_worker.h $(HTTP_SOURCES) $(HTTP_HEADERS) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ room_poll_worker_test.c room_poll_worker.c $(HTTP_SOURCES) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

room-poll-worker-test: $(ROOM_POLL_WORKER_TEST_TARGET)
	./$(ROOM_POLL_WORKER_TEST_TARGET)

$(CURRENT_API_CONTRACT_TEST_TARGET): current_api_contract_test.c $(HTTP_SOURCES) $(HTTP_HEADERS) mobile_session_contract.c mobile_session_contract.h ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ current_api_contract_test.c $(HTTP_SOURCES) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

$(BUILD_DIR)/http_payload_codec_test$(EXEEXT): http_payload_codec_test.c http_payload_codec.c http_payload_codec.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ http_payload_codec_test.c http_payload_codec.c

.PHONY: http-payload-codec-test
http-payload-codec-test: $(BUILD_DIR)/http_payload_codec_test$(EXEEXT)
	./$(BUILD_DIR)/http_payload_codec_test$(EXEEXT)

$(BUILD_DIR)/http_transport_test$(EXEEXT): http_transport_test.c http_transport.c http_transport.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ http_transport_test.c http_transport.c $(SDL_LIBS) $(PLATFORM_LIBS)

.PHONY: http-transport-test
http-transport-test: $(BUILD_DIR)/http_transport_test$(EXEEXT)
	$(PYTHON) ../tests/run_http_transport_test.py ./$(BUILD_DIR)/http_transport_test$(EXEEXT)

$(BUILD_DIR)/api_workflow_test$(EXEEXT): api_workflow_test.c $(HTTP_SOURCES) $(HTTP_HEADERS) mobile_session_contract.c mobile_session_contract.h ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ api_workflow_test.c $(HTTP_SOURCES) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

.PHONY: api-boundary-test
api-boundary-test: $(BUILD_DIR)/api_workflow_test$(EXEEXT)
	$(PYTHON) ../tests/run_api_boundary_test.py ./$(BUILD_DIR)/api_workflow_test$(EXEEXT)

current-api-contract-test: $(CURRENT_API_CONTRACT_TEST_TARGET) http-payload-codec-test
	$(PYTHON) ../tests/run_current_api_contract_test.py ./$(CURRENT_API_CONTRACT_TEST_TARGET)

$(ROOM_MATCHING_RESPONSE_TEST_TARGET): room_matching_response_test.c $(HTTP_SOURCES) $(HTTP_HEADERS) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wno-unused-function $(SDL_CFLAGS) -o $@ room_matching_response_test.c $(HTTP_SOURCES) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

room-matching-response-test: $(ROOM_MATCHING_RESPONSE_TEST_TARGET)
	./$(ROOM_MATCHING_RESPONSE_TEST_TARGET)

$(MOBILE_SESSION_CONTRACT_TEST_TARGET): mobile_session_contract_test.c mobile_session_contract.c mobile_session_contract.h ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ mobile_session_contract_test.c mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c

mobile-session-contract-test: $(MOBILE_SESSION_CONTRACT_TEST_TARGET)
	./$(MOBILE_SESSION_CONTRACT_TEST_TARGET)

$(CLIENT_CONFIG_CONTROLLER_TEST_TARGET): client_config_controller_test.c client_config.c client_config.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -o $@ client_config_controller_test.c client_config.c

client-config-controller-test: $(CLIENT_CONFIG_CONTROLLER_TEST_TARGET)
	./$(CLIENT_CONFIG_CONTROLLER_TEST_TARGET)

$(GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET): gb_runtime_fixed_host_rom_resolver_test.c gb_runtime_fixed_host_rom_resolver.c gb_runtime_fixed_host_rom_resolver.h rom_metadata.c rom_metadata.h client_config.h ../runtimes/gb/src/server/content_hash.c ../runtimes/gb/src/common/utf8_file.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../runtimes/gb/src/server -o $@ gb_runtime_fixed_host_rom_resolver_test.c gb_runtime_fixed_host_rom_resolver.c rom_metadata.c ../runtimes/gb/src/server/content_hash.c

gb-runtime-fixed-host-rom-resolver-test: $(GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET)
	work_dir=$$(mktemp -d) && cd "$$work_dir" && "$(abspath $(GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET))"

$(ROM_METADATA_TEST_TARGET): rom_metadata_test.c rom_metadata.c rom_metadata.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ rom_metadata_test.c rom_metadata.c

rom-metadata-test: $(ROM_METADATA_TEST_TARGET)
	./$(ROM_METADATA_TEST_TARGET)

$(CREDENTIAL_STORE_TEST_TARGET): credential_store_test.c credential_store.c credential_store.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -DINTEGRAL_CREDENTIAL_STORE_LINUX_TEST \
		-o $@ credential_store_test.c credential_store.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

credential-store-test: $(CREDENTIAL_STORE_TEST_TARGET)
	./$(CREDENTIAL_STORE_TEST_TARGET)

.PHONY: account-test
$(BUILD_DIR)/client_account_test$(EXEEXT): client_account_test.c client_account.c client_account.h client_config.c client_config.h credential_store.h http_client.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ client_account_test.c client_account.c client_config.c

account-test: $(BUILD_DIR)/client_account_test$(EXEEXT)
	work_dir=$$(mktemp -d); ./$(BUILD_DIR)/client_account_test$(EXEEXT) $$work_dir; status=$$?; rm -rf $$work_dir; exit $$status

.PHONY: user-config-test
$(BUILD_DIR)/client_user_config_test$(EXEEXT): client_user_config_test.c client_user_config.c client_user_config.h client_rom_catalog.c client_rom_catalog.h client_config.c client_config.h rom_metadata.c rom_metadata.h http_client.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ client_user_config_test.c client_user_config.c client_rom_catalog.c client_config.c rom_metadata.c

user-config-test: $(BUILD_DIR)/client_user_config_test$(EXEEXT)
	work_dir=$$(mktemp -d) && ./$(BUILD_DIR)/client_user_config_test$(EXEEXT) "$$work_dir"

.PHONY: key-editor-test
$(BUILD_DIR)/client_key_config_test$(EXEEXT): client_key_config_test.c client_key_config.c client_key_config.h client_config.c client_config.h ../runtimes/gb/src/common/key_config.c ../runtimes/gb/src/common/key_config.h ../runtimes/gb/src/common/string_util.c ../runtimes/gb/src/common/string_util.h ../runtimes/gb/src/common/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ client_key_config_test.c client_key_config.c client_config.c ../runtimes/gb/src/common/key_config.c ../runtimes/gb/src/common/string_util.c $(SDL_LIBS)

key-editor-test: $(BUILD_DIR)/client_key_config_test$(EXEEXT)
	work_dir=$$(mktemp -d) && ./$(BUILD_DIR)/client_key_config_test$(EXEEXT) "$$work_dir"

.PHONY: rom-editor-test
$(BUILD_DIR)/client_rom_editor_test$(EXEEXT): client_rom_editor_test.c client_rom_editor.c client_rom_editor.h client_rom_catalog.c client_rom_catalog.h client_config.c client_config.h rom_metadata.c rom_metadata.h http_client.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ client_rom_editor_test.c client_rom_editor.c client_rom_catalog.c client_config.c rom_metadata.c $(SDL_LIBS)

rom-editor-test: $(BUILD_DIR)/client_rom_editor_test$(EXEEXT)
	work_dir=$$(mktemp -d) && ./$(BUILD_DIR)/client_rom_editor_test$(EXEEXT) "$$work_dir"

.PHONY: rom-registration-test
.PHONY: export-test
export-test: $(BUILD_DIR)/client_export_test$(EXEEXT)
	work_dir=$$(mktemp -d) && ./$(BUILD_DIR)/client_export_test$(EXEEXT) "$$work_dir"

$(BUILD_DIR)/client_export_test$(EXEEXT): client_export_test.c client_file_io.c client_file_io.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ client_export_test.c client_file_io.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

$(BUILD_DIR)/client_rom_registration_test$(EXEEXT): client_rom_registration_test.c client_rom_registration.c client_rom_registration.h client_rom_editor.c client_rom_editor.h client_rom_catalog.c client_rom_catalog.h client_file_io.c client_file_io.h client_config.c client_config.h rom_metadata.c rom_metadata.h http_client.h ../runtimes/gb/src/server/slot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -o $@ client_rom_registration_test.c client_rom_registration.c client_rom_editor.c client_rom_catalog.c client_file_io.c client_config.c rom_metadata.c $(SDL_LIBS) $(PLATFORM_LIBS)

rom-registration-test: $(BUILD_DIR)/client_rom_registration_test$(EXEEXT)
	work_dir=$$(mktemp -d) && mkdir -p "$$work_dir/roms" && cd "$$work_dir" && "$(abspath $(BUILD_DIR)/client_rom_registration_test$(EXEEXT))" "$$work_dir"

.PHONY: gb-local-launch-test
$(BUILD_DIR)/client_launch_gb_local_test$(EXEEXT): client_launch_gb_local_test.c client_launch_gb_local.c client_launch_gb_local.h client_save_sync.h client_config.h client_config.c http_client.h client_file_io.c client_file_io.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ client_launch_gb_local_test.c client_launch_gb_local.c client_config.c client_file_io.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

gb-local-launch-test: $(BUILD_DIR)/client_launch_gb_local_test$(EXEEXT)
	work_dir=$$(mktemp -d) && ./$(BUILD_DIR)/client_launch_gb_local_test$(EXEEXT) "$$work_dir"

.PHONY: gb-mobile-launch-test
$(BUILD_DIR)/client_launch_gb_mobile_test$(EXEEXT): client_launch_gb_mobile_test.c client_launch_gb_mobile.c client_launch_gb_mobile.h client_config.h client_save_sync.h http_client.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ client_launch_gb_mobile_test.c client_launch_gb_mobile.c

gb-mobile-launch-test: $(BUILD_DIR)/client_launch_gb_mobile_test$(EXEEXT)
	work_dir=$$(mktemp -d) && ./$(BUILD_DIR)/client_launch_gb_mobile_test$(EXEEXT) "$$work_dir"

.PHONY: launch-prepare-test
$(BUILD_DIR)/client_launch_prepare_test$(EXEEXT): client_launch_prepare_test.c client_launch_gb_mobile_prepare.c client_launch_n64_local.c client_launch_gb_mobile.h client_launch_n64_local.h client_file_io.h client_save_sync.h client_config.h http_client.h mobile_session_contract.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ client_launch_prepare_test.c client_launch_gb_mobile_prepare.c client_launch_n64_local.c

launch-prepare-test: $(BUILD_DIR)/client_launch_prepare_test$(EXEEXT)
	test_binary="$(CURDIR)/$(BUILD_DIR)/client_launch_prepare_test$(EXEEXT)"; work_dir=$$(mktemp -d) && cd "$$work_dir" && "$$test_binary"

.PHONY: save-sync-test
$(BUILD_DIR)/client_save_sync_test$(EXEEXT): client_save_sync_test.c client_save_sync.c client_save_outbox.c client_file_io.c client_save_sync.h client_save_outbox.h client_file_io.h client_config.h http_client.h mobile_session_contract.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ client_save_sync_test.c client_save_sync.c client_save_outbox.c client_file_io.c ../runtimes/gb/src/server/content_hash.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

save-sync-test: $(BUILD_DIR)/client_save_sync_test$(EXEEXT)
	@set -e; test_binary="$(CURDIR)/$(BUILD_DIR)/client_save_sync_test$(EXEEXT)"; for scenario in storage inflight monitor-local monitor-mobile monitor-reject monitor-upload-fail monitor-complete-fail monitor-withheld monitor-stop-fail monitor-expired monitor-lost; do work_dir=$$(mktemp -d); (cd "$$work_dir" && "$$test_binary" "$$scenario"); done

.PHONY: save-fault-test
$(BUILD_DIR)/client_save_fault_test$(EXEEXT): client_save_fault_test.c client_save_sync.c client_save_outbox.c client_file_io.c $(HTTP_SOURCES) $(CLIENT_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ client_save_fault_test.c client_save_sync.c client_save_outbox.c client_file_io.c $(HTTP_SOURCES) mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(OPENSSL_LIBS) $(PLATFORM_LIBS)

save-fault-test: $(BUILD_DIR)/client_save_fault_test$(EXEEXT)
	python3 ../tests/run_save_fault_test.py $(BUILD_DIR)/client_save_fault_test$(EXEEXT)

.PHONY: room-state-test
$(BUILD_DIR)/client_room_test$(EXEEXT): client_room_test.c $(SOURCES) $(CLIENT_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(PNG_CFLAGS) -std=gnu11 -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ client_room_test.c $(filter-out login_client.c n64_runtime_media_stream.c,$(SOURCES)) $(SAMEBOY_LIB) -lm $(SDL_LIBS) $(PNG_LIBS) $(PLATFORM_LIBS)

room-state-test: $(BUILD_DIR)/client_room_test$(EXEEXT)
	./$(BUILD_DIR)/client_room_test$(EXEEXT)
	SDL_VIDEODRIVER=dummy ./$(BUILD_DIR)/client_room_test$(EXEEXT) --focus-return
	$(PYTHON) ../tests/run_room_util_test.py --n64 $(BUILD_DIR)/client_room_test$(EXEEXT)
ifneq ($(EXEEXT),.exe)
	INTEGRAL_ROOM_TEST_BINARY=$(abspath $(BUILD_DIR)/client_room_test$(EXEEXT)) $(PYTHON) ../tests/test_round7_room_http.py
endif

.PHONY: n64-transfer-memory-test
n64-transfer-memory-test:
	$(MAKE) -C ../runtimes/n64 transfer-memory-test

$(BUILD_DIR)/client_n64_memory_test$(EXEEXT): client_n64_memory_test.c $(SOURCES) $(CLIENT_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(PNG_CFLAGS) -std=gnu11 -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ client_n64_memory_test.c $(filter-out login_client.c client_room_n64.c,$(SOURCES)) $(SAMEBOY_LIB) -lm $(SDL_LIBS) $(PNG_LIBS) $(PLATFORM_LIBS)

n64-client-memory-test: $(BUILD_DIR)/client_n64_memory_test$(EXEEXT)
	./$(BUILD_DIR)/client_n64_memory_test$(EXEEXT)

smoke: $(TARGET) n64-transfer-memory-test n64-client-memory-test
	$(MAKE) room-state-test
	$(MAKE) save-sync-test
	$(MAKE) launch-prepare-test
	$(MAKE) gb-mobile-launch-test
	$(MAKE) gb-local-launch-test
	$(MAKE) screenshots-test
	$(MAKE) util-keys-test
	$(MAKE) rom-registration-test
	$(MAKE) rom-editor-test
	$(MAKE) key-editor-test
	$(MAKE) user-config-test
	$(MAKE) account-test
	$(MAKE) client-config-controller-test
	$(MAKE) credential-store-test
	$(MAKE) current-api-contract-test
	$(MAKE) room-matching-response-test
	$(MAKE) mobile-session-contract-test
	$(MAKE) gb-runtime-fixed-host-rom-resolver-test
ifeq ($(EXEEXT),.exe)
	$(MAKE) windows-boundary-test
	@echo "SKIP log permission smoke: POSIX permissions do not apply on Windows"
else
	work_dir=$$(mktemp -d); ./$(TARGET) --log-permission-smoke $$work_dir; status=$$?; rm -rf $$work_dir; exit $$status
endif
	work_dir=$$(mktemp -d); ./$(TARGET) --outbox-smoke $$work_dir; status=$$?; rm -rf $$work_dir; exit $$status
	work_dir=$$(mktemp -d); ./$(TARGET) --runtime-path-cleanup-smoke $$work_dir; status=$$?; rm -rf $$work_dir; exit $$status
	work_dir=$$(mktemp -d); ./$(TARGET) --local-navigation-smoke --config $$work_dir/client.conf; status=$$?; rm -rf $$work_dir; exit $$status
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screenshot $(BUILD_DIR)/login_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen password-change --screenshot $(BUILD_DIR)/password_change_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen main --screenshot $(BUILD_DIR)/main_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen room-mode --screenshot $(BUILD_DIR)/room_mode_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen join-room --screenshot $(BUILD_DIR)/join_room_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen local-mode --screenshot $(BUILD_DIR)/local_mode_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen local --screenshot $(BUILD_DIR)/local_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen gb-mobile --screenshot $(BUILD_DIR)/gb_mobile_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen n64_runtime --screenshot $(BUILD_DIR)/n64_runtime_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen room --screenshot $(BUILD_DIR)/room_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen n64-room --screenshot $(BUILD_DIR)/n64_room_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen n64-room-ready-error --screenshot $(BUILD_DIR)/n64_room_ready_error_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen keys --screenshot $(BUILD_DIR)/keys_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen rom --screenshot $(BUILD_DIR)/rom_smoke.bmp
	SDL_VIDEODRIVER=dummy ./$(TARGET) --smoke-test --config $(BUILD_DIR)/smoke-empty.conf --screen rom-browser --screenshot $(BUILD_DIR)/rom_browser_smoke.bmp
	./$(TARGET) --config-smoke $(BUILD_DIR)/slots_smoke.conf

clean:
	rm -rf $(BUILD_DIR)
