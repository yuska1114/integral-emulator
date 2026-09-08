/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_APP_CHOICE_LIST_H
#define INTEGRAL_GB_RUNTIME_APP_CHOICE_LIST_H

#include <stddef.h>

#include "menu_paths.h"

enum {
    INTEGRAL_GB_RUNTIME_MAX_CHOICES = 64,
};

typedef struct ChoiceList {
    char items[INTEGRAL_GB_RUNTIME_MAX_CHOICES][INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
    unsigned count;
} ChoiceList;

void integral_gb_runtime_choice_list_add(ChoiceList *list, const char *path);
void integral_gb_runtime_choice_list_scan_dir_for_suffixes(ChoiceList *list,
                                                 const char *dir_path,
                                                 const char **suffixes,
                                                 unsigned suffix_count);
void integral_gb_runtime_choice_list_sort(ChoiceList *list);
unsigned integral_gb_runtime_choice_list_find_index(const ChoiceList *list, const char *value);
void integral_gb_runtime_choice_list_cycle(const ChoiceList *list, char *value, size_t value_size, int direction);
void integral_gb_runtime_choice_list_cycle_optional(const ChoiceList *list,
                                          char *value,
                                          size_t value_size,
                                          int direction);
void integral_gb_runtime_choice_list_remove_at(ChoiceList *list, unsigned index);

#endif
