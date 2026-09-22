CC ?= cc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Werror
CFLAGS += $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)
LDFLAGS ?=
PLATFORM := $(shell uname -s)
WINDRES ?= windres
EXEEXT :=
WINDOWS_BUILD :=
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(PLATFORM)),)
EXEEXT := .exe
WINDOWS_BUILD := 1
CFLAGS += -Wno-format-truncation
endif

BUILD_DIR := ../build_exp
APP_BIN := $(BUILD_DIR)/integral_gb_runtime_frontend$(EXEEXT)
SERVER_BIN := $(BUILD_DIR)/integral_gb_runtime_dual_server$(EXEEXT)
GB_LINK_ENGINE_DETERMINISM_TEST_BIN := $(BUILD_DIR)/gb_link_engine_determinism_test$(EXEEXT)
SECURE_MEMORY_TEST_BIN := $(BUILD_DIR)/secure_memory_test$(EXEEXT)
GB_LINK_ENGINE_ZEROIZATION_TEST_BIN := $(BUILD_DIR)/gb_link_engine_zeroization_test$(EXEEXT)
GB_LINK_ENGINE_SAVE_PREFLIGHT_TEST_BIN := $(BUILD_DIR)/gb_link_engine_save_preflight_test$(EXEEXT)
ROM_PROFILE_TEST_BIN := $(BUILD_DIR)/rom_profile_test$(EXEEXT)
SGB_FIXTURE_ROM := $(BUILD_DIR)/test-fixtures/sgb_color_test.gb
LINK_SERIAL_FIXTURE_A := $(BUILD_DIR)/test-fixtures/link_serial_a.gb
LINK_SERIAL_FIXTURE_B := $(BUILD_DIR)/test-fixtures/link_serial_b.gb
LINK_IR_FIXTURE_A := $(BUILD_DIR)/test-fixtures/link_ir_a.gbc
LINK_IR_FIXTURE_B := $(BUILD_DIR)/test-fixtures/link_ir_b.gbc
LINK_RTC_FIXTURE := $(BUILD_DIR)/test-fixtures/link_rtc_boundary.gbc
SGB_SLOT_TEST_BIN := $(BUILD_DIR)/slot_sgb_test$(EXEEXT)
SGB_BOOT_RESOURCE_TEST_BIN := $(BUILD_DIR)/sgb_boot_resource_test$(EXEEXT)
LOG_CONFIG_TEST_BIN := $(BUILD_DIR)/log_config_test$(EXEEXT)
KEY_CONFIG_CONTROLLER_TEST_BIN := $(BUILD_DIR)/key_config_controller_test$(EXEEXT)
VIDEO_WINDOW_EXIT_CONFIRMATION_TEST_BIN := $(BUILD_DIR)/video_window_exit_confirmation_test$(EXEEXT)
SERVER_OPTIONS_TEST_BIN := $(BUILD_DIR)/server_options_test$(EXEEXT)
INPUT_ROUTER_AUTO_A_TEST_BIN := $(BUILD_DIR)/input_router_auto_a_test$(EXEEXT)
DISPLAY_SCALE_TEST_BIN := $(BUILD_DIR)/display_scale_test$(EXEEXT)
MENU_CONFIG_DISPLAY_SCALE_TEST_BIN := $(BUILD_DIR)/menu_config_display_scale_test$(EXEEXT)
SERIAL_PERIPHERAL_TEST_BIN := $(BUILD_DIR)/serial_peripheral_test$(EXEEXT)
SAMEBOY_DIR := ../third_party/SameBoy
SAMEBOY_LIB := $(SAMEBOY_DIR)/build/lib/libsameboy.a
GB_RUNTIME_CPPFLAGS := -Icommon -I$(SAMEBOY_DIR)/Core
GB_RUNTIME_LDFLAGS := $(SAMEBOY_LIB) -lm
SDL2_CONFIG ?= sdl2-config
SDL2_CFLAGS := $(shell $(SDL2_CONFIG) --cflags)
SDL2_LIBS := $(shell $(SDL2_CONFIG) --libs)
GB_RUNTIME_PLATFORM_LIBS :=
APP_RESOURCE_OBJECTS :=
INTEGRAL_EMULATOR_ICON_ICO ?= ../../../assets/public/integral_emulator_icon.ico
GB_LINK_ENGINE_DETERMINISM_CFLAGS := $(CFLAGS)
ifneq ($(filter Linux MINGW% MSYS% CYGWIN%,$(PLATFORM)),)
# SameBoy's internal save-state header intentionally uses four-character
# constants. GCC diagnoses those under -Wall on Linux and Windows; keep project
# warnings fatal and suppress only this third-party representation warning for
# the Twin PoC.
GB_LINK_ENGINE_DETERMINISM_CFLAGS += -Wno-multichar
endif
ifeq ($(WINDOWS_BUILD),1)
GB_RUNTIME_PLATFORM_LIBS += -lws2_32 -liphlpapi
APP_RESOURCE_OBJECTS += $(BUILD_DIR)/gb_runtime.res.o
endif

