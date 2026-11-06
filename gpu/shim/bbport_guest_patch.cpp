// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_guest_patch.h"
#include "common/guest_memory.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace BbGuestPatch {
namespace {
std::mutex patch_mutex;
constexpr std::size_t MaxBytes=16u<<20;
struct Mapping { std::uintptr_t start;std::size_t size;int protection; };
bool Mappings(std::uintptr_t first,std::uintptr_t last,std::vector<Mapping>& out) {
    FILE* file=std::fopen("/proc/self/maps","r");
    if (!file) return false;
    char line[1024],permissions[5];unsigned long lo,hi;
    auto next=first;
    while (next<last && std::fgets(line,sizeof(line),file)) {
        if (!std::strchr(line,'\n')) { int c;do { c=std::fgetc(file); } while(c!='\n' && c!=EOF); }
        if (std::sscanf(line,"%lx-%lx %4s",&lo,&hi,permissions)!=3 || hi<=next) continue;
        if (lo>next) break;
        const auto end=std::min<std::uintptr_t>(hi,last);
        const int protection=(permissions[0]=='r'?PROT_READ:0)|
            (permissions[1]=='w'?PROT_WRITE:0)|(permissions[2]=='x'?PROT_EXEC:0);
        out.push_back({next,end-next,protection});next=end;
    }
    std::fclose(file);return next==last;
}
void Restore(const std::vector<Mapping>& mappings,std::size_t count,int final) {
    for (std::size_t i=0;i<count;++i) {
        const auto& m=mappings[i];
        if (mprotect(reinterpret_cast<void*>(m.start),m.size,final<0?m.protection:final)) {
            std::fputs("Guest patch: cannot restore page protection; stopping\n",stderr);std::abort();
        }
    }
}
}
Result Write(std::uintptr_t destination,std::span<const std::uint8_t> bytes,
    std::span<const std::uint8_t> expected,int final_protection,bool opcode_last,Mode mode) {
    if (bytes.empty() || bytes.size()>MaxBytes || final_protection<-1 ||
        (final_protection>=0 && (final_protection&~(PROT_READ|PROT_WRITE|PROT_EXEC))) ||
        (mode!=Mode::Quiescent && mode!=Mode::WritableData) ||
        (mode==Mode::WritableData && (final_protection!=-1 || opcode_last)) ||
        (!expected.empty() && expected.size()!=bytes.size()) ||
        !BbGuestMemory::Range(destination,bytes.size())) return Result::Refused;
    std::vector<std::uint8_t> replacement(bytes.size()),before(bytes.size()),check(expected.size());
    if (!BbGuestMemory::Read(nullptr,reinterpret_cast<std::uintptr_t>(bytes.data()),replacement.data(),replacement.size()) ||
        (!expected.empty() && !BbGuestMemory::Read(nullptr,reinterpret_cast<std::uintptr_t>(expected.data()),check.data(),check.size())))
        return Result::Refused;
    std::lock_guard lock{patch_mutex};
    if (!BbGuestMemory::Read(nullptr,destination,before.data(),before.size())) return Result::Refused;
    const bool already=before==replacement;
    if (mode==Mode::Quiescent && already) return Result::AlreadyApplied;
    if (!already && !expected.empty() && before!=check) return Result::Mismatch;
    const long page=sysconf(_SC_PAGESIZE);
    if (page<=0 || (page&(page-1))) return Result::Refused;
    const auto mask=std::uintptr_t(page)-1;
    const auto end=destination+bytes.size();
    if (end>std::numeric_limits<std::uintptr_t>::max()-mask) return Result::Refused;
    std::vector<Mapping> mappings;
    if (!Mappings(destination&~mask,(end+mask)&~mask,mappings)) return Result::Refused;
    if (mode==Mode::WritableData && std::ranges::any_of(mappings,[](const Mapping& m) {
        return !(m.protection&PROT_WRITE) || (m.protection&PROT_EXEC);
    })) return Result::Refused;
    // Keep the established idempotence rule for code patches whose expected
    // bytes were the original prologue, even after a previous application.
    if (already) return Result::AlreadyApplied;
    std::size_t opened=0;
    if (mode==Mode::Quiescent) {
        for (const auto& m:mappings) {
            if (mprotect(reinterpret_cast<void*>(m.start),m.size,m.protection|PROT_READ|PROT_WRITE)) {
                Restore(mappings,opened,-1);return Result::Refused;
            }
            ++opened;
        }
    }
    const auto write=[&](std::uintptr_t at,const void* source,std::size_t size) {
        return !size || BbGuestMemory::Write(at,source,size);
    };
    const bool written=opcode_last
        ? write(destination+1,replacement.data()+1,replacement.size()-1) && write(destination,replacement.data(),1)
        : write(destination,replacement.data(),replacement.size());
    if (!written && !write(destination,before.data(),before.size())) {
        std::fputs("Guest patch: cannot roll back incomplete write; stopping\n",stderr);std::abort();
    }
    if (mode==Mode::Quiescent)
        __builtin___clear_cache(reinterpret_cast<char*>(destination),reinterpret_cast<char*>(end));
    Restore(mappings,opened,written?final_protection:-1);
    return written?Result::Applied:Result::Refused;
}
}
