CC ?= cc
PKG_CONFIG ?= pkg-config
PYTHON ?= python3
WINDRES ?= windres
CFLAGS ?= -O2 -Wall -Wextra -Werror
CFLAGS += $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)
HAS_SDL_TTF := $(shell $(PKG_CONFIG) --exists SDL2_ttf 2>/dev/null && echo 1)
SDL_TTF_CFLAGS := $(shell $(PKG_CONFIG) --cflags SDL2_ttf 2>/dev/null)
SDL_TTF_LIBS := $(shell $(PKG_CONFIG) --libs-only-L SDL2_ttf 2>/dev/null) -lSDL2_ttf
HAS_OPENSSL := $(shell $(PKG_CONFIG) --exists openssl 2>/dev/null && echo 1)
OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)
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
SOURCES := login_client.c sdl_text.c sdl_unicode_text.c http_client.c client_config.c credential_store.c rom_metadata.c \
	media_relay_client.c media_codec.c media_h264.c n64_runtime_media_stream.c room_poll_worker.c \
	mobile_session_contract.c gb_runtime_fixed_host_rom_resolver.c gb_runtime_fixed_host_runtime.c gb_runtime_fixed_host_product_runtime.c gb_runtime_fixed_host_snapshot_ipc.c gb_runtime_fixed_host_result_ipc.c gb_runtime_fixed_host_child_runtime.c \
	../runtimes/gb/src/server/gb_link_engine.c ../runtimes/gb/src/server/secure_memory.c ../runtimes/gb/src/server/content_hash.c \
	../runtimes/gb/src/server/rom_profile.c ../runtimes/gb/src/server/sgb_boot_resource.c ../runtimes/gb/src/server/slot.c \
	../runtimes/gb/src/serial/serial_peripheral.c ../runtimes/gb/src/common/file_util.c ../runtimes/gb/src/common/net_compat.c \
	../runtimes/gb/src/common/key_config.c ../runtimes/gb/src/common/string_util.c \
	../runtimes/n64/src/remote_media_ipc.c