COMMON_HEADERS := ../../common/window_focus.h common/display_scale.h common/file_util.h common/utf8_file.h common/key_config.h common/log_util.h common/mdns.h common/net_compat.h common/parse_util.h common/process_util.h common/protocol.h common/sdl_text.h common/string_util.h
COMMON_SOURCES := common/display_scale.c common/file_util.c common/key_config.c common/log_util.c common/mdns.c common/net_compat.c common/parse_util.c common/process_util.c common/sdl_text.c common/string_util.c
APP_HEADERS := app/choice_list.h app/key_config_menu.h app/menu_config.h app/menu_draw.h app/menu_paths.h app/menu_state.h app/screenshot_viewer.h
APP_RESOURCES := app/windows_resource.rc $(BUILD_DIR)/integral_emulator_icon.ico ../../../scripts/verify_app_icon_assets.py
SERVER_HEADERS := ../../common/screenshot.h ../../common/screenshot_path.h  server/audio_player.h server/input_router.h server/lan_remote.h server/link_bridge.h server/rom_profile.h server/screenshot.h server/server_cleanup.h server/server_options.h server/sgb_boot_resource.h server/slot.h server/slot2_upload_wait.h server/stream_server.h server/video_window.h server/wait_window.h serial/serial_peripheral.h
SERVER_SOURCES := server/main.c server/audio_player.c server/input_router.c server/lan_remote.c server/link_bridge.c server/rom_profile.c server/screenshot.c server/server_cleanup.c server/server_options.c server/sgb_boot_resource.c server/slot.c server/slot2_upload_wait.c server/stream_server.c server/video_window.c server/wait_window.c serial/serial_peripheral.c
APP_SOURCES := app/main.c app/choice_list.c app/key_config_menu.c app/menu_config.c app/menu_draw.c app/menu_paths.c app/menu_state.c app/screenshot_viewer.c

.PHONY: all app server mobile-runtime secure-memory-test gb-link-engine-zeroization-test gb-link-engine-save-preflight-test gb-link-engine-determinism-test gb-link-engine-determinism-smoke gb-link-engine-determinism-serial-smoke gb-link-engine-determinism-ir-smoke gb-link-engine-determinism-rtc-smoke rom-profile-test serial-peripheral-test sgb-fixture sgb-slot-test sgb-boot-resource-test log-config-test key-config-controller-test display-scale-test menu-config-display-scale-test video-window-exit-confirmation-test server-options-test input-router-auto-a-test clean

all: app server

app: $(APP_BIN)

server: $(SERVER_BIN)

mobile-runtime:
	$(MAKE) -C mobile runtime $(if $(LIBMOBILE_ROOT),LIBMOBILE_ROOT="$(LIBMOBILE_ROOT)") $(if $(LIBMOBILE_BUILD),LIBMOBILE_BUILD="$(LIBMOBILE_BUILD)")

secure-memory-test: $(SECURE_MEMORY_TEST_BIN)
	$(SECURE_MEMORY_TEST_BIN)

gb-link-engine-zeroization-test: $(GB_LINK_ENGINE_ZEROIZATION_TEST_BIN) $(LINK_SERIAL_FIXTURE_A) $(LINK_SERIAL_FIXTURE_B)
	$(GB_LINK_ENGINE_ZEROIZATION_TEST_BIN) $(LINK_SERIAL_FIXTURE_A) $(LINK_SERIAL_FIXTURE_B)

