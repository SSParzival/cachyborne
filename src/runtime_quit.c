// SPDX-License-Identifier: GPL-2.0-or-later
/* Exit through the game's save+title path. Confirmation belongs to the game's
 * actual affirmative callback, so answering No never arms this state. */
#define _GNU_SOURCE
#include "runtime_quit.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_error.h>

static pthread_mutex_t quit_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t quit_deadline;
static int quit_title_seen;
static uint64_t quit_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static void quit_clear(void) {
    __atomic_store_n(&quit_deadline,0,__ATOMIC_RELEASE);
    quit_title_seen=0;
}
/* quit_lock held. The save module never calls us while its own lock is held. */
static void quit_check(void) {
    if (!quit_deadline) return;
    if (quit_now()>=quit_deadline) { quit_clear(); return; }
    if (!quit_title_seen || runtime_savedata_mounted()) return;
    SDL_Event event={.type=SDL_EVENT_QUIT};
    if (SDL_PushEvent(&event)) {
        quit_clear();
        puts("Runtime: Exit to desktop: save data unmounted and title reached, closing");
        fflush(stdout);
    } else {
        /* Keep the request for a later title/save notification, within its deadline. */
        fprintf(stderr,"Runtime: Exit to desktop: %s\n",SDL_GetError());
    }
}
void runtime_quit_confirmed(void) {
    pthread_mutex_lock(&quit_lock);
    quit_title_seen=0;
    __atomic_store_n(&quit_deadline,quit_now()+120000000000ull,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&quit_lock);
    puts("Runtime: Exit to desktop: confirmed; waiting for the game's save and title");
}
void runtime_quit_cancel(void) {
    if (!__atomic_load_n(&quit_deadline,__ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&quit_lock);
    quit_clear();
    pthread_mutex_unlock(&quit_lock);
}
void runtime_quit_at_title(void) {
    if (!__atomic_load_n(&quit_deadline,__ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&quit_lock);
    quit_title_seen=1;
    quit_check();
    pthread_mutex_unlock(&quit_lock);
}
void runtime_quit_on_save_done(void) {
    if (!__atomic_load_n(&quit_deadline,__ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&quit_lock);
    quit_check();
    pthread_mutex_unlock(&quit_lock);
}
