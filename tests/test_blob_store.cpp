// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/blob_store.h"
#include "common/elf_info.h"
#include "common/path_util.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <iterator>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using BbCache::BlobType;

int main() {
    const auto parent=fs::current_path()/"test-output";
    fs::create_directories(parent);
    auto pattern=(parent/"blob-store-XXXXXX").string();
    assert(mkdtemp(pattern.data()));
    const fs::path root=pattern;
    assert(setenv("BB_GPU_USER_DIR",pattern.c_str(),1)==0);
    Common::FS::SetUserPath(Common::FS::PathType::CacheDir,root);
    Common::ElfInfo::Instance().Set("TEST", "", 0, 0);
    auto& store=BbCache::BlobStore::Instance();
    assert(!store.Flush());
    store.Open();
    assert(store.Flush());
    // A child opening the cache after Flush must see the complete profile,
    // including repeated writes to the same name in FIFO order.
    for (unsigned i=0;i<128;++i)
        assert(store.Save(BlobType::ShaderProfile,"profile",std::vector<u8>(4096,u8(i))));
    assert(store.Flush());
    {
        std::ifstream file(store.Directory()/"profile.bin",std::ios::binary);
        const std::vector<u8> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(bytes==std::vector<u8>(4096,127));
    }
    const auto published_profile=store.Directory()/"profile.bin";
    const pid_t child=fork();
    assert(child>=0);
    if (child==0) {
        // Use only async-signal-safe calls after forking the writer process.
        const int fd=open(published_profile.c_str(),O_RDONLY);
        std::array<u8,4096> bytes{};
        const auto n=fd<0 ? -1 : read(fd,bytes.data(),bytes.size());
        bool valid=n==ssize_t(bytes.size());
        for (auto value:bytes) valid=valid && value==127;
        if (fd>=0) close(fd);
        _exit(valid ? 0 : 1);
    }
    int status=0;
    assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);

    // A FIFO holds a real write after the worker has removed it from the
    // queue. An empty queue alone must not release a flush barrier.
    const auto pipe=store.Directory()/"held.bin.tmp";
    assert(mkfifo(pipe.c_str(),0600)==0);
    const int reader=open(pipe.c_str(),O_RDONLY|O_NONBLOCK);
    assert(reader>=0);
    const int pipe_capacity=fcntl(reader,F_GETPIPE_SZ);
    assert(pipe_capacity>0);
    const auto size=std::size_t(pipe_capacity)*4;
    assert(store.Save(BlobType::ShaderProfile,"held",std::vector<u8>(size,0x5a)));
    pollfd ready{reader,POLLIN,0};
    assert(poll(&ready,1,5000)==1 && (ready.revents&POLLIN));
    auto barrier=std::async(std::launch::async,[&] { return store.Flush(); });
    auto second_barrier=std::async(std::launch::async,[&] { return store.Flush(); });
    assert(barrier.wait_for(100ms)==std::future_status::timeout);
    assert(second_barrier.wait_for(100ms)==std::future_status::timeout);
    std::array<u8,4096> buffer;
    std::size_t received=0;
    while (received<size) {
        ready.revents=0;
        assert(poll(&ready,1,5000)==1 && (ready.revents&POLLIN));
        const auto bytes=read(reader,buffer.data(),buffer.size());
        assert(bytes>0);
        for (ssize_t i=0;i<bytes;++i) assert(buffer[i]==0x5a);
        received+=std::size_t(bytes);
    }
    assert(barrier.wait_for(5s)==std::future_status::ready && barrier.get());
    assert(second_barrier.wait_for(5s)==std::future_status::ready && second_barrier.get());
    assert(close(reader)==0);
    assert(fs::exists(store.Directory()/"held.bin"));

    // An unwritable replacement cannot be published as a successful flush,
    // and the previous profile remains intact.
    fs::create_directory(store.Directory()/"profile.bin.tmp");
    assert(store.Save(BlobType::ShaderProfile,"profile",std::vector<u8>{1,2,3}));
    assert(!store.Flush());
    {
        std::ifstream file(store.Directory()/"profile.bin",std::ios::binary);
        const std::vector<u8> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(bytes==std::vector<u8>(4096,127));
    }
    store.Close();
    assert(!store.Flush());
    store.Open();
    assert(store.Save(BlobType::ShaderProfile,"after",std::vector<u8>{4,5,6}));
    assert(store.Flush()); // a new session has no stale failure state
    // Close must still drain writes without a preceding Flush.
    assert(store.Save(BlobType::ShaderProfile,"last",std::vector<u8>{7,8,9}));
    const auto last=store.Directory()/"last.bin";
    store.Close();
    std::ifstream file(last,std::ios::binary);
    const std::vector<u8> bytes((std::istreambuf_iterator<char>(file)),{});
    assert((bytes==std::vector<u8>{7,8,9}));
    file.close();
    {
        BbCache::BlobStore scoped;
        scoped.Open();
        assert(scoped.Save(BlobType::ShaderProfile,"destructor",std::vector<u8>{10,11,12}));
    } // destruction must wake/drain/join a worker even without explicit Close
    std::ifstream final_file(last.parent_path()/"destructor.bin",std::ios::binary);
    const std::vector<u8> final_bytes((std::istreambuf_iterator<char>(final_file)),{});
    assert((final_bytes==std::vector<u8>{10,11,12}));
    final_file.close();
    fs::remove_all(root);
}
