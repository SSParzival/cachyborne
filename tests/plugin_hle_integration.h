// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
typedef struct {
    int (*add)(uint64_t,const char*,void*);
    void (*retire)(uint64_t);
    void (*seal)(void);
    uintptr_t (*resolve)(const char*,int);
} HleNativeBackend;
typedef int (*HleNativeCase)(const char*,const char*,const HleNativeBackend*);
