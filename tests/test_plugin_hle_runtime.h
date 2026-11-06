// SPDX-License-Identifier: GPL-3.0-or-later
// Included by the real native-runtime test: no mock import resolver.
#include "runtime_plugin_hle.h"
#include <stdarg.h>
#include <sched.h>
static int32_t ABI hle_a(uint32_t value) { return 100+(int32_t)value; }
static int32_t ABI hle_b(uint32_t value) { return 200+(int32_t)value; }
typedef struct { uint64_t integer;double real; } HleMixed;
static HleMixed ABI hle_mixed(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,
    uint64_t g,uint64_t h,uint64_t i,uint64_t j,
    double x0,double x1,double x2,double x3,double x4,double x5,double x6,double x7) {
    return (HleMixed){a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j,
        x0+2*x1+3*x2+4*x3+5*x4+6*x5+7*x6+8*x7};
}
static double ABI hle_variadic(uint64_t first,...) {
    va_list ap;va_start(ap,first);double result=(double)first;
    for(unsigned i=0;i<9;++i) result+=(double)va_arg(ap,uint64_t)*(i+2);
    for(unsigned i=0;i<8;++i) result+=va_arg(ap,double)*(i+1);
    va_end(ap);return result;
}
static _Atomic int hle_entered,hle_release;
static uint64_t ABI hle_blocked(uint64_t value) {
    atomic_store(&hle_entered,1);
    while (!atomic_load(&hle_release)) sched_yield();
    return value+17;
}
static uint64_t (ABI *hle_cached_block)(uint64_t);
static void *hle_worker(void *unused) {
    (void)unused;assert(hle_cached_block(25)==42);return NULL;
}
static void hle_permissions(uintptr_t code) {
    FILE *maps=fopen("/proc/self/maps","r");assert(maps);
    char line[512],permissions[5];unsigned long low,high;int found=0;
    while(fgets(line,sizeof(line),maps)) if(sscanf(line,"%lx-%lx %4s",&low,&high,permissions)==3 &&
        low<=code && code<high) {
        assert(permissions[0]=='r' && permissions[1]!='w' && permissions[2]=='x');found=1;
    }
    fclose(maps);assert(found);
}
static void test_plugin_hle_runtime(void) {
    typedef int32_t (ABI *Sleep)(uint32_t);
    const uintptr_t original=runtime_resolve("1jfXLRVzisc#p#J",0);
    assert(original && !runtime_plugin_hle_enabled());
    assert(runtime_plugin_hle_register(0,"sceKernelUsleep",hle_a));
    assert(runtime_plugin_hle_register(1,NULL,hle_a));
    assert(runtime_plugin_hle_register(1,"sceKernelUsleep",NULL));
    assert(runtime_plugin_hle_register(1,"not_a_native_export",hle_a));
    char long_name[300];memset(long_name,'x',sizeof(long_name));
    assert(runtime_plugin_hle_register(1,long_name,hle_a));
    assert(!runtime_plugin_hle_register(1,"sceKernelUsleep",hle_a));
    assert(!runtime_plugin_hle_register(2,"sceKernelUsleep",hle_b));
    for(uint64_t owner=10;owner<16;++owner)
        assert(!runtime_plugin_hle_register(owner,"sceKernelUsleep",hle_a));
    assert(runtime_plugin_hle_register(99,"sceKernelUsleep",hle_a)); // eight-owner bound
    for(uint64_t owner=10;owner<16;++owner) runtime_plugin_hle_retire(owner);
    assert(!runtime_plugin_hle_register(3,"memcpy",hle_mixed));
    assert(!runtime_plugin_hle_register(4,"memset",hle_variadic));
    assert(!runtime_plugin_hle_register(5,"memmove",hle_blocked));
    assert(!runtime_plugin_hle_register(6,"__stack_chk_guard",hle_a));
    assert(runtime_resolve("1jfXLRVzisc#p#J",0)==original); // staged, not yet sealed
    const uintptr_t guard=runtime_resolve("f7uOxY9mM1U#p#J",1);
    runtime_plugin_hle_seal();
    assert(runtime_plugin_hle_register(7,"sceKernelUsleep",hle_a));
    Sleep cached=GET(Sleep,"1jfXLRVzisc#p#J");
    assert((uintptr_t)cached!=original && cached(7)==207);
    assert((uintptr_t)cached==runtime_resolve("1jfXLRVzisc#other#module",0));
    hle_permissions((uintptr_t)cached);
    assert(runtime_resolve("f7uOxY9mM1U#p#J",1)==guard);
    assert(!runtime_resolve("f7uOxY9mM1U#p#J",0));
    runtime_start(0);assert(!runtime_resolve("1jfXLRVzisc#p#J",0));runtime_start(1);
    runtime_plugin_hle_retire(2);assert(cached(7)==107);
    runtime_plugin_hle_retire(1);assert(cached(0)==0); // same cached import, native fallback
    typedef HleMixed (ABI *Mixed)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
        uint64_t,uint64_t,uint64_t,uint64_t,double,double,double,double,double,double,double,double);
    Mixed mixed=GET(Mixed,"Q3VBxCXhUHs#q#q");
    HleMixed result=mixed(1,2,3,4,5,6,7,8,9,10,1.,2.,3.,4.,5.,6.,7.,8.);
    assert(result.integer==385 && result.real==204.);
    typedef double (ABI *Variadic)(uint64_t,...);
    Variadic variadic=GET(Variadic,"8zTFvBIAIN8#q#q");
    assert(variadic((uint64_t)1,(uint64_t)2,(uint64_t)3,(uint64_t)4,(uint64_t)5,
        (uint64_t)6,(uint64_t)7,(uint64_t)8,(uint64_t)9,(uint64_t)10,
        1.,2.,3.,4.,5.,6.,7.,8.)==589.);
    hle_cached_block=GET(uint64_t (ABI *)(uint64_t),"+P6FRGH4LfA#q#q");
    pthread_t thread;assert(!pthread_create(&thread,NULL,hle_worker,NULL));
    while(!atomic_load(&hle_entered)) sched_yield();
    runtime_plugin_hle_retire(5);
    char from[]="fallback",to[16]={0};
    typedef void *(ABI *Move)(void*,const void*,size_t);
    Move restored=(Move)(uintptr_t)hle_cached_block;
    assert(restored(to,from,sizeof(from))==to && !strcmp(from,to));
    atomic_store(&hle_release,1);assert(!pthread_join(thread,NULL));
    runtime_plugin_hle_retire(3);runtime_plugin_hle_retire(4);runtime_plugin_hle_retire(6);
    assert(GET(Move,"Q3VBxCXhUHs#q#q")(to,from,sizeof(from))==to);
    puts("PASS: native HLE cached-import fallback, layered retirement, scalar/vector/stack/variadic ABI, RX code and in-flight call residency contract");
}
