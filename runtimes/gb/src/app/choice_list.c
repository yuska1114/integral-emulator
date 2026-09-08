/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "choice_list.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log_util.h"
#include "string_util.h"

static int compare_choice(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void integral_gb_runtime_choice_list_add(ChoiceList *list, const char *path)
{
    if (list->count >= INTEGRAL_GB_RUNTIME_MAX_CHOICES) {
        return;
    }
    for (unsigned i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], path) == 0) {
            return;
        }
    }
    (void)integral_gb_runtime_copy_text(list->items[list->count], sizeof(list->items[list->count]), path);
    list->count++;
}

void integral_gb_runtime_choice_list_scan_dir_for_suffixes(ChoiceList *list,
                                                 const char *dir_path,
                                                 const char **suffixes,
                                                 unsigned suffix_count)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (integral_gb_runtime_log_enabled()) {
            fprintf(stderr, "choice list: failed to open '%s': %s\n", dir_path, strerror(errno));
            fflush(stderr);
        }
        return;
    }

    if (integral_gb_runtime_log_enabled()) {
        fprintf(stderr, "choice list: scanning '%s'\n", dir_path);
        fflush(stderr);
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        bool matched = false;
        for (unsigned i = 0; i < suffix_count; i++) {
            if (integral_gb_runtime_menu_has_suffix(entry->d_name, suffixes[i])) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            continue;
        }

        integral_gb_runtime_choice_list_add(list, entry->d_name);
    }
    closedir(dir);
    if (integral_gb_runtime_log_enabled()) {
        fprintf(stderr, "choice list: '%s' candidates=%u\n", dir_path, list->count);
        fflush(stderr);
    }
}

void integral_gb_runtime_choice_list_sort(ChoiceList *list)
{
    qsort(list->items, list->count, sizeof(list->items[0]), compare_choice);
}

unsigned integral_gb_runtime_choice_list_find_index(const ChoiceList *list, const char *value)
{
    for (unsigned i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], value) == 0) {
            return i;
        }
    }
    return 0;
}

void integral_gb_runtime_choice_list_cycle(const ChoiceList *list, char *value, size_t value_size, int direction)
{
    if (list->count == 0) {
        return;
    }
    unsigned index = integral_gb_runtime_choice_list_find_index(list, value);
    if (direction > 0) {
        index = (index + 1) % list->count;
    }
    else {
        index = (index + list->count - 1) % list->count;
    }
    (void)integral_gb_runtime_copy_text(value, value_size, list->items[index]);
}

void integral_gb_runtime_choice_list_cycle_optional(const ChoiceList *list,
                                          char *value,
                                          size_t value_size,
                                          int direction)
{
    unsigned count = list->count + 1;
    if (count == 0) {
        return;
    }

    unsigned index = 0;
    if (value[0] != '\0') {
        index = integral_gb_runtime_choice_list_find_index(list, value) + 1;
    }

    if (direction > 0) {
        index = (index + 1) % count;
    }
    else {
        index = (index + count - 1) % count;
    }

    if (index == 0) {
        value[0] = '\0';
    }
    else {
        (void)integral_gb_runtime_copy_text(value, value_size, list->items[index - 1]);
    }
}

void integral_gb_runtime_choice_list_remove_at(ChoiceList *list, unsigned index)
{
    if (!list || index >= list->count) {
        return;
    }
    for (unsigned i = index + 1; i < list->count; i++) {
        memcpy(list->items[i - 1], list->items[i], sizeof(list->items[i - 1]));
    }
    list->count--;
    if (list->count < INTEGRAL_GB_RUNTIME_MAX_CHOICES) {
        list->items[list->count][0] = '\0';
    }
}
