// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the shipped module's guest imports over loopback only. Adapted
// from BBHOST bdc5cdf tests/net_p2p_test.cpp; no account or server is used.
#include "bbnet20/bbnet.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

// No guest image is mapped by this socket-only test.
extern "C" int runtime_memory_low_info(std::uintptr_t,int*,std::uintptr_t*) { return 0; }

namespace {
struct Address {
    std::uint8_t len=16,family=2;
    std::uint16_t port;
    std::uint32_t addr;
    std::uint16_t vport;
    std::uint8_t zero[6]{};
};
static_assert(sizeof(Address)==16);
template<class F> F Resolve(BbNetResolveFn resolve,const char* nid) {
    const auto fn=resolve(nid);assert(fn);return reinterpret_cast<F>(fn);
}
int FreePort() {
    const int fd=socket(AF_INET,SOCK_DGRAM,0);assert(fd>=0);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0);
    socklen_t size=sizeof(addr);assert(getsockname(fd,reinterpret_cast<sockaddr*>(&addr),&size)==0);
    const int port=ntohs(addr.sin_port);close(fd);return port;
}
void Chunk(std::vector<std::uint8_t>& out,unsigned kind,unsigned body) {
    out.push_back(kind);out.push_back(body>>8);out.push_back(body);
    for (unsigned i=0;i<body;++i) out.push_back(i*31+kind);
}
}

