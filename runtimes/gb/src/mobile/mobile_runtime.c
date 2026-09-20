/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_runtime_host.h"

#include "../common/net_compat.h"
#include "../common/display_scale.h"
#include "../server/audio_player.h"
#include "../server/content_hash.h"
#include "../server/input_router.h"
#include "../server/rom_profile.h"
#include "../server/screenshot.h"
#include "../server/slot.h"
#include "../server/video_window.h"
#include "mobile.h"
#include "mobile_adapter_bridge.h"
#include "mobile_route_engine.h"
#include "mobile_session_manifest.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_US 16743u
#define MAX_DEADLINE_LAG_US (FRAME_US * 2ULL)
#define MAX_INPUT_PULSES 64u

typedef struct RuntimeOptions {
  const char *rom_path, *save_path, *config_path, *manifest_path, *result_path;
  const char *input_script_path;
  unsigned scale, window_width, window_height, frame_limit, screenshot_every;
  int64_t rtc_offset_seconds;
  IntegralGBRuntimeKeyConfig slot1_keys;
  SDL_Keycode fast_key, screenshot_key, escape_key, turbo_hold_key, reset_key;
  bool headless, dump_screenshot;
} RuntimeOptions;

typedef struct InputPulse {
  unsigned start_frame, end_frame, period;
  GB_key_t key;
} InputPulse;

typedef struct InputScript {
  InputPulse pulses[MAX_INPUT_PULSES];
  unsigned count;
} InputScript;

typedef struct RuntimeHost {
  uint8_t config[MOBILE_CONFIG_SIZE];
  uint64_t elapsed_us, timer_us[MOBILE_MAX_TIMERS];
  bool config_dirty, serial_enabled;
  IntegralGBRuntimeMobileRouteEngine *routes;
} RuntimeHost;

