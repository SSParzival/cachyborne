// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport render: the pipeline cache on disk, one file per blob in the user's cache directory
// under the game's serial (name.meta: shader metadata, name.spv: SPIR-V, name.key: pipeline keys,
// profile.bin: the build that wrote them). Writes go to a writer thread, each to a temporary
// file renamed into place, so a crash leaves the old file or none, never a cut-short one.
#pragma once

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "common/types.h"

namespace BbCache {

enum class BlobType : u32 {
    ShaderMeta,
    ShaderBinary,
    PipelineKey,
    ShaderProfile,
    /// A compilation's inputs (shader_capture.h TakeInput): kept when the cache is cleared.
    ShaderInput,
};

class BlobStore {
public:
    static BlobStore& Instance();
    ~BlobStore() { Close(); }

    void Open();
    /// Writes what is queued, then stops the writer.
    void Close();
    /// Waits for writes submitted before this call, including the write already
    /// taken by the worker. False if closed or any prior write failed. This is
    /// a visibility barrier for another process, not an fsync/durability promise.
    /// Open/Close must be serialized with producers and Flush callers.
    bool Flush();
    [[nodiscard]] bool IsOpened() const {
        return opened;
    }
    /// The game's cache directory (after Open).
    [[nodiscard]] const std::filesystem::path& Directory() const {
        return dir;
    }
    /// Loading at start is done (kept for the callers' order; files need no switch).
    void FinishPreload() {}
    /// Removes every blob: an incompatible cache is rebuilt, not ignored.
    void Clear();

    bool Save(BlobType type, const std::string& name, std::vector<u8>&& data);
    bool Save(BlobType type, const std::string& name, std::vector<u32>&& data);
    void Load(BlobType type, const std::string& name, std::vector<u8>& data);
    void Load(BlobType type, const std::string& name, std::vector<u32>& data);
    void ForEachBlob(BlobType type, const std::function<void(std::vector<u8>&& data)>& func);

private:
    struct Write {
        std::filesystem::path path;
        std::vector<u8> bytes;
    };

    std::filesystem::path PathOf(BlobType type, const std::string& name) const;
    bool Queue(std::filesystem::path path, std::span<const u8> bytes, std::vector<u8>* owned);
    void Writer(std::stop_token stop);
    static bool WriteFile(const Write& write);
    template <typename T>
    void LoadInto(BlobType type, const std::string& name, std::vector<T>& data);

    std::filesystem::path dir;
    bool opened = false;
    std::mutex mutex;
    std::condition_variable_any cv;
    std::condition_variable flushed;
    std::deque<Write> queue;
    u64 submitted = 0, completed = 0, first_failed = 0; // guarded by mutex
    std::jthread writer; // joins before the state it accesses is destroyed
};

} // namespace BbCache
