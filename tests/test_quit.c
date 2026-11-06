// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_error.h>
static atomic_uint_fast64_t now_ns=1000000000;
static int fake_clock(clockid_t clock,struct timespec *t) {
    assert(clock==CLOCK_MONOTONIC);
    const uint64_t now=atomic_load(&now_ns);
    t->tv_sec=now/1000000000;t->tv_nsec=now%1000000000;
    return 0;
}
#define clock_gettime fake_clock
#include "../src/runtime_quit.c"
#undef clock_gettime
static atomic_int mounts,pushed;
static bool push_success=true;
int runtime_savedata_mounted(void) { return atomic_load(&mounts); }
bool SDL_PushEvent(SDL_Event *event) {
    assert(event->type==SDL_EVENT_QUIT);
    if (!push_success) return false;
    atomic_fetch_add(&pushed,1);
    return true;
}
const char *SDL_GetError(void) { return "synthetic queue failure"; }
static void *notify(void *unused) {
    (void)unused;
    for (int i=0;i<100;++i) {
        runtime_quit_at_title();runtime_quit_on_save_done();
    }
    return NULL;
}
int main(void) {
    /* No confirmation (including No/cancel) cannot close even at title. */
    runtime_quit_at_title();runtime_quit_on_save_done();
    assert(!atomic_load(&pushed));
    runtime_quit_confirmed();
    runtime_quit_on_save_done(); /* unmounted but still in world */
    assert(!atomic_load(&pushed));
    runtime_quit_at_title();
    assert(atomic_load(&pushed)==1);
    runtime_quit_at_title();runtime_quit_on_save_done();
    assert(atomic_load(&pushed)==1);
    runtime_quit_confirmed();atomic_store(&mounts,2);
    runtime_quit_at_title();assert(atomic_load(&pushed)==1);
    atomic_store(&mounts,1);runtime_quit_on_save_done();
    assert(atomic_load(&pushed)==1);
    atomic_store(&mounts,0);runtime_quit_on_save_done();
    assert(atomic_load(&pushed)==2);
    runtime_quit_confirmed();runtime_quit_cancel();runtime_quit_at_title();
    assert(atomic_load(&pushed)==2);
    runtime_quit_confirmed();atomic_fetch_add(&now_ns,120000000000ull);
    runtime_quit_at_title();assert(atomic_load(&pushed)==2);
    runtime_quit_confirmed();atomic_store(&mounts,1);runtime_quit_at_title();
    atomic_fetch_add(&now_ns,120000000000ull);atomic_store(&mounts,0);
    runtime_quit_on_save_done();assert(atomic_load(&pushed)==2);
    runtime_quit_confirmed();push_success=false;runtime_quit_at_title();
    assert(atomic_load(&pushed)==2);
    push_success=true;runtime_quit_at_title();assert(atomic_load(&pushed)==3);
    runtime_quit_confirmed();
    pthread_t a,b;
    assert(!pthread_create(&a,NULL,notify,NULL));
    assert(!pthread_create(&b,NULL,notify,NULL));
    pthread_join(a,NULL);pthread_join(b,NULL);
    assert(atomic_load(&pushed)==4);
    puts("PASS: confirmed-only exit, title/save ordering, all mounts, expiry, cancellation, failed event retry and concurrent notifications");
}