static int parse_unsigned(const char *text, unsigned *out) {
  char *end = NULL;
  errno = 0;
  unsigned long value = strtoul(text, &end, 10);
  if (errno || !end || *end || value > 0xffffffffu)
    return -1;
  *out = (unsigned)value;
  return 0;
}
static int parse_rtc_offset_seconds(const char *text, int64_t *out) {
  char *end = NULL;
  errno = 0;
  long long value = strtoll(text, &end, 10);
  if (errno || !end || *end || value < -315360000LL || value > 315360000LL)
    return -1;
  *out = (int64_t)value;
  return 0;
}
static int parse_options(int argc, char **argv, RuntimeOptions *o) {
  memset(o, 0, sizeof(*o));
  o->scale = integral_display_scale_from_environment(
      "INTEGRAL_EMULATOR_DISPLAY_SCALE");
  integral_gb_runtime_key_config_slot1_default(&o->slot1_keys);
  o->fast_key = integral_gb_runtime_key_config_fast_default();
  o->screenshot_key = integral_gb_runtime_key_config_screenshot_default();
  o->escape_key = integral_gb_runtime_key_config_escape_default();
  o->turbo_hold_key = integral_gb_runtime_key_config_turbo_hold_default();
  o->reset_key = integral_gb_runtime_key_config_reset_default();
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--rom") && i + 1 < argc)
      o->rom_path = argv[++i];
    else if (!strcmp(argv[i], "--save") && i + 1 < argc)
      o->save_path = argv[++i];
    else if (!strcmp(argv[i], "--config") && i + 1 < argc)
      o->config_path = argv[++i];
    else if (!strcmp(argv[i], "--session-manifest") && i + 1 < argc)
      o->manifest_path = argv[++i];
    else if (!strcmp(argv[i], "--runtime-result") && i + 1 < argc)
      o->result_path = argv[++i];
    else if (!strcmp(argv[i], "--rtc-offset-seconds") && i + 1 < argc) {
      if (parse_rtc_offset_seconds(argv[++i], &o->rtc_offset_seconds))
        return -1;
    }
    else if (!strcmp(argv[i], "--input-script") && i + 1 < argc)
      o->input_script_path = argv[++i];
    else if (!strcmp(argv[i], "--slot1-keys") && i + 1 < argc) {
      if (integral_gb_runtime_key_config_parse(&o->slot1_keys, argv[++i]))
        return -1;
    }
    else if (!strcmp(argv[i], "--fast-key") && i + 1 < argc) {
      o->fast_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
      if (o->fast_key == SDLK_UNKNOWN)
        return -1;
    }
    else if (!strcmp(argv[i], "--screenshot-key") && i + 1 < argc) {
      o->screenshot_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
      if (o->screenshot_key == SDLK_UNKNOWN)
        return -1;
    }
    else if (!strcmp(argv[i], "--escape-key") && i + 1 < argc) {
      o->escape_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
      if (o->escape_key == SDLK_UNKNOWN)
        return -1;
    }
    else if (!strcmp(argv[i], "--turbo-hold-key") && i + 1 < argc) {
      o->turbo_hold_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
      if (o->turbo_hold_key == SDLK_UNKNOWN)
        return -1;
    }
    else if (!strcmp(argv[i], "--reset-key") && i + 1 < argc) {
      o->reset_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
      if (o->reset_key == SDLK_UNKNOWN)
        return -1;
    }
    else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
      if (integral_display_scale_parse(argv[++i], &o->scale))
        return -1;
    } else if (!strcmp(argv[i], "--window-width") && i + 1 < argc) {
      if (parse_unsigned(argv[++i], &o->window_width) ||
          o->window_width < 160u || o->window_width > 16384u)
        return -1;
    } else if (!strcmp(argv[i], "--window-height") && i + 1 < argc) {
      if (parse_unsigned(argv[++i], &o->window_height) ||
          o->window_height < 144u || o->window_height > 16384u)
        return -1;
    } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
      if (parse_unsigned(argv[++i], &o->frame_limit))
        return -1;
    } else if (!strcmp(argv[i], "--screenshot-every") && i + 1 < argc) {
      if (parse_unsigned(argv[++i], &o->screenshot_every) ||
          !o->screenshot_every)
        return -1;
    } else if (!strcmp(argv[i], "--dump-screenshot"))
      o->dump_screenshot = true;
    else if (!strcmp(argv[i], "--headless"))
      o->headless = true;
    else
      return -1;
  }
  return o->rom_path && o->save_path && o->config_path && o->manifest_path &&
                 o->result_path &&
                 ((o->window_width == 0u) == (o->window_height == 0u))
             ? 0
             : -1;
}
static int parse_key(const char *name, GB_key_t *key) {
  static const struct {
    const char *name;
    GB_key_t key;
  } keys[] = {{"RIGHT", GB_KEY_RIGHT},   {"LEFT", GB_KEY_LEFT},
              {"UP", GB_KEY_UP},         {"DOWN", GB_KEY_DOWN},
              {"A", GB_KEY_A},           {"B", GB_KEY_B},
              {"SELECT", GB_KEY_SELECT}, {"START", GB_KEY_START}};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    if (!strcmp(name, keys[i].name)) {
      *key = keys[i].key;
      return 0;
    }
  }
  return -1;
}
static int load_input_script(InputScript *script, const char *path) {
  memset(script, 0, sizeof(*script));
  if (!path)
    return 0;
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;
  char line[128];
  unsigned line_number = 0;
  while (fgets(line, sizeof(line), f)) {
    line_number++;
    if (line[0] == '\n' || line[0] == '#')
      continue;
    if (script->count >= MAX_INPUT_PULSES) {
      fprintf(stderr, "Too many input script lines\n");
      fclose(f);
      return -1;
    }
    InputPulse *p = &script->pulses[script->count];
    char key[16], extra;
    int fields = sscanf(line, "%u %u %u %15s %c", &p->start_frame,
                        &p->end_frame, &p->period, key, &extra);
    if (fields != 4 || p->end_frame <= p->start_frame || p->period < 2 ||
        parse_key(key, &p->key)) {
      fprintf(stderr, "Invalid input script line %u: %s", line_number, line);
      fclose(f);
      return -1;
    }
    script->count++;
  }
  return fclose(f) == 0 ? 0 : -1;
}
static void apply_input_script(GB_gameboy_t *gb, const InputScript *script,
                               unsigned frame) {
  bool pressed[GB_KEY_MAX] = {false};
  for (unsigned i = 0; i < script->count; i++) {
    const InputPulse *p = &script->pulses[i];
    if (frame >= p->start_frame && frame < p->end_frame &&
        (frame - p->start_frame) % p->period < p->period / 2)
      pressed[p->key] = true;
  }
  for (unsigned key = 0; key < GB_KEY_MAX; key++)
    GB_set_key_state(gb, (GB_key_t)key, pressed[key]);
}
static const IntegralGBRuntimeMobileManifestArtifact *
role(const IntegralGBRuntimeMobileSessionManifest *m, const char *name) {
  for (size_t i = 0; i < m->artifact_count; i++)
    if (!strcmp(m->artifacts[i].role, name))
      return &m->artifacts[i];
  return NULL;
}
static int load_exact(const char *path, void *out, size_t size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  size_t n = fread(out, 1, size, f);
  int extra = fgetc(f), rc = fclose(f);
  return n == size && extra == EOF && rc == 0 ? 0 : -1;
}
static int save_exact(const char *path, const void *data, size_t size) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  int ok = fwrite(data, 1, size, f) == size && fflush(f) == 0 && fclose(f) == 0;
  return ok ? 0 : -1;
}
static void host_log(void *user, const char *line) {
  (void)user;
  (void)line;
}
static void serial_disable(void *user) {
  ((RuntimeHost *)user)->serial_enabled = false;
}
static void serial_enable(void *user, bool mode32) {
  ((RuntimeHost *)user)->serial_enabled = !mode32;
}
static bool config_read(void *user, void *dest, uintptr_t offset, size_t size) {
  RuntimeHost *h = user;
  if (offset > sizeof(h->config) || size > sizeof(h->config) - offset)
    return false;
  memcpy(dest, h->config + offset, size);
  return true;
}
static bool config_write(void *user, const void *src, uintptr_t offset,
                         size_t size) {
  RuntimeHost *h = user;
  if (offset > sizeof(h->config) || size > sizeof(h->config) - offset)
    return false;
  memcpy(h->config + offset, src, size);
  h->config_dirty = true;
  return true;
}
static void time_latch(void *user, unsigned timer) {
  RuntimeHost *h = user;
  if (timer < MOBILE_MAX_TIMERS)
    h->timer_us[timer] = h->elapsed_us;
}
static bool time_check(void *user, unsigned timer, unsigned ms) {
  RuntimeHost *h = user;
  return timer < MOBILE_MAX_TIMERS &&
         h->elapsed_us - h->timer_us[timer] >= (uint64_t)ms * 1000u;
}
static bool sock_open(void *user, unsigned c, enum mobile_socktype t,
                      enum mobile_addrtype a, unsigned p) {
  return integral_gb_runtime_mobile_route_sock_open(
      ((RuntimeHost *)user)->routes, c, t, a, p);
}
static void sock_close(void *user, unsigned c) {
  integral_gb_runtime_mobile_route_sock_close(((RuntimeHost *)user)->routes, c);
}
static int sock_connect(void *user, unsigned c, const struct mobile_addr *a) {
  return integral_gb_runtime_mobile_route_sock_connect(
      ((RuntimeHost *)user)->routes, c, a);
}
static bool sock_false(void *user, unsigned c) {
  (void)user;
  (void)c;
  return false;
}
static int sock_send(void *user, unsigned c, const void *d, unsigned n,
                     const struct mobile_addr *a) {
  return integral_gb_runtime_mobile_route_sock_send(
      ((RuntimeHost *)user)->routes, c, d, n, a);
}
static int sock_recv(void *user, unsigned c, void *d, unsigned n,
                     struct mobile_addr *a) {
  return integral_gb_runtime_mobile_route_sock_recv(
      ((RuntimeHost *)user)->routes, c, d, n, a);
}
static void define_callbacks(struct mobile_adapter *a) {
  mobile_def_debug_log(a, host_log);
  mobile_def_serial_disable(a, serial_disable);
  mobile_def_serial_enable(a, serial_enable);
  mobile_def_config_read(a, config_read);
  mobile_def_config_write(a, config_write);
  mobile_def_time_latch(a, time_latch);
  mobile_def_time_check_ms(a, time_check);
  mobile_def_sock_open(a, sock_open);
  mobile_def_sock_close(a, sock_close);
  mobile_def_sock_connect(a, sock_connect);
  mobile_def_sock_listen(a, sock_false);
  mobile_def_sock_accept(a, sock_false);
  mobile_def_sock_send(a, sock_send);
  mobile_def_sock_recv(a, sock_recv);
}
static void pace(uint64_t *deadline) {
  uint64_t now = integral_gb_runtime_now_us();
  if (!*deadline || now > *deadline + MAX_DEADLINE_LAG_US)
    *deadline = now;
  *deadline += FRAME_US;
  while ((now = integral_gb_runtime_now_us()) < *deadline)
    integral_gb_runtime_sleep_ms((unsigned)((*deadline - now) / 1000u) + 1u);
}
static int file_hash(const char *path, size_t *size_out, char hex[65]) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  IntegralGBRuntimeContentSha256 h;
  integral_gb_runtime_content_sha256_init(&h);
  unsigned char b[8192];
  size_t total = 0;
  for (;;) {
    size_t n = fread(b, 1, sizeof(b), f);
    if (n) {
      integral_gb_runtime_content_sha256_update(&h, b, n);
      total += n;
    }
    if (n < sizeof(b))
      break;
  }
  if (ferror(f) || fclose(f))
    return -1;
  unsigned char digest[32];
  integral_gb_runtime_content_sha256_finish(&h, digest);
  integral_gb_runtime_content_sha256_hex(digest, hex);
  *size_out = total;
  return 0;
}
static int write_result(const RuntimeOptions *o,
                        const IntegralGBRuntimeMobileSessionManifest *m,
                        bool clean, bool flushed, size_t size, const char *hash,
                        unsigned errors,
                        const IntegralGBRuntimeMobileRouteStats *stats) {
  char temp[1200];
  if (snprintf(temp, sizeof(temp), "%s.tmp", o->result_path) >=
      (int)sizeof(temp))
    return -1;
  FILE *f = fopen(temp, "wb");
  if (!f)
    return -1;
  int ok =
      fprintf(
          f,
          "schema_version=1\nclean_exit=%u\nbattery_flush=%u\nsav_size=%"
          "zu\nsav_sha256=%s\nadapter_errors=%u\npackage_digest=%s\nroute_dns_"
          "requests=%u\nroute_http_requests=%u\nroute_matched_responses=%"
          "u\nroute_unauthorized_responses=%u\nroute_body_requests=%u\nroute_"
          "body_bytes=%llu\nroute_replay_responses=%u\nexit_reason=%s\n",
          clean ? 1u : 0u, flushed ? 1u : 0u, size, hash ? hash : "none",
          errors, m->package_digest, stats->dns_requests, stats->http_requests,
          stats->matched_responses, stats->unauthorized_responses,
          stats->body_requests, (unsigned long long)stats->body_bytes,
          stats->replay_responses,
          clean && flushed ? "normal" : "runtime_error") >= 0 &&
      fflush(f) == 0 && fclose(f) == 0;
  if (!ok) {
    remove(temp);
    return -1;
  }
  if (rename(temp, o->result_path)) {
    remove(temp);
    return -1;
  }
  return 0;
}

