// SPDX-License-Identifier: GPL-2.0-or-later
// Offline preparation only. Runtime altar requests still require the host
// service and localized messages; this tool never changes the original dump.
#include "games/bloodborne/esd.h"
#include "games/bloodborne/rebirth_script.h"
#include "common/sha256.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main(int argc, char** argv) {
    if ((argc != 3 || std::strcmp(argv[1], "check")) &&
        (argc != 4 || std::strcmp(argv[1], "rebirth"))) {
        std::cerr << "Usage: rebirth-script-tool check INPUT.esd\n"
                     "       rebirth-script-tool rebirth INPUT.esd NEW_OUTPUT.esd\n";
        return 2;
    }
    std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > (64 << 20)) {
        std::cerr << "Cannot read script, or script exceeds 64 MiB\n";
        return 1;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        std::cerr << "Cannot read complete script\n";
        return 1;
    }
    std::string why;
    if (argc == 3) {
        esd::Script script;
        if (!esd::parse(bytes, script, &why) || esd::serialize(script) != bytes) {
            std::cerr << "Script does not round-trip: " << why << '\n';
            return 1;
        }
    } else {
        if (!rebirth_script(bytes, &why)) {
            std::cerr << "Rebirth preparation refused: " << why << '\n';
            return 1;
        }
        // Never truncate a dump file or a previously generated result, including
        // symlinks. A partial new output is removed if writing fails.
        const int fd = open(argv[3], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) { std::cerr << "Cannot create new output: " << std::strerror(errno) << '\n'; return 1; }
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto n = write(fd, bytes.data() + done, bytes.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            done += static_cast<std::size_t>(n);
        }
        const bool flushed = done == bytes.size() && fsync(fd) == 0;
        const bool closed = close(fd) == 0;
        if (!flushed || !closed) {
            unlink(argv[3]);
            std::cerr << "Cannot write complete output\n";
            return 1;
        }
    }
    std::cout << sha256_hex(bytes.data(), bytes.size()) << '\n';
}
