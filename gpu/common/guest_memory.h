// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/uio.h>
#include <unistd.h>

namespace BbGuestMemory {
using Address = std::uintptr_t;
inline bool Range(Address at,std::size_t size) { return at>=0x10000 && size<=UINTPTR_MAX-at; }
inline bool Read(void*,Address at,void* destination,std::size_t size) {
    if (!Range(at,size)) return false;
    iovec local{destination,size},remote{reinterpret_cast<void*>(at),size};
    return process_vm_readv(getpid(),&local,1,&remote,1,0)==ssize_t(size);
}
template<class T> bool Read(Address at,T& value) { return Read(nullptr,at,&value,sizeof value); }
inline bool Write(Address at,const void* source,std::size_t size) {
    if (!Range(at,size)) return false;
    iovec local{const_cast<void*>(source),size},remote{reinterpret_cast<void*>(at),size};
    return process_vm_writev(getpid(),&local,1,&remote,1,0)==ssize_t(size);
}
}
