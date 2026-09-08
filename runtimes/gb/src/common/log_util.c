/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "log_util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define integral_gb_runtime_getcwd _getcwd
#else
#include <unistd.h>
#define integral_gb_runtime_getcwd getcwd
#endif

static bool redirected;
static bool checked_config;
static bool logging_enabled;

static void trim_config_line(char *text)
{
    char *start = text;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0 &&
           (text[len - 1] == '\n' ||
            text[len - 1] == '\r' ||
            text[len - 1] == ' ' ||
            text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
}

static bool config_value_is_enabled(const char *value)
{
    return strcmp(value, "1") == 0 ||
           strcmp(value, "on") == 0 ||
           strcmp(value, "ON") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "TRUE") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "YES") == 0;
}

static bool local_time_now(struct tm *out)
{
    time_t now = time(NULL);
#ifdef _WIN32
    return localtime_s(out, &now) == 0;
#else
    return localtime_r(&now, out) != NULL;
#endif
}

bool integral_gb_runtime_log_enabled(void)
{
    if (checked_config) {
        return logging_enabled;
    }
    checked_config = true;
    logging_enabled = false;

    const char *env_enabled = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_LOG_ENABLED");
    if (env_enabled && config_value_is_enabled(env_enabled)) {
        logging_enabled = true;
        return true;
    }

    FILE *in = fopen(INTEGRAL_GB_RUNTIME_LOG_CONFIG_FILE, "r");
    if (!in) {
        return false;
    }

    char line[128];
    while (fgets(line, sizeof(line), in)) {
        trim_config_line(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char *name = line;
        char *value = equals + 1;
        trim_config_line(name);
        trim_config_line(value);

        if (strcmp(name, "enabled") == 0 || strcmp(name, "log") == 0) {
            logging_enabled = config_value_is_enabled(value);
        }
    }
    fclose(in);
    return logging_enabled;
}

void integral_gb_runtime_log_redirect_stdio(const char *component)
{
    if (redirected) {
        return;
    }
    redirected = true;

    if (!integral_gb_runtime_log_enabled()) {
        return;
    }

    const char *stdio_ready = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_LOG_STDIO_READY");
    if (!stdio_ready || !config_value_is_enabled(stdio_ready)) {
        FILE *out = freopen("gb_runtime.log", "a", stdout);
        FILE *err = freopen("gb_runtime.log", "a", stderr);
        if (out) {
            setvbuf(stdout, NULL, _IOLBF, 0);
        }
        if (err) {
            setvbuf(stderr, NULL, _IOLBF, 0);
        }
    }

    struct tm tm_now;
    char time_text[32] = "unknown-time";
    if (local_time_now(&tm_now)) {
        (void)strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &tm_now);
    }

    char cwd[512] = "";
    if (!integral_gb_runtime_getcwd(cwd, sizeof(cwd))) {
        cwd[0] = '\0';
    }

    fprintf(stderr,
            "\n=== GB Runtime %s start %s ===\n"
            "cwd: %s\n",
            component ? component : "process",
            time_text,
            cwd[0] ? cwd : "(unavailable)");
    fflush(stderr);
}