gb-link-engine-save-preflight-test: $(GB_LINK_ENGINE_SAVE_PREFLIGHT_TEST_BIN) $(LINK_RTC_FIXTURE) $(LINK_SERIAL_FIXTURE_A) $(SGB_FIXTURE_ROM)
	$(GB_LINK_ENGINE_SAVE_PREFLIGHT_TEST_BIN) $(LINK_RTC_FIXTURE) $(LINK_SERIAL_FIXTURE_A) $(SGB_FIXTURE_ROM)


gb-link-engine-determinism-test: $(GB_LINK_ENGINE_DETERMINISM_TEST_BIN)

gb-link-engine-determinism-smoke: $(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) $(SGB_FIXTURE_ROM) $(BUILD_DIR)/bootroms/sgb2_boot.bin
	$(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) --rom-a $(SGB_FIXTURE_ROM) --rom-b $(SGB_FIXTURE_ROM) --frames 120 --checkpoint 60 --repetitions 2 --render-profile cross

gb-link-engine-determinism-serial-smoke: $(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) $(LINK_SERIAL_FIXTURE_A) $(LINK_SERIAL_FIXTURE_B)
	$(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) --rom-a $(LINK_SERIAL_FIXTURE_A) --rom-b $(LINK_SERIAL_FIXTURE_B) --frames 120 --checkpoint-list 1,60,120 --repetitions 2 --render-profile cross --require-serial-events

gb-link-engine-determinism-ir-smoke: $(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) $(LINK_IR_FIXTURE_A) $(LINK_IR_FIXTURE_B)
	$(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) --rom-a $(LINK_IR_FIXTURE_A) --rom-b $(LINK_IR_FIXTURE_B) --frames 120 --checkpoint-list 1,60,120 --repetitions 2 --render-profile cross --require-ir-events

gb-link-engine-determinism-rtc-smoke: $(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) $(LINK_RTC_FIXTURE)
	$(GB_LINK_ENGINE_DETERMINISM_TEST_BIN) --rom-a $(LINK_RTC_FIXTURE) --rom-b $(LINK_RTC_FIXTURE) --frames 600 --checkpoint-list 1,300,600 --repetitions 2 --render-profile cross --require-rtc-day-at-least 1

rom-profile-test: $(ROM_PROFILE_TEST_BIN)
	$(ROM_PROFILE_TEST_BIN)

serial-peripheral-test: $(SERIAL_PERIPHERAL_TEST_BIN)
	$(SERIAL_PERIPHERAL_TEST_BIN)

sgb-fixture: $(SGB_FIXTURE_ROM)

sgb-slot-test: $(SGB_SLOT_TEST_BIN) $(SGB_FIXTURE_ROM) $(BUILD_DIR)/bootroms/sgb2_boot.bin
	$(SGB_SLOT_TEST_BIN) $(SGB_FIXTURE_ROM)

sgb-boot-resource-test: $(SGB_BOOT_RESOURCE_TEST_BIN) $(BUILD_DIR)/bootroms/sgb2_boot.bin
	$(SGB_BOOT_RESOURCE_TEST_BIN) $(BUILD_DIR)/bootroms/sgb2_boot.bin $(BUILD_DIR)/bootroms/corrupt-test.bin

log-config-test: $(LOG_CONFIG_TEST_BIN)
	mkdir -p $(BUILD_DIR)/log-config-test-runtime/config
	$(RM) $(BUILD_DIR)/log-config-test-runtime/config/gb_runtime_log.conf
	printf 'enabled=1\n' > $(BUILD_DIR)/log-config-test-runtime/config/gb_runtime_log.conf
	cd $(BUILD_DIR)/log-config-test-runtime && ../log_config_test enabled
	printf 'enabled=0\n' > $(BUILD_DIR)/log-config-test-runtime/config/gb_runtime_log.conf
	cd $(BUILD_DIR)/log-config-test-runtime && ../log_config_test disabled

key-config-controller-test: $(KEY_CONFIG_CONTROLLER_TEST_BIN)
	$(KEY_CONFIG_CONTROLLER_TEST_BIN)

display-scale-test: $(DISPLAY_SCALE_TEST_BIN)
	$(DISPLAY_SCALE_TEST_BIN)

