// SPDX-License-Identifier: GPL-3.0-or-later
#include "bbport_plugin_image.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
using BbPluginImage::Image;
using BbGuestPatch::Result;
namespace {
struct Region { std::uintptr_t base;std::size_t size; };
bool Mapped(void* p,std::uintptr_t at,std::size_t size) {
    const auto& r=*static_cast<Region*>(p);
    return at>=r.base && at-r.base<=r.size && size<=r.size-(at-r.base);
}
}
int main() {
    auto* memory=static_cast<unsigned char*>(mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(memory!=MAP_FAILED);std::memset(memory,0x77,8192);
    std::vector<std::uint8_t> elf(4096);
    Elf64_Ehdr header{};std::memcpy(header.e_ident,"\x7f" "ELF\x02\x01\x01",7);
    header.e_machine=EM_X86_64;header.e_phoff=sizeof(header);header.e_phentsize=sizeof(Elf64_Phdr);header.e_phnum=1;
    Elf64_Phdr load{};load.p_type=PT_LOAD;load.p_filesz=load.p_memsz=4096;
    std::memcpy(elf.data(),&header,sizeof(header));std::memcpy(elf.data()+header.e_phoff,&load,sizeof(load));
    auto image=Image::Create(elf,memory,8192);assert(image && !image->Is109());
    assert(image->Sha256()==image->MappedSha256());
    unsigned char value=0;
    assert(image->Read(0x400000,&value,1) && value==0x77);
    assert(image->Read(reinterpret_cast<std::uintptr_t>(memory),&value,1) && value==0x77);
    assert(!image->GuestAddress(0x400000));
    assert(!image->Read(0x401000,&value,1));
    assert(!image->Read(std::numeric_limits<std::uint64_t>::max(),&value,2));
    std::array<std::uint8_t,1> old{0x77},replacement{0x66};
    assert(image->Patch(0x400000,old,replacement)==Result::Refused && memory[0]==0x77);
    assert(image->Write(0x400000,replacement)==Result::Refused);
    assert(image->Write(0x400000,replacement,true)==Result::Refused);
    Region region{reinterpret_cast<std::uintptr_t>(memory+4096),128};
    assert(!image->Read(region.base,&value,1));
    auto approved=Image::Create(elf,memory,8192,Mapped,&region);assert(approved);
    assert(approved->Read(region.base,&value,1) && value==0x77);
    assert(!approved->Read(region.base+128,&value,1));
    auto padded=elf;padded.push_back(0x4a);
    auto variant=Image::Create(padded,memory,8192);assert(variant);
    assert(variant->Sha256()!=image->Sha256() && variant->MappedSha256()==image->MappedSha256());
    assert(!Image::Create(elf,memory,32));
    auto malformed=elf;Elf64_Ehdr bad=header;bad.e_phoff=UINT64_MAX;
    std::memcpy(malformed.data(),&bad,sizeof(bad));assert(!Image::Create(malformed,memory,8192));
    malformed=elf;auto invalid=load;invalid.p_filesz=4097;
    std::memcpy(malformed.data()+header.e_phoff,&invalid,sizeof(invalid));assert(!Image::Create(malformed,memory,8192));
    assert(!Image::Create(elf,reinterpret_cast<unsigned char*>(UINTPTR_MAX-16),8192));
    assert(!munmap(memory,8192));

    const char* path=std::getenv("CACHYBORNE_TEST_ELF");if (!path) return 77;
    std::ifstream file(path,std::ios::binary);
    std::vector<std::uint8_t> retail((std::istreambuf_iterator<char>(file)),{});
    constexpr std::size_t capacity=512u<<20,offset=0x2034770;
    auto* relocated=static_cast<unsigned char*>(mmap(nullptr,capacity,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(relocated!=MAP_FAILED);
    auto actual=Image::Create(retail,relocated,capacity);assert(actual && actual->Is109());
    assert(actual->GuestAddress(0x2434770)==reinterpret_cast<std::uintptr_t>(relocated+offset));
    Elf64_Ehdr eh;std::memcpy(&eh,retail.data(),sizeof(eh));bool copied=false;
    for(unsigned i=0;i<eh.e_phnum;++i) {
        Elf64_Phdr p;std::memcpy(&p,retail.data()+eh.e_phoff+i*sizeof(p),sizeof(p));
        if(p.p_type==PT_LOAD && p.p_vaddr<=offset && offset-p.p_vaddr+6<=p.p_filesz) {
            std::memcpy(relocated+offset,retail.data()+p.p_offset+offset-p.p_vaddr,6);copied=true;
        }
    }
    const std::array<std::uint8_t,6> prologue{0x55,0x48,0x89,0xe5,0x41,0x57};
    std::array<std::uint8_t,6> observed{};
    assert(copied && actual->Read(0x2434770,observed.data(),observed.size()) && observed==prologue);
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    assert(!mprotect(relocated+(offset&~(page-1)),page,PROT_READ|PROT_EXEC));
    auto changed=prologue;changed[0]=0x90;
    assert(actual->Patch(0x2434770,prologue,changed)==Result::Applied);
    assert(actual->Patch(0x2434770,prologue,changed)==Result::AlreadyApplied);
    assert(actual->Patch(0x2434770,changed,prologue)==Result::Applied);
    assert(actual->Write(0x2434770,changed)==Result::Refused); // live text is never writable data
    assert(actual->Write(0x2434770,changed,true)==Result::Applied);
    assert(actual->Write(0x2434770,prologue,true)==Result::Applied);
    assert(actual->Read(0x2434770,observed.data(),observed.size()) && observed==prologue);
    // A real ELF writable segment (including BSS) accepts live BN-addressed
    // data changes. Host/runtime pointers and unrecognized images stay refused.
    std::size_t data_offset=0;
    for (unsigned i=0;i<eh.e_phnum;++i) {
        Elf64_Phdr p;std::memcpy(&p,retail.data()+eh.e_phoff+i*sizeof(p),sizeof(p));
        if (p.p_type==PT_LOAD && (p.p_flags&PF_W) && !(p.p_flags&PF_X) && p.p_memsz>page*4) {
            data_offset=(p.p_vaddr+page-1)&~(page-1);break;
        }
    }
    assert(data_offset);
    auto* data=relocated+data_offset;
    const auto bn=0x400000+data_offset;
    const std::array<std::uint8_t,4> values{12,34,56,78};
    assert(actual->Write(bn,values)==Result::Applied);
    assert(!std::memcmp(data,values.data(),values.size()));
    assert(actual->Write(bn,values)==Result::AlreadyApplied);
    assert(actual->Write(reinterpret_cast<std::uintptr_t>(data),values)==Result::Refused);
    assert(actual->Write(0x400000+capacity,values)==Result::Refused);
    assert(actual->Write(std::numeric_limits<std::uint64_t>::max()-1,values)==Result::Refused);
    assert(actual->Write(bn,{})==Result::Refused);
    assert(actual->Write(bn,std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(1),4))==Result::Refused);
    assert(!std::memcmp(data,values.data(),values.size()));
    std::memset(data+page-2,0x41,4);
    assert(!mprotect(data+page,page,PROT_READ));
    assert(actual->Write(bn+page-2,values)==Result::Refused);
    for (unsigned i=0;i<4;++i) assert(data[page-2+i]==0x41);
    assert(!mprotect(data,page,PROT_READ|PROT_WRITE|PROT_EXEC));
    assert(actual->Write(bn,values)==Result::Refused);
    assert(!mprotect(data,page*2,PROT_READ|PROT_WRITE));
    assert(!actual->GuestAddress(0x400000+capacity));
    retail.push_back(0x6c);
    auto equivalent=Image::Create(retail,relocated,capacity);
    assert(equivalent && equivalent->Is109() && equivalent->Sha256()!=actual->Sha256());
    assert(!munmap(relocated,capacity));
}
