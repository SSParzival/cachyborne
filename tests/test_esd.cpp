// SPDX-License-Identifier: GPL-2.0-or-later
#include "games/bloodborne/esd.h"
#include "games/bloodborne/rebirth_script.h"
#include "common/sha256.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

using Bytes = std::vector<std::uint8_t>;
static void put(Bytes& b, std::size_t at, std::int64_t value) { std::memcpy(b.data() + at, &value, 8); }
static void roundtrip(const Bytes& b) {
    esd::Script s; std::string why;
    assert(esd::parse(b, s, &why));
    assert(esd::serialize(s) == b);
}
int main() {
    assert(sha256_hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const Bytes abc{'a','b','c'};
    assert(sha256_hex(abc.data(), abc.size()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    esd::Script base;
    base.head.resize(0x6c); std::memcpy(base.head.data(), "fsSL", 4);
    base.dhead.resize(0x18); base.dtail.resize(0x10); base.name = {'t',0,0,0};
    const int state = base.state(0);
    base.groups.push_back({123, {state}, -1, esd::kNew});
    const Bytes good = esd::serialize(base);
    roundtrip(good);
    // A condition present only in an unreferenced pointer list must remain in
    // the serialized condition pool, including its expression/commands.
    esd::Script dead_model = base;
    const int orphan = dead_model.cond(state, esd::done(esd::lit(1)));
    dead_model.dead.push_back(dead_model.run({orphan}));
    roundtrip(esd::serialize(dead_model));
    // Extreme signed offsets and counts must fail before arithmetic or loops.
    for (const std::size_t at : {0x84, 0x8c, 0x94, 0x9c, 0xbc, 0xc4, 0xcc, 0xec, 0xf4}) {
        for (const auto value : {std::numeric_limits<std::int64_t>::max(),
                                std::numeric_limits<std::int64_t>::min(), std::int64_t(-2)}) {
            Bytes bad = good; put(bad, at, value);
            esd::Script s; std::string why;
            assert(!esd::parse(bad, s, &why));
        }
    }
    for (std::size_t size = 0; size < good.size(); ++size) {
        esd::Script s; std::string why;
        assert(!esd::parse(Bytes(good.begin(), good.begin() + size), s, &why));
    }
    // Reproducible mutation smoke test; sanitizer execution covers both parser
    // and serialization of every model the parser accepts.
    std::uint32_t random = 0xabcdbb09;
    for (int n = 0; n < 10000; ++n) {
        Bytes b = good;
        for (int change = 0; change < 4; ++change) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            b[random % b.size()] ^= static_cast<std::uint8_t>(random >> 16);
        }
        esd::Script s; std::string why;
        if (esd::parse(b, s, &why)) {
            esd::Script again;
            assert(esd::parse(esd::serialize(s), again, &why));
        }
    }
    Bytes rejected = good; std::string why;
    assert(!rebirth_script(rejected, &why) && rejected == good);
    int scripts = 0, rebirths = 0;
    if (const char* path = std::getenv("CACHYBORNE_ESD_CORPUS")) {
        assert(std::filesystem::is_directory(path));
        std::vector<std::filesystem::path> files;
        for (const auto& file : std::filesystem::directory_iterator(path))
            if (file.path().extension() == ".esd") files.push_back(file.path());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            std::ifstream f(file, std::ios::binary); assert(f);
            Bytes b((std::istreambuf_iterator<char>(f)), {});
            roundtrip(b); ++scripts;
            for (int mutation = 0; mutation < 8; ++mutation) {
                Bytes changed = b;
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                changed[random % changed.size()] ^= static_cast<std::uint8_t>((random >> 16) | 1);
                esd::Script model, reread;
                if (esd::parse(changed, model, &why) && !esd::parse(esd::serialize(model), reread, &why)) {
                    std::cerr << file << " mutation " << mutation << ": " << why << '\n';
                    assert(false);
                }
            }
            if (sha256_hex(b.data(), b.size()) == "e56163a1bb8f46ae59368160e522e11f970e2268f6d356b823e4d72a7c969ff1") {
                assert(rebirth_script(b, &why));
                assert(sha256_hex(b.data(), b.size()) == "e726dec68e8d73c5d5b6ba1875fabd6a85320b6b14f95ca3d6729143a3c362b2");
                roundtrip(b);
                esd::Script s; assert(esd::parse(b,s,&why));
                auto* group = s.group(2147483641); assert(group && group->states.size() == 16);
                const Bytes rewritten = b;
                assert(!rebirth_script(b, &why) && b == rewritten);
                ++rebirths;
            }
        }
        assert(scripts > 0 && rebirths == 1);
    }
    std::cout << "ok: bounded ESD parser, 10000 mutations, hashes; local scripts=" << scripts << " rebirths=" << rebirths << '\n';
}