menu-config-display-scale-test: $(MENU_CONFIG_DISPLAY_SCALE_TEST_BIN)
	$(MENU_CONFIG_DISPLAY_SCALE_TEST_BIN)

video-window-exit-confirmation-test: $(VIDEO_WINDOW_EXIT_CONFIRMATION_TEST_BIN)
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $(VIDEO_WINDOW_EXIT_CONFIRMATION_TEST_BIN)

$(VIDEO_WINDOW_EXIT_CONFIRMATION_TEST_BIN): ../../common/window_focus.h

server-options-test: $(SERVER_OPTIONS_TEST_BIN)
	$(SERVER_OPTIONS_TEST_BIN)

input-router-auto-a-test: $(INPUT_ROUTER_AUTO_A_TEST_BIN)
	$(INPUT_ROUTER_AUTO_A_TEST_BIN)

.PHONY: screenshot-test
screenshot-test: $(BUILD_DIR)/screenshot_test
	python3 ../../../tests/run_util_capture_test.py $(BUILD_DIR)/screenshot_test

$(BUILD_DIR)/server2_util_test$(EXEEXT): server/server2_util_test.c $(SERVER_SOURCES) $(SERVER_HEADERS) $(COMMON_SOURCES) $(COMMON_HEADERS) $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) server/server2_util_test.c $(filter-out server/main.c,$(SERVER_SOURCES)) $(COMMON_SOURCES) $(LDFLAGS) $(SAMEBOY_LIB) -lm $(SDL2_LIBS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(BUILD_DIR)/screenshot_test: server/screenshot_test.c server/screenshot.c server/screenshot.h ../../common/screenshot.h ../../common/screenshot_path.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) server/screenshot_test.c server/screenshot.c $(LDFLAGS) $(SDL2_LIBS) -o $@

$(APP_BIN): $(APP_SOURCES) $(APP_HEADERS) $(COMMON_SOURCES) $(COMMON_HEADERS) $(APP_RESOURCE_OBJECTS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) $(APP_SOURCES) $(COMMON_SOURCES) $(APP_RESOURCE_OBJECTS) $(LDFLAGS) $(SDL2_LIBS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(BUILD_DIR)/gb_runtime.res.o: $(APP_RESOURCES) | $(BUILD_DIR)
	$(WINDRES) app/windows_resource.rc -O coff -o $@

$(BUILD_DIR)/integral_emulator_icon.ico: $(INTEGRAL_EMULATOR_ICON_ICO) ../../../scripts/verify_app_icon_assets.py | $(BUILD_DIR)
	python3 ../../../scripts/verify_app_icon_assets.py "$(INTEGRAL_EMULATOR_ICON_ICO)" --format ico
	cp -f "$(INTEGRAL_EMULATOR_ICON_ICO)" $@

$(SERVER_BIN): $(SERVER_SOURCES) $(COMMON_SOURCES) $(SERVER_HEADERS) $(COMMON_HEADERS) $(SAMEBOY_LIB) $(BUILD_DIR)/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) $(SERVER_SOURCES) $(COMMON_SOURCES) $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) $(SDL2_LIBS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(GB_LINK_ENGINE_DETERMINISM_TEST_BIN): server/gb_link_engine_determinism_test.c server/gb_link_engine.c server/gb_link_engine.h server/secure_memory.c server/secure_memory.h server/content_hash.c server/content_hash.h server/rom_profile.c server/rom_profile.h server/sgb_boot_resource.c server/sgb_boot_resource.h server/slot.c server/slot.h serial/serial_peripheral.c serial/serial_peripheral.h common/file_util.c common/net_compat.c $(SAMEBOY_LIB) $(BUILD_DIR)/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(GB_LINK_ENGINE_DETERMINISM_CFLAGS) server/gb_link_engine_determinism_test.c server/gb_link_engine.c server/secure_memory.c server/content_hash.c server/rom_profile.c server/sgb_boot_resource.c server/slot.c serial/serial_peripheral.c common/file_util.c common/net_compat.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(SECURE_MEMORY_TEST_BIN): server/secure_memory_test.c server/secure_memory.c server/secure_memory.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) server/secure_memory_test.c server/secure_memory.c $(LDFLAGS) -o $@