SAMEBOY_HEADERS := $(wildcard $(SAMEBOY_DIR)/Core/*.h)
GB_ENGINE_HEADERS := ../runtimes/gb/src/common/file_util.h ../runtimes/gb/src/common/key_config.h \
	../runtimes/gb/src/common/net_compat.h ../runtimes/gb/src/common/protocol.h \
	../runtimes/gb/src/common/string_util.h ../runtimes/gb/src/server/content_hash.h \
	../runtimes/gb/src/server/gb_link_engine.h ../runtimes/gb/src/server/rom_profile.h \
	../runtimes/gb/src/server/secure_memory.h ../runtimes/gb/src/server/sgb_boot_resource.h \
	../runtimes/gb/src/server/slot.h ../runtimes/gb/src/serial/serial_peripheral.h \
	../runtimes/n64/src/remote_media_ipc.h $(SAMEBOY_HEADERS)
CLIENT_HEADERS := client_config.h credential_store.h http_client.h mobile_session_contract.h \
	room_poll_worker.h media_relay_client.h n64_runtime_media_stream.h media_codec.h media_h264.h \
	rom_metadata.h sdl_text.h sdl_unicode_text.h gb_runtime_fixed_host_rom_resolver.h \
	gb_runtime_fixed_host_runtime.h gb_runtime_fixed_host_product_runtime.h \
	gb_runtime_fixed_host_snapshot_ipc.h gb_runtime_fixed_host_result_ipc.h \
	gb_runtime_fixed_host_child_runtime.h $(GB_ENGINE_HEADERS)

.PHONY: all gb-runtime-fixed-host-product gb-runtime-fixed-host-product-smoke gb-runtime-fixed-host-trade-terminal-smoke gb-runtime-fixed-host-runtime-test gb-runtime-fixed-host-product-runtime-test gb-runtime-fixed-host-snapshot-ipc-test gb-runtime-fixed-host-result-ipc-test gb-runtime-fixed-host-scheduler-test gb-runtime-fixed-host-rom-resolver-test rom-metadata-test client-config-controller-test clean smoke media-relay-smoke media-codec-test media-foundation-probe media-h264-test media-h264-interop room-poll-worker-test current-api-contract-test room-matching-response-test mobile-session-contract-test n64-runtime-stop-process-test

all: $(TARGET) $(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCES) $(CLIENT_HEADERS) $(SAMEBOY_LIB) $(WINDOWS_CLIENT_RESOURCE) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -std=gnu11 -I../runtimes/gb/src/common -I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ $(SOURCES) $(WINDOWS_CLIENT_RESOURCE) $(SAMEBOY_LIB) -lm $(SDL_LIBS) $(PLATFORM_LIBS)

ifneq ($(WINDOWS_CLIENT_RESOURCE),)
build/integral_emulator_icon.ico: $(INTEGRAL_EMULATOR_ICON_ICO) ../scripts/verify_app_icon_assets.py | $(BUILD_DIR)
	$(PYTHON) ../scripts/verify_app_icon_assets.py "$(INTEGRAL_EMULATOR_ICON_ICO)" --format ico
	cp -f "$(INTEGRAL_EMULATOR_ICON_ICO)" $@

$(WINDOWS_CLIENT_RESOURCE): windows_launcher.rc build/integral_emulator_icon.ico | $(BUILD_DIR)
	$(WINDRES) windows_launcher.rc -O coff -o $@
endif

$(SAMEBOY_LIB):
	$(MAKE) -C $(SAMEBOY_DIR) lib \
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
	../runtimes/gb/src/common/key_config.c ../runtimes/gb/src/common/string_util.c \
	../runtimes/n64/src/remote_media_ipc.c

GB_RUNTIME_FIXED_HOST_PRODUCT_HEADERS := gb_runtime_fixed_host_product_runtime.h \
	gb_runtime_fixed_host_scheduler.h gb_runtime_fixed_host_snapshot_ipc.h \
	gb_runtime_fixed_host_result_ipc.h gb_runtime_fixed_host_runtime.h media_relay_client.h \
	media_codec.h media_h264.h n64_runtime_media_stream.h sdl_text.h $(GB_ENGINE_HEADERS)

$(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET): $(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES) $(GB_RUNTIME_FIXED_HOST_PRODUCT_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) \
		-std=gnu11 -I../runtimes/gb/src/common \
		-I../runtimes/gb/third_party/SameBoy/Core -Wno-multichar -o $@ \
		$(GB_RUNTIME_FIXED_HOST_PRODUCT_SOURCES) $(SAMEBOY_LIB) \
		-lm $(SDL_LIBS) $(PLATFORM_LIBS)

gb-runtime-fixed-host-product: $(GB_RUNTIME_FIXED_HOST_PRODUCT_TARGET)

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

$(ROOM_POLL_WORKER_TEST_TARGET): room_poll_worker_test.c room_poll_worker.c room_poll_worker.h http_client.c http_client.h mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ room_poll_worker_test.c room_poll_worker.c http_client.c mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

room-poll-worker-test: $(ROOM_POLL_WORKER_TEST_TARGET)
	./$(ROOM_POLL_WORKER_TEST_TARGET)

$(CURRENT_API_CONTRACT_TEST_TARGET): current_api_contract_test.c http_client.c http_client.h mobile_session_contract.c mobile_session_contract.h ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ current_api_contract_test.c http_client.c mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

current-api-contract-test: $(CURRENT_API_CONTRACT_TEST_TARGET)
	$(PYTHON) ../tests/run_current_api_contract_test.py ./$(CURRENT_API_CONTRACT_TEST_TARGET)

$(ROOM_MATCHING_RESPONSE_TEST_TARGET): room_matching_response_test.c http_client.c http_client.h mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wno-unused-function $(SDL_CFLAGS) -o $@ room_matching_response_test.c http_client.c mobile_session_contract.c ../runtimes/gb/src/server/content_hash.c $(SDL_LIBS) $(PLATFORM_LIBS)

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

$(GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET): gb_runtime_fixed_host_rom_resolver_test.c gb_runtime_fixed_host_rom_resolver.c gb_runtime_fixed_host_rom_resolver.h rom_metadata.c rom_metadata.h client_config.h ../runtimes/gb/src/server/content_hash.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../runtimes/gb/src/server -o $@ gb_runtime_fixed_host_rom_resolver_test.c gb_runtime_fixed_host_rom_resolver.c rom_metadata.c ../runtimes/gb/src/server/content_hash.c

gb-runtime-fixed-host-rom-resolver-test: $(GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET)
	./$(GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_TEST_TARGET)

$(ROM_METADATA_TEST_TARGET): rom_metadata_test.c rom_metadata.c rom_metadata.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ rom_metadata_test.c rom_metadata.c

rom-metadata-test: $(ROM_METADATA_TEST_TARGET)
	./$(ROM_METADATA_TEST_TARGET)

smoke: $(TARGET)
	$(MAKE) client-config-controller-test
	$(MAKE) current-api-contract-test
	$(MAKE) room-matching-response-test
	$(MAKE) mobile-session-contract-test
	$(MAKE) gb-runtime-fixed-host-rom-resolver-test
	work_dir=$$(mktemp -d); ./$(TARGET) --log-permission-smoke $$work_dir; status=$$?; rm -rf $$work_dir; exit $$status
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