int main(int argc,char** argv) {
    assert(argc==2);
    setenv("BB_ONLINE","1",1);setenv("BB_NET_BACKEND","thd",1);
    setenv("BB_THD_STUN","off",1);setenv("BB_BBSC","0",1);
    const int port=FreePort();
    setenv("BB_P2P_PORT",std::to_string(port).c_str(),1);
    const bool large=!std::getenv("BB_NET_LARGE_DATAGRAMS") || std::getenv("BB_NET_LARGE_DATAGRAMS")[0]!='0';
    const bool split=large && (!std::getenv("BB_NET_SPLIT_DATAGRAMS") || std::getenv("BB_NET_SPLIT_DATAGRAMS")[0]!='0');
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!module) { std::fprintf(stderr,"dlopen: %s\n",dlerror());return 1; }
    auto init=reinterpret_cast<BbNetInitFn>(dlsym(module,"bbnet_init"));
    auto resolve=reinterpret_cast<BbNetResolveFn>(dlsym(module,"bbnet_resolve"));assert(init && resolve);
    const BbNetHost host{BBNET_ABI_VERSION,"","CUSA03173","Loopback test","01.09",
        +[](void**,void*(*)(void*),void*,std::uint64_t,const char*)->std::int32_t { std::abort(); },
        +[](void*,void**)->std::int32_t { std::abort(); },
        +[](void*) { std::abort(); },+[](std::uintptr_t,std::uint64_t) { return 0; }};
    assert(init(&host)==0);
    auto net_init=Resolve<int(*)()>(resolve,"Nlev7Lg8k3A");
    auto create=Resolve<int(*)(const char*,int,int,int)>(resolve,"Q4qBuN-c0ZM");
    auto bind_sock=Resolve<int(*)(int,const void*,int)>(resolve,"bErx49PgxyY");
    auto send=Resolve<int(*)(int,const void*,std::uint64_t,int,const void*,int)>(resolve,"gvD1greCu0A");
    auto recv=Resolve<int(*)(int,void*,std::uint64_t,int,void*,unsigned*)>(resolve,"304ooNZxWDY");
    auto net_errno=Resolve<int*(*)()>(resolve,"HQOwnfMGipQ");
    auto close_sock=Resolve<int(*)(int)>(resolve,"45ggEzakPJQ");
    assert(net_init()==0);
    for (const unsigned first : {30,300}) {
        const int a=create("loopback a",2,6,0),b=create("loopback b",2,6,0);assert(a>=0 && b>=0);
        Address bind_a{16,2,htons(port),0,htons(first)},bind_b{16,2,htons(port),0,htons(first+1)};
        assert(bind_sock(a,&bind_a,16)==0 && bind_sock(b,&bind_b,16)==0);
        Address to{16,2,htons(port),htonl(INADDR_LOOPBACK),htons(first+1)};
        const auto receive=[&](std::vector<std::uint8_t>& data) {
            Address from{};unsigned size=16;
            for (int wait=0;wait<300;++wait) {
                const int n=recv(b,data.data(),data.size(),0x80,&from,&size);
                if (n>=0) { assert(ntohs(from.vport)==first);return n; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return -1;
        };
        const unsigned header=first<256 ? 4 : 6;
        const unsigned maximum=(large ? 65507 : 2048)-header;
        for (unsigned n : {0u,1u,64u,1932u,2208u,4140u,maximum}) {
            std::vector<std::uint8_t> data(n,0xff),in(n+64);
            const int result=send(a,data.data(),n,0,&to,16);
            if (n>maximum) { assert(result<0 && *net_errno()==40);continue; }
            assert(result==int(n));
            assert(receive(in)==int(n) && std::equal(data.begin(),data.end(),in.begin()));
        }
        for (const int pairs : {7,8,15}) {
            std::vector<std::uint8_t> data;
            for (int i=0;i<pairs;++i) { Chunk(data,1,251);Chunk(data,1,19); }
            const int result=send(a,data.data(),data.size(),0,&to,16);
            if (data.size()>maximum) { assert(result<0 && *net_errno()==40);continue; }
            assert(result==int(data.size()));
            std::vector<std::uint8_t> joined;
            unsigned pieces=0;
            while (joined.size()<data.size()) {
                std::vector<std::uint8_t> in(65536);const int n=receive(in);assert(n>0);
                // Four 276-byte pairs fit per piece, with no partial chunk.
                assert(unsigned(n)==(split ? std::min<std::size_t>(1104,data.size()-joined.size()) : data.size()));
                joined.insert(joined.end(),in.begin(),in.begin()+n);++pieces;
            }
            assert(joined==data && pieces==(split ? unsigned((pairs+3)/4) : 1));
        }
        {
            std::vector<std::uint8_t> data;
            Chunk(data,0,85);Chunk(data,1,1238);Chunk(data,2,40);Chunk(data,3,1238);Chunk(data,1,6);
            const int result=send(a,data.data(),data.size(),0,&to,16);
            if (!large) assert(result<0 && *net_errno()==40);
            else {
                assert(result==int(data.size()));
                const std::vector<unsigned> lengths=split ? std::vector<unsigned>{88,1241,43,1241,9}
                                                         : std::vector<unsigned>{unsigned(data.size())};
                std::vector<std::uint8_t> joined;
                for (const auto length : lengths) {
                    std::vector<std::uint8_t> in(65536);const int n=receive(in);assert(n==int(length));
                    joined.insert(joined.end(),in.begin(),in.begin()+n);
                }
                assert(joined==data);
            }
        }
        // Valid initial chunks followed by a truncated chunk must go whole;
        // the prefix must never be sent once before discovering the bad tail.
        {
            std::vector<std::uint8_t> data;
            for (int i=0;i<4;++i) Chunk(data,1,251);
            data.insert(data.end(),{2,0x10,0x00,1});data.resize(1300,0xff);
            std::vector<std::uint8_t> in(65536);
            assert(send(a,data.data(),data.size(),0,&to,16)==int(data.size()));
            assert(receive(in)==int(data.size()) && std::equal(data.begin(),data.end(),in.begin()));
        }
        *net_errno()=0;
        const std::array<std::uint8_t,1> byte{0xff};
        assert(send(a,byte.data(),maximum+1,0,&to,16)<0 && *net_errno()==40);
        assert(send(a,byte.data(),std::numeric_limits<std::uint64_t>::max(),0,&to,16)<0 && *net_errno()==40);
        assert(send(a,nullptr,1,0,&to,16)<0 && *net_errno()==EFAULT);
        std::array<std::uint8_t,16> sink{};
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        assert(recv(b,sink.data(),sink.size(),0x80,nullptr,nullptr)<0);
        assert(close_sock(a)==0 && close_sock(b)==0);
    }
    std::printf("THD loopback: full payload integrity, whole-chunk splitting, both vport headers and guest errno PASS (large=%d, split=%d)\n",large,split);
}
