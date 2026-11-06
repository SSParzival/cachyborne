// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "runtime_plugin_hle.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { MAX_BINDINGS=256, MAX_LAYERS=8, NAME_BYTES=256, THUNK_BYTES=16 };
typedef struct { uint64_t owner;uintptr_t function; } Layer;
typedef struct {
    char name[NAME_BYTES];
    Layer layers[MAX_LAYERS];
    unsigned count;
    uintptr_t original;
    int bound;
} Binding;
typedef struct {
    pthread_mutex_t mutex;
    Binding bindings[MAX_BINDINGS];
    unsigned count;
    unsigned char *code;
    _Atomic(uintptr_t) *targets;
} State;
static _Atomic(State *) state;
static _Atomic int sealed;
static pthread_mutex_t creation=PTHREAD_MUTEX_INITIALIZER;
int runtime_plugin_hle_enabled(void) {
    return atomic_load_explicit(&state,memory_order_acquire)!=NULL;
}

static State *get_state(void) {
    State *s=atomic_load_explicit(&state,memory_order_acquire);
    if (s) return s;
    pthread_mutex_lock(&creation);
    s=atomic_load_explicit(&state,memory_order_relaxed);
    if (!s) {
        s=calloc(1,sizeof(*s));
        if (s && pthread_mutex_init(&s->mutex,NULL)) { free(s);s=NULL; }
        if (s) atomic_store_explicit(&state,s,memory_order_release);
    }
    pthread_mutex_unlock(&creation);
    return s;
}
static uintptr_t effective(const Binding *b) {
    return b->count ? b->layers[b->count-1].function : b->original;
}
static int allocate_thunks(State *s) {
    if (s->code) return 1;
    long page=sysconf(_SC_PAGESIZE);
    if (page<MAX_BINDINGS*THUNK_BYTES || (size_t)page<MAX_BINDINGS*sizeof(uintptr_t)) return 0;
    unsigned char *region=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (region==MAP_FAILED) return 0;
    _Atomic(uintptr_t) *targets=(_Atomic(uintptr_t)*)(region+page);
    memset(region,0xcc,(size_t)page);
    for (unsigned i=0;i<MAX_BINDINGS;++i) {
        unsigned char *code=region+i*THUNK_BYTES;
        /* jmp qword [rip+disp32]: no GPR, vector, flags or stack changes. */
        code[0]=0xff;code[1]=0x25;
        int32_t displacement=(int32_t)((unsigned char *)&targets[i]-(code+6));
        memcpy(code+2,&displacement,sizeof(displacement));
        atomic_init(&targets[i],0);
    }
    if (mprotect(region,(size_t)page,PROT_READ|PROT_EXEC)) {
        munmap(region,(size_t)page*2);return 0;
    }
    s->code=region;s->targets=targets;
    return 1;
}
int runtime_plugin_hle_register(uint64_t owner,const char *name,void *function) {
    if (!owner || !name || !function || atomic_load_explicit(&sealed,memory_order_acquire)) return 1;
    const size_t length=strnlen(name,NAME_BYTES);
    if (!length || length>=NAME_BYTES || !runtime_function_name_known(name)) return 1;
    State *s=get_state();if (!s) return 1;
    pthread_mutex_lock(&s->mutex);
    if (atomic_load_explicit(&sealed,memory_order_acquire)) { pthread_mutex_unlock(&s->mutex);return 1; }
    unsigned index=0;
    while (index<s->count && strcmp(s->bindings[index].name,name)) ++index;
    if (index==s->count) {
        if (s->count==MAX_BINDINGS) { pthread_mutex_unlock(&s->mutex);return 1; }
        memcpy(s->bindings[index].name,name,length+1);++s->count;
    }
    Binding *b=&s->bindings[index];
    unsigned prior=0;while (prior<b->count && b->layers[prior].owner!=owner) ++prior;
    if (prior==b->count && b->count==MAX_LAYERS) { pthread_mutex_unlock(&s->mutex);return 1; }
    if (prior<b->count) {
        memmove(b->layers+prior,b->layers+prior+1,(b->count-prior-1)*sizeof(Layer));--b->count;
    }
    b->layers[b->count++]=(Layer){owner,(uintptr_t)function};
    pthread_mutex_unlock(&s->mutex);return 0;
}
void runtime_plugin_hle_seal(void) {
    /* Serialize with registration's second closed check/publication. */
    pthread_mutex_lock(&creation);
    State *s=atomic_load_explicit(&state,memory_order_acquire);
    if (s) pthread_mutex_lock(&s->mutex);
    atomic_store_explicit(&sealed,1,memory_order_release);
    if (s) pthread_mutex_unlock(&s->mutex);
    pthread_mutex_unlock(&creation);
}
void runtime_plugin_hle_retire(uint64_t owner) {
    State *s=atomic_load_explicit(&state,memory_order_acquire);
    if (!s || !owner) return;
    pthread_mutex_lock(&s->mutex);
    for (unsigned i=0;i<s->count;++i) {
        Binding *b=&s->bindings[i];
        for (unsigned j=0;j<b->count;++j) if (b->layers[j].owner==owner) {
            memmove(b->layers+j,b->layers+j+1,(b->count-j-1)*sizeof(Layer));--b->count;break;
        }
        if (b->bound) atomic_store_explicit(&s->targets[i],effective(b),memory_order_release);
    }
    pthread_mutex_unlock(&s->mutex);
}
uintptr_t runtime_plugin_hle_resolve(const char *symbol,uintptr_t original) {
    State *s=atomic_load_explicit(&state,memory_order_acquire);
    if (!s || !symbol || !original || !atomic_load_explicit(&sealed,memory_order_acquire)) return original;
    pthread_mutex_lock(&s->mutex);
    unsigned i=0;while (i<s->count && strcmp(s->bindings[i].name,symbol)) ++i;
    uintptr_t result=original;
    if (i<s->count) {
        Binding *b=&s->bindings[i];
        if (b->bound) {
            if (b->original==original) result=(uintptr_t)(s->code+i*THUNK_BYTES);
        } else if (b->count && allocate_thunks(s)) {
            b->original=original;b->bound=1;
            atomic_store_explicit(&s->targets[i],effective(b),memory_order_release);
            result=(uintptr_t)(s->code+i*THUNK_BYTES);
        }
    }
    pthread_mutex_unlock(&s->mutex);return result;
}
