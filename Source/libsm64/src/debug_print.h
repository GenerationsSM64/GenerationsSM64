#pragma once

#include "libsm64.h"
#include <stdio.h>

extern char g_debug_str[];
extern SM64DebugPrintFunctionPtr g_debug_print_func;

#define DEBUG_PRINT(...)                                                                               \
    do {                                                                                               \
        if (g_debug_print_func) {                                                                      \
            sprintf(g_debug_str, __VA_ARGS__);                                                         \
            g_debug_print_func(g_debug_str);                                                           \
        } else {                                                                                       \
            printf(__VA_ARGS__);                                                                       \
            printf("\n");                                                                              \
        }                                                                                              \
    } while (0)
