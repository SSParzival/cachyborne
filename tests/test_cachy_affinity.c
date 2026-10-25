#define _GNU_SOURCE
#include "src/cachy_affinity.h"
#include <assert.h>

int main(void) {
    cpu_set_t parsed, original, actual;
    assert(cachy_parse_cpus("0-11,15,19", &parsed));
    assert(CPU_COUNT(&parsed) == 14 && CPU_ISSET(19, &parsed));
    assert(!cachy_parse_cpus("3-1", &parsed));
    assert(!cachy_parse_cpus("0,", &parsed));
    assert(!cachy_parse_cpus("1024", &parsed));
    assert(!cachy_parse_cpus("-1", &parsed));
    assert(!cachy_parse_cpus("0-x", &parsed));
    assert(sched_getaffinity(0, sizeof(original), &original) == 0);
    int first = 0, last = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &original)) { last = cpu; if (!CPU_ISSET(first, &original)) first = cpu; }
    }
    char p[24], e[24];
    snprintf(p, sizeof(p), "%d", first);
    snprintf(e, sizeof(e), "%d", last);
    setenv("BB_P_CPUS", p, 1); setenv("BB_E_CPUS", e, 1); setenv("BB_CPU_PINNING", "1", 1);
    cachy_pin_thread(pthread_self(), "main");
    assert(sched_getaffinity(0, sizeof(actual), &actual) == 0);
    assert(CPU_COUNT(&actual) == 1 && CPU_ISSET(first, &actual));
    /* Background work can move outside the foreground's inherited mask. */
    cachy_pin_thread(pthread_self(), "bb:ShaderCompile");
    assert(sched_getaffinity(0, sizeof(actual), &actual) == 0);
    assert(CPU_COUNT(&actual) == 1 && CPU_ISSET(last, &actual));
    setenv("BB_CPU_PINNING", "0", 1);
    cachy_pin_thread(pthread_self(), "main");
    assert(sched_getaffinity(0, sizeof(actual), &actual) == 0 && CPU_ISSET(last, &actual));
    assert(pthread_setaffinity_np(pthread_self(), sizeof(original), &original) == 0);
    puts("Cachyborne affinity policy passed");
}