$(GB_LINK_ENGINE_ZEROIZATION_TEST_BIN): server/gb_link_engine_zeroization_test.c server/gb_link_engine.c server/gb_link_engine.h server/secure_memory.c server/secure_memory.h server/content_hash.c server/content_hash.h server/rom_profile.c server/rom_profile.h server/sgb_boot_resource.c server/sgb_boot_resource.h server/slot.c server/slot.h serial/serial_peripheral.c serial/serial_peripheral.h common/file_util.c common/net_compat.c $(SAMEBOY_LIB) $(BUILD_DIR)/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(GB_LINK_ENGINE_DETERMINISM_CFLAGS) server/gb_link_engine_zeroization_test.c server/gb_link_engine.c server/secure_memory.c server/content_hash.c server/rom_profile.c server/sgb_boot_resource.c server/slot.c serial/serial_peripheral.c common/file_util.c common/net_compat.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(GB_LINK_ENGINE_SAVE_PREFLIGHT_TEST_BIN): server/gb_link_engine_save_preflight_test.c server/gb_link_engine.c server/gb_link_engine.h server/secure_memory.c server/secure_memory.h server/content_hash.c server/content_hash.h server/rom_profile.c server/rom_profile.h server/sgb_boot_resource.c server/sgb_boot_resource.h server/slot.c server/slot.h serial/serial_peripheral.c serial/serial_peripheral.h common/file_util.c common/net_compat.c $(SAMEBOY_LIB) $(BUILD_DIR)/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(GB_LINK_ENGINE_DETERMINISM_CFLAGS) server/gb_link_engine_save_preflight_test.c server/gb_link_engine.c server/secure_memory.c server/content_hash.c server/rom_profile.c server/sgb_boot_resource.c server/slot.c serial/serial_peripheral.c common/file_util.c common/net_compat.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(ROM_PROFILE_TEST_BIN): server/rom_profile_test.c server/rom_profile.c server/rom_profile.h $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(CFLAGS) server/rom_profile_test.c server/rom_profile.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) -o $@

$(SERIAL_PERIPHERAL_TEST_BIN): serial/serial_peripheral_test.c serial/serial_peripheral.c serial/serial_peripheral.h $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(CFLAGS) serial/serial_peripheral_test.c serial/serial_peripheral.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) -o $@

$(SGB_SLOT_TEST_BIN): server/slot_sgb_test.c server/rom_profile.c server/sgb_boot_resource.c server/slot.c serial/serial_peripheral.c server/rom_profile.h server/sgb_boot_resource.h server/slot.h serial/serial_peripheral.h common/file_util.c common/net_compat.c $(SAMEBOY_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(CFLAGS) server/slot_sgb_test.c server/rom_profile.c server/sgb_boot_resource.c server/slot.c serial/serial_peripheral.c common/file_util.c common/net_compat.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(SGB_BOOT_RESOURCE_TEST_BIN): server/sgb_boot_resource_test.c server/sgb_boot_resource.c server/sgb_boot_resource.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) server/sgb_boot_resource_test.c server/sgb_boot_resource.c $(LDFLAGS) -o $@

$(BUILD_DIR)/bootroms/sgb2_boot.bin: ../assets/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	mkdir -p $(dir $@)
	cp -f $< $@

$(SGB_FIXTURE_ROM): ../test_fixtures/sgb/sgb_color_test.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	rgbasm -o $@.o $<
	rgblink -o $@ $@.o
	rgbfix -v -p 0 $@
	$(RM) $@.o

$(LINK_SERIAL_FIXTURE_A): ../test_fixtures/link_engine/serial_link_test.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	rgbasm -D LINK_ROLE=0 -o $@.o $<
	rgblink -o $@ $@.o
	rgbfix -v -p 0 $@
	$(RM) $@.o

$(LINK_SERIAL_FIXTURE_B): ../test_fixtures/link_engine/serial_link_test.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	rgbasm -D LINK_ROLE=1 -o $@.o $<
	rgblink -o $@ $@.o
	rgbfix -v -p 0 $@
	$(RM) $@.o

$(LINK_IR_FIXTURE_A): ../test_fixtures/link_engine/infrared_link_test.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	rgbasm -D LINK_ROLE=0 -o $@.o $<
	rgblink -o $@ $@.o
	rgbfix -v -p 0 $@
	$(RM) $@.o

