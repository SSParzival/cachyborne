/* Linux host policy, independent of the PS4's seven logical CPU bits. */
#ifndef CACHY_AFFINITY_H
#define CACHY_AFFINITY_H
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static cpu_set_t cachy_allowed;
static int cachy_allowed_valid;
/* Capture before main is pinned; every including module loads before main(). */
__attribute__((constructor)) static void cachy_capture_affinity(void) {
    cachy_allowed_valid = sched_getaffinity(0, sizeof(cachy_allowed), &cachy_allowed) == 0;
}
static inline int cachy_parse_cpus(const char *text, cpu_set_t *mask) {
    CPU_ZERO(mask);
    if (!text || !*text) return 0;
    while (*text) {
        char *end;
        long first = strtol(text, &end, 10), last = first;
        if (end == text || first < 0 || first >= CPU_SETSIZE) return 0;
        text = end;
        if (*text == '-') {
            text++;
            last = strtol(text, &end, 10);
            if (end == text || last < first || last >= CPU_SETSIZE) return 0;
            text = end;
        }
        for (long cpu = first; cpu <= last; cpu++) CPU_SET(cpu, mask);
        if (!*text) return 1;
        if (*text++ != ',' || !*text) return 0;
    }
    return 0;
}
static inline void cachy_pin_thread(pthread_t thread, const char *name) {
    const char *enabled = getenv("BB_CPU_PINNING");
    if (!enabled || strcmp(enabled, "1") || !cachy_allowed_valid) return;
    /* Explicit known worker roles; unknown guest threads stay on the P cores. */
    int background = name && (strstr(name, "ShaderCompile") || strstr(name, "PipeCacheIO") ||
        strstr(name, "AssetWorker") || strstr(name, "Decompress"));
    const char *cpus = getenv(background ? "BB_E_CPUS" : "BB_P_CPUS");
    cpu_set_t requested, selected;
    if (!cachy_parse_cpus(cpus ? cpus : (background ? "12-19" : "0-11"), &requested)) return;
    CPU_AND(&selected, &requested, &cachy_allowed);
    if (!CPU_COUNT(&selected)) return;
    int error = pthread_setaffinity_np(thread, sizeof(selected), &selected);
    if (error) fprintf(stderr, "Cachyborne: affinity for %s: %s\n", name ? name : "thread", strerror(error));
}
#endif
