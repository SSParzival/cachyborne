// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_guest_patch.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
using BbGuestPatch::Result;
using BbGuestPatch::Mode;
static int Protection(const void* p) {
    FILE* file=std::fopen("/proc/self/maps","r");assert(file);
    char line[1024],permissions[5];unsigned long lo,hi;int result=-1;
    while(std::fgets(line,sizeof(line),file))
        if(std::sscanf(line,"%lx-%lx %4s",&lo,&hi,permissions)==3 &&
            lo<=reinterpret_cast<std::uintptr_t>(p) && reinterpret_cast<std::uintptr_t>(p)<hi)
            result=(permissions[0]=='r'?PROT_READ:0)|(permissions[1]=='w'?PROT_WRITE:0)|(permissions[2]=='x'?PROT_EXEC:0);
    std::fclose(file);return result;
}
int main() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory=static_cast<unsigned char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(memory!=MAP_FAILED);std::memset(memory,0x55,page*2);
    assert(!mprotect(memory,page,PROT_READ|PROT_EXEC));
    assert(!mprotect(memory+page,page,PROT_READ));
    std::array<std::uint8_t,4> before{0x55,0x55,0x55,0x55},after{1,2,3,4},wrong{9,8,7,6};
    const auto at=reinterpret_cast<std::uintptr_t>(memory+page-2);
    assert(BbGuestPatch::Write(at,after,wrong)==Result::Mismatch);
    assert(!std::memcmp(memory+page-2,before.data(),4));
    assert(BbGuestPatch::Write(at,after,before)==Result::Applied);
    assert(!std::memcmp(memory+page-2,after.data(),4));
    assert(Protection(memory)==(PROT_READ|PROT_EXEC) && Protection(memory+page)==PROT_READ);
    assert(BbGuestPatch::Write(at,after,wrong)==Result::AlreadyApplied);
    assert(BbGuestPatch::Write(0,after)==Result::Refused);
    assert(BbGuestPatch::Write(std::numeric_limits<std::uintptr_t>::max()-1,after)==Result::Refused);
    assert(BbGuestPatch::Write(at,std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(1),4))==Result::Refused);
    assert(BbGuestPatch::Write(at,after,std::span<const std::uint8_t>(before.data(),3))==Result::Refused);

    // Live data writes cannot silently open RO/RX pages, including an
    // idempotent request or a span whose first page is already writable.
    assert(BbGuestPatch::Write(at,after,{},-1,false,Mode::WritableData)==Result::Refused);
    assert(!mprotect(memory,page*2,PROT_READ|PROT_WRITE));
    assert(BbGuestPatch::Write(at,wrong,after,-1,false,Mode::WritableData)==Result::Applied);
    assert(!std::memcmp(memory+page-2,wrong.data(),4));
    assert(BbGuestPatch::Write(at,wrong,before,-1,false,Mode::WritableData)==Result::AlreadyApplied);
    assert(Protection(memory)==(PROT_READ|PROT_WRITE) && Protection(memory+page)==(PROT_READ|PROT_WRITE));
    assert(!mprotect(memory+page,page,PROT_READ));
    assert(BbGuestPatch::Write(at,after,{},-1,false,Mode::WritableData)==Result::Refused);
    assert(BbGuestPatch::Write(at,wrong,{},-1,false,Mode::WritableData)==Result::Refused);
    assert(!std::memcmp(memory+page-2,wrong.data(),4));
    assert(Protection(memory)==(PROT_READ|PROT_WRITE) && Protection(memory+page)==PROT_READ);
    assert(!mprotect(memory,page,PROT_READ|PROT_WRITE|PROT_EXEC));
    const auto first=reinterpret_cast<std::uintptr_t>(memory);
    assert(BbGuestPatch::Write(first,after,{},-1,false,Mode::WritableData)==Result::Refused);
    assert(BbGuestPatch::Write(first,after,{},PROT_READ,false,Mode::WritableData)==Result::Refused);
    assert(BbGuestPatch::Write(first,after,{},-1,true,Mode::WritableData)==Result::Refused);
    assert(BbGuestPatch::Write(first,after,{},-1,false,static_cast<Mode>(99))==Result::Refused);
    assert(!mprotect(memory,page,PROT_READ|PROT_EXEC));

    // Open the first page, refuse write access on the second, then verify that
    // neither page changed and the first page's original RX state was restored.
    char name[]="patch-readonly-XXXXXX";const int writable=mkstemp(name);assert(writable>=0);
    assert(!ftruncate(writable,page));
    std::array<std::uint8_t,2> zeros{};
    assert(write(writable,zeros.data(),zeros.size())==ssize_t(zeros.size()));
    const int readonly=open(name,O_RDONLY);assert(readonly>=0);
    assert(!unlink(name) && !close(writable));
    assert(mmap(memory+page,page,PROT_READ,MAP_SHARED|MAP_FIXED,readonly,0)==memory+page);
    assert(!close(readonly));
    std::array<std::uint8_t,4> unchanged{};std::memcpy(unchanged.data(),memory+page-2,4);
    assert(BbGuestPatch::Write(at,wrong,unchanged)==Result::Refused);
    assert(!std::memcmp(memory+page-2,unchanged.data(),4));
    assert(Protection(memory)==(PROT_READ|PROT_EXEC) && Protection(memory+page)==PROT_READ);
    assert(!munmap(memory,page*2));
}