$(LINK_IR_FIXTURE_B): ../test_fixtures/link_engine/infrared_link_test.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	rgbasm -D LINK_ROLE=1 -o $@.o $<
	rgblink -o $@ $@.o
	rgbfix -v -p 0 $@
	$(RM) $@.o

$(LINK_RTC_FIXTURE): ../test_fixtures/link_engine/rtc_boundary_test.asm | $(BUILD_DIR)
	mkdir -p $(dir $@)
	rgbasm -o $@.o $<
	rgblink -o $@ $@.o
	rgbfix -v -p 0 $@
	$(RM) $@.o

$(LOG_CONFIG_TEST_BIN): common/log_util_test.c common/log_util.c common/log_util.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) common/log_util_test.c common/log_util.c $(LDFLAGS) -o $@

$(KEY_CONFIG_CONTROLLER_TEST_BIN): common/key_config_controller_test.c common/key_config.c common/key_config.h common/string_util.c common/string_util.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) common/key_config_controller_test.c common/key_config.c common/string_util.c $(LDFLAGS) $(SDL2_LIBS) -o $@

$(DISPLAY_SCALE_TEST_BIN): common/display_scale_test.c common/display_scale.c common/display_scale.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) common/display_scale_test.c common/display_scale.c $(LDFLAGS) -o $@

$(MENU_CONFIG_DISPLAY_SCALE_TEST_BIN): app/menu_config_display_scale_test.c app/menu_config.c app/menu_config.h app/menu_paths.c app/menu_paths.h common/display_scale.c common/file_util.c common/key_config.c common/log_util.c common/string_util.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) app/menu_config_display_scale_test.c app/menu_config.c app/menu_paths.c common/display_scale.c common/file_util.c common/key_config.c common/log_util.c common/string_util.c $(LDFLAGS) $(SDL2_LIBS) -o $@

$(VIDEO_WINDOW_EXIT_CONFIRMATION_TEST_BIN): server/video_window_exit_confirmation_test.c server/video_window.c server/input_router.c server/slot.c server/rom_profile.c server/sgb_boot_resource.c serial/serial_peripheral.c common/display_scale.c common/file_util.c common/key_config.c common/net_compat.c common/sdl_text.c common/string_util.c $(SAMEBOY_LIB) $(BUILD_DIR)/bootroms/sgb2_boot.bin | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) -DINTEGRAL_GB_RUNTIME_VIDEO_WINDOW_TEST_READBACK=1 server/video_window_exit_confirmation_test.c server/video_window.c server/input_router.c server/slot.c server/rom_profile.c server/sgb_boot_resource.c serial/serial_peripheral.c common/display_scale.c common/file_util.c common/key_config.c common/net_compat.c common/sdl_text.c common/string_util.c $(LDFLAGS) $(GB_RUNTIME_LDFLAGS) $(SDL2_LIBS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(SERVER_OPTIONS_TEST_BIN): server/server_options_test.c server/server_options.c server/server_options.h common/display_scale.c common/file_util.c common/key_config.c common/parse_util.c common/string_util.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) server/server_options_test.c server/server_options.c common/display_scale.c common/file_util.c common/key_config.c common/parse_util.c common/string_util.c $(LDFLAGS) $(SDL2_LIBS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(INPUT_ROUTER_AUTO_A_TEST_BIN): server/input_router_auto_a_test.c server/input_router.c server/input_router.h common/key_config.c common/key_config.h common/string_util.c common/string_util.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GB_RUNTIME_CPPFLAGS) $(SDL2_CFLAGS) $(CFLAGS) server/input_router_auto_a_test.c server/input_router.c common/key_config.c common/string_util.c $(LDFLAGS) $(SDL2_LIBS) $(GB_RUNTIME_PLATFORM_LIBS) -o $@

$(BUILD_DIR):
	mkdir -p $@

$(SAMEBOY_LIB): $(wildcard $(SAMEBOY_DIR)/Core/*.c $(SAMEBOY_DIR)/Core/*.h)
	$(MAKE) -C $(SAMEBOY_DIR) lib CONF=release\
		$(if $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS),CC="$(CC) $(INTEGRAL_RELEASE_PREFIX_MAP_FLAGS)")

clean:
	rm -rf $(BUILD_DIR)
