// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/blob_store.h"
#include "shim/bb_env.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fmt/format.h>

#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/thread.h"

namespace BbCache {

namespace {

const char* Extension(BlobType type) {
    switch (type) {
    case BlobType::ShaderMeta:
        return ".meta";
    case BlobType::ShaderBinary:
        return ".spv";
    case BlobType::PipelineKey:
        return ".key";
    case BlobType::ShaderProfile:
        return ".bin";
    case BlobType::ShaderInput:
        return ".in";
    }
    return ".blob";
}

} // namespace

BlobStore& BlobStore::Instance() {
    static BlobStore store;
    return store;
}

void BlobStore::Open() {
    if (opened) {
        return;
    }
    dir = Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
          Common::ElfInfo::Instance().GameSerial();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    submitted = completed = first_failed = 0;
    writer = std::jthread([this](std::stop_token stop) { Writer(stop); });
    opened = true;
}

void BlobStore::Close() {
    if (!opened) {
        return;
    }
    FinishPreload();
    writer.request_stop();
    cv.notify_all();
    writer.join();
    opened = false;
    LOG_INFO(Render, "Pipeline cache written");
}

bool BlobStore::Flush() {
    if (!opened) return false;
    std::unique_lock lk{mutex};
    const u64 target = submitted;
    flushed.wait(lk, [&] { return completed >= target; });
    return !first_failed || first_failed > target;
}

std::filesystem::path BlobStore::PathOf(BlobType type, const std::string& name) const {
    return dir / (name + Extension(type));
}

bool BlobStore::Save(BlobType type, const std::string& name, std::vector<u8>&& data) {
    return opened && Queue(PathOf(type, name), {}, &data);
}

bool BlobStore::Save(BlobType type, const std::string& name, std::vector<u32>&& data) {
    return opened && Queue(PathOf(type, name),
                           {reinterpret_cast<const u8*>(data.data()), data.size() * sizeof(u32)},
                           nullptr);
}

bool BlobStore::Queue(std::filesystem::path path, std::span<const u8> bytes,
                      std::vector<u8>* owned) {
    {
        std::scoped_lock ahead_lk{ahead_mutex};
        if (!ahead.empty()) {
            ahead.erase(path.filename().string());
        }
    }
    Write write{std::move(path), owned ? std::move(*owned)
                                       : std::vector<u8>(bytes.begin(), bytes.end())};
    {
        std::scoped_lock lk{mutex};
        queue.push_back(std::move(write));
        ++submitted;
    }
    cv.notify_one();
    return true;
}

void BlobStore::Writer(std::stop_token stop) {
    Common::SetCurrentThreadName("bb:PipeCacheIO");
    std::unique_lock lk{mutex};
    for (;;) {
        cv.wait(lk, [&] { return !queue.empty() || stop.stop_requested(); });
        if (queue.empty()) {
            return; // stopping with nothing left to write
        }
        Write write = std::move(queue.front());
        queue.pop_front();
        lk.unlock();
        const bool success = WriteFile(write);
        lk.lock();
        ++completed;
        if (!success && !first_failed) first_failed = completed;
        flushed.notify_all();
    }
}

bool BlobStore::WriteFile(const Write& write) {
    using namespace Common::FS;
    auto temp = write.path;
    temp += ".tmp";
    bool written = false;
    {
        const IOFile file{temp, FileAccessMode::Create};
        written = file.IsOpen() && file.Write(write.bytes) == write.bytes.size();
    }
    std::error_code ec;
    if (written) {
        std::filesystem::rename(temp, write.path, ec);
    }
    if (!written || ec) {
        LOG_ERROR(Render, "Failed to write {}", write.path.string());
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

void BlobStore::ReadAhead() {
    if (!opened || bb_env_off("BB_CACHE_READ_AHEAD")) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator{dir, ec}) {
        const auto ext = entry.path().extension();
        if (ext == ".meta" || ext == ".spv" || ext == ".key") {
            files.push_back(entry.path());
        }
    }
    if (files.empty()) {
        return;
    }
    constexpr u64 MaxBlobBytes = 64ull << 20;
    std::vector<std::vector<u8>> data(files.size());
    std::vector<u8> read(files.size());
    std::atomic<size_t> next{0};
    std::atomic<u64> bytes{0};
    const auto worker = [&] {
        for (size_t i; (i = next.fetch_add(1, std::memory_order_relaxed)) < files.size();) {
            const Common::FS::IOFile file{files[i], Common::FS::FileAccessMode::Read};
            if (!file.IsOpen()) {
                continue;
            }
            const u64 size = file.GetSize();
            if (size > MaxBlobBytes) {
                continue;
            }
            data[i].resize(size);
            if (file.Read(data[i]) != size) {
                data[i] = {};
                continue;
            }
            read[i] = 1;
            bytes.fetch_add(size, std::memory_order_relaxed);
        }
    };
    const u32 threads = u32(std::clamp<size_t>(files.size() / 64, 1, 16));
    std::vector<std::jthread> pool;
    for (u32 t = 1; t < threads; ++t) {
        pool.emplace_back(worker);
    }
    worker();
    pool.clear();
    size_t taken = 0;
    {
        std::scoped_lock lk{ahead_mutex};
        ahead.clear();
        ahead.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            if (read[i]) {
                ahead.emplace(files[i].filename().string(), std::move(data[i]));
                ++taken;
            }
        }
    }
    LOG_INFO(Render, "GPU cache: {} of {} blobs read ahead in {:.2f} s on {} threads ({:.1f} MiB)",
             taken, files.size(),
             std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
             threads, double(bytes.load()) / (1024.0 * 1024.0));
}

void BlobStore::FinishPreload() {
    std::scoped_lock lk{ahead_mutex};
    decltype(ahead){}.swap(ahead);
}

template <typename T>
void BlobStore::LoadInto(BlobType type, const std::string& name, std::vector<T>& data) {
    if (!opened) {
        return;
    }
    const auto path = PathOf(type, name);
    {
        std::scoped_lock lk{ahead_mutex};
        if (const auto it = ahead.find(path.filename().string()); it != ahead.end()) {
            const size_t count = it->second.size() / sizeof(T);
            data.resize(count);
            if (count != 0) {
                std::memcpy(data.data(), it->second.data(), count * sizeof(T));
            }
            return;
        }
    }
    using namespace Common::FS;
    const IOFile file{path, FileAccessMode::Read};
    if (!file.IsOpen()) {
        data.clear();
        return;
    }
    data.resize(file.GetSize() / sizeof(T));
    file.Read(data);
}

void BlobStore::Load(BlobType type, const std::string& name, std::vector<u8>& data) {
    LoadInto(type, name, data);
}

void BlobStore::Load(BlobType type, const std::string& name, std::vector<u32>& data) {
    LoadInto(type, name, data);
}

void BlobStore::ForEachBlob(BlobType type,
                            const std::function<void(std::vector<u8>&& data)>& func) {
    const std::string ext = Extension(type);
    {
        std::vector<std::vector<u8>> buffered;
        {
            std::scoped_lock lk{ahead_mutex};
            if (!ahead.empty()) {
                for (const auto& [name, bytes] : ahead) {
                    if (name.ends_with(ext)) {
                        buffered.push_back(bytes);
                    }
                }
            }
        }
        if (!buffered.empty()) {
            for (auto& bytes : buffered) {
                func(std::move(bytes));
            }
            return;
        }
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator{dir, ec}) {
        if (entry.path().extension() != ext) {
            continue;
        }
        using namespace Common::FS;
        const IOFile file{entry.path(), FileAccessMode::Read};
        if (file.IsOpen()) {
            std::vector<u8> data(file.GetSize());
            file.Read(data);
            func(std::move(data));
        }
    }
}

void BlobStore::Clear() {
    if (!opened) {
        return;
    }
    FinishPreload();
    std::error_code ec;
    u64 removed = 0, kept = 0;
    // The old build's metadata and pipeline keys are kept in previous/: the next shader recipe
    // import rebuilds what it can of them for this build (ImportRecipes, MergeMetas). A cache of
    // another version used to be lost whole (2026-10-27: 1531 files of a player's own cache, the
    // pipelines the recipes lacked compiled again while they played).
    const auto previous = dir / "previous";
    std::filesystem::remove_all(previous, ec);
    std::filesystem::create_directories(previous, ec);
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto ext = entry.path().extension();
        if (ext == ".in") {
            continue; // the inputs translate again for this build (shader_rebuild.h)
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (ext == ".meta" || ext == ".key") {
            std::filesystem::rename(entry.path(), previous / entry.path().filename(), ec);
            if (!ec) {
                ++kept;
                continue;
            }
        }
        if (std::filesystem::remove(entry.path(), ec)) {
            ++removed;
        }
    }
    if (kept == 0) {
        std::filesystem::remove_all(previous, ec);
    }
    // Shader recipes translated for the old profile (shader_rebuild.h): translated again.
    std::filesystem::remove_all(dir / "import", ec);
    std::filesystem::remove_all(dir / "import.tmp", ec);
    LOG_WARNING(Render, "Pipeline cache cleared ({} files): it is rebuilt for this build{}", removed,
                kept ? fmt::format(" ({} metadata and pipeline files kept in previous/ for the "
                                   "shader recipe import)",
                                   kept)
                     : std::string{});
}

} // namespace BbCache