int integral_gb_runtime_mobile_runtime_main(int argc, char **argv) {
  RuntimeOptions o;
  if (parse_options(argc, argv, &o)) {
    fprintf(
        stderr,
        "Usage: %s --rom PATH --save PATH --config PATH --session-manifest "
        "PATH --runtime-result PATH [--rtc-offset-seconds N] [--headless] "
        "[--frames N] [--scale auto|1|2|3|4|5|6] "
        "[--window-width N --window-height N] "
        "[--slot1-keys SPEC] [--fast-key KEY] [--screenshot-key KEY] "
        "[--escape-key KEY] [--turbo-hold-key KEY] [--reset-key KEY] "
        "[--input-script PATH] [--screenshot-every N] [--dump-screenshot]\n",
        argv[0]);
    return 2;
  }
  InputScript input_script;
  if (load_input_script(&input_script, o.input_script_path)) {
    fprintf(stderr, "failed to load input script\n");
    return 1;
  }
  IntegralGBRuntimeMobileSessionManifest manifest;
  char error[160];
  if (integral_gb_runtime_mobile_session_manifest_load(
          o.manifest_path, &manifest, error, sizeof(error))) {
    fprintf(stderr, "%s\n", error);
    return 1;
  }
  const IntegralGBRuntimeMobileManifestArtifact *eeprom =
      role(&manifest, "adapter_eeprom");
  RuntimeHost host;
  memset(&host, 0, sizeof(host));
  if (!eeprom || eeprom->size != sizeof(host.config) ||
      load_exact(eeprom->path, host.config, sizeof(host.config))) {
    fprintf(stderr, "invalid adapter EEPROM artifact\n");
    return 1;
  }
  host.routes = integral_gb_runtime_mobile_route_engine_new(&manifest, error,
                                                            sizeof(error));
  if (!host.routes) {
    fprintf(stderr, "%s\n", error);
    return 1;
  }
  struct mobile_adapter *adapter = mobile_new(&host);
  if (!adapter) {
    integral_gb_runtime_mobile_route_engine_free(host.routes);
    return 1;
  }
  define_callbacks(adapter);
  mobile_config_load(adapter);
  mobile_config_set_device(adapter, MOBILE_ADAPTER_BLUE, true);
  struct mobile_addr4 dns = {.type = MOBILE_ADDRTYPE_IPV4,
                             .port = MOBILE_DNS_PORT,
                             .host = {127, 0, 0, 1}};
  mobile_config_set_dns(adapter, (struct mobile_addr *)&dns, MOBILE_DNS1);
  mobile_config_set_dns(adapter, (struct mobile_addr *)&dns, MOBILE_DNS2);
  mobile_config_save(adapter);
  mobile_start(adapter);
  GB_model_t model;
  IntegralGBRuntimeRomProfile profile;
  IntegralGBRuntimeRomModelReason reason;
  bool running = true, clean = false, flushed = false;
  if (integral_gb_runtime_slot_model_for_rom(o.rom_path, &model, &profile,
                                             &reason)) {
    running = false;
  }
  IntegralGBRuntimeSlot slot;
  memset(&slot, 0, sizeof(slot));
  if (running) {
    IntegralGBRuntimeSlotConfig cfg = {.name = "mobile-mode",
                                       .rom_path = o.rom_path,
                                       .save_path = o.save_path,
                                       .model = model,
                                       .skip_boot_rom = true};
    if (integral_gb_runtime_slot_init(&slot, &cfg))
      running = false;
    else if (o.rtc_offset_seconds != 0 &&
             integral_gb_runtime_slot_apply_rtc_offset_seconds(
                 &slot, o.rtc_offset_seconds) < 0)
      running = false;
  }
  IntegralGBRuntimeMobileAdapterBridge bridge;
  memset(&bridge, 0, sizeof(bridge));
  if (running && integral_gb_runtime_mobile_adapter_bridge_connect(
                     &bridge, slot.gb, &slot.serial_router, adapter, NULL))
    running = false;
  IntegralGBRuntimeInputRouter input;
  IntegralGBRuntimeVideoWindow *window = NULL;
  IntegralGBRuntimeAudioPlayer *audio = NULL;
  if (running) {
    integral_gb_runtime_input_router_init(&input, &slot, NULL);
    integral_gb_runtime_input_router_set_keymaps(
        &input, &o.slot1_keys, NULL, o.fast_key, o.screenshot_key,
        o.escape_key, o.turbo_hold_key, o.reset_key);
    integral_gb_runtime_input_router_disable_speed_controls(&input);
  }
  if (running && !o.headless &&
      integral_gb_runtime_video_window_open_titled_unthrottled_sized(
          &window, o.scale, 1, "INTEGRAL EMULATOR MOBILE MODE",
          o.window_width, o.window_height))
    running = false;
  if (running && window && integral_gb_runtime_audio_player_open(&audio, 120u))
    running = false;
  unsigned frame = 0;
  uint64_t deadline = 0;
  while (running && (!o.frame_limit || frame < o.frame_limit)) {
    if (window) {
      IntegralGBRuntimeVideoWindowPollResult pr =
          integral_gb_runtime_video_window_poll(window, &input, &slot, NULL);
      if (pr != INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE) {
        if (pr == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_RETURN_MENU)
          clean = true;
        break;
      }
    }
    if (integral_gb_runtime_input_router_take_reset_request(&input)) {
      mobile_stop(adapter);
      integral_gb_runtime_mobile_adapter_bridge_reset_pipeline(&bridge);
      integral_gb_runtime_slot_reset(&slot);
      mobile_start(adapter);
      integral_gb_runtime_video_window_show_message(window, "RESET");
    }
    if (integral_gb_runtime_input_router_take_screenshot_request(&input)) {
      char screenshot_path[512];
      if (integral_gb_runtime_screenshot_save_pair(
              &slot, NULL, "gb_mobile", "local", screenshot_path, sizeof(screenshot_path)) == 0) {
        printf("screenshot saved: %s\n", screenshot_path);
        integral_gb_runtime_video_window_show_message(window, "SCREENSHOT SAVED");
      } else {
        fprintf(stderr, "Failed to save screenshot\n");
        integral_gb_runtime_video_window_show_message(window, "SCREENSHOT FAILED");
      }
    }
    for (unsigned i = 0; i < 1u && (!o.frame_limit || frame < o.frame_limit);
         i++) {
      if (input_script.count)
        apply_input_script(slot.gb, &input_script, frame);
      if (integral_gb_runtime_slot_run_frames(&slot, 1)) {
        running = false;
        break;
      }
      host.elapsed_us += FRAME_US;
      integral_gb_runtime_mobile_adapter_bridge_pump(&bridge);
      frame++;
      if (o.screenshot_every && frame % o.screenshot_every == 0u) {
        char path[512];
        if (integral_gb_runtime_screenshot_save_slot(&slot, path,
                                                     sizeof(path))) {
          running = false;
          break;
        }
        fprintf(stdout, "screenshot_frame_%u=%s\n", frame, path);
      }
    }
    integral_gb_runtime_audio_player_queue_slot(audio, &slot);
    if (window && integral_gb_runtime_video_window_render(window, &slot, NULL))
      running = false;
    if (window && running)
      pace(&deadline);
  }
  if (input_script.count && slot.gb)
    for (unsigned key = 0; key < GB_KEY_MAX; key++)
      GB_set_key_state(slot.gb, (GB_key_t)key, false);
  if (running && o.dump_screenshot) {
    char path[512];
    if (integral_gb_runtime_screenshot_save_slot(&slot, path, sizeof(path)))
      running = false;
    else
      fprintf(stdout, "screenshot_final=%s\n", path);
  }
  if (running && !clean && o.frame_limit && frame >= o.frame_limit)
    clean = true;
  if (running && clean)
    flushed = integral_gb_runtime_slot_save_battery(&slot) == 0;
  size_t save_size = 0;
  char hash[65] = "none";
  if (flushed && file_hash(o.save_path, &save_size, hash))
    flushed = false;
  unsigned errors = bridge.callback_errors +
                    integral_gb_runtime_mobile_route_error_count(host.routes);
  IntegralGBRuntimeMobileRouteStats route_stats;
  integral_gb_runtime_mobile_route_stats(host.routes, &route_stats);
  if (bridge.connected)
    integral_gb_runtime_mobile_adapter_bridge_disconnect(&bridge);
  mobile_stop(adapter);
  if (host.config_dirty &&
      save_exact(o.config_path, host.config, sizeof(host.config)))
    running = false;
  integral_gb_runtime_video_window_close(window);
  integral_gb_runtime_audio_player_close(audio);
  if (slot.gb)
    integral_gb_runtime_slot_free_without_save(&slot);
  free(adapter);
  integral_gb_runtime_mobile_route_engine_free(host.routes);
  if (write_result(&o, &manifest, running && clean, flushed, save_size, hash,
                   errors, &route_stats))
    return 1;
  return running && clean && flushed ? 0 : 1;
}
