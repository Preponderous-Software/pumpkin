// Saves: the porch the page keeps in this browser (localStorage
// 'pumpkin-porch-v1', a JSON list of carving codes) and every #p= link ever
// shared hold carving codes. Each frozen fixture in tests/fixtures/, one per
// code version, must still load exactly: every pumpkin on it decodes to what
// it held when the fixture was written, and a fixture of the current version
// encodes back to the same bytes. A format change that cannot read one fails
// the build (tests/fixtures/README.md).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "carve/carving.h"
#include "check.h"

using namespace pumpkin;

namespace {

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// The porch as JSON.stringify wrote it: ["code","code",...]. Codes are
// base64url, so there is nothing to unescape. False when it is not that.
bool porchCodes(const std::string& json, std::vector<std::string>& out) {
    if (json.size() < 2 || json.front() != '[' || json.back() != ']') return false;
    size_t i = 1;
    while (i < json.size() - 1) {
        if (json[i] != '"') return false;
        const size_t end = json.find('"', i + 1);
        if (end == std::string::npos) return false;
        out.push_back(json.substr(i + 1, end - i - 1));
        i = end + 1;
        if (json[i] == ',') ++i;
    }
    return true;
}

// Every value a carving decodes to (variety; each piece's shape, mirror,
// place, size and turn; each cut's points), to 1/10000 of a carve unit or
// radian, folded with FNV-1a: what the pumpkin looks like, not how it is coded.
uint64_t fingerprint(const Carving& c) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](int64_t v) {
        for (int b = 0; b < 8; ++b) {
            h ^= static_cast<uint64_t>(v >> (8 * b)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    auto q = [](float f) { return static_cast<int64_t>(std::lround(static_cast<double>(f) * 10000.0)); };
    mix(c.variety);
    for (const Op& op : c.ops) {
        mix(op.isStamp);
        if (op.isStamp) {
            mix(op.stamp.shape);
            mix(op.stamp.flip);
            mix(q(op.stamp.at.x));
            mix(q(op.stamp.at.y));
            mix(q(op.stamp.scale));
            mix(q(op.stamp.angle));
        } else {
            mix(op.cut.closed);
            mix(static_cast<int64_t>(op.cut.points.size()));
            for (const Vec2& p : op.cut.points) {
                mix(q(p.x));
                mix(q(p.y));
            }
        }
    }
    return h;
}

// What one pumpkin on a fixture's porch holds.
struct Expect {
    Variety variety;
    size_t ops;
    int stamps, closedCuts, openCuts, cutPoints;
    uint64_t print;
};

void checkPorch(const char* name, int version, const std::vector<Expect>& want) {
    std::printf("fixture %s\n", name);
    const std::string json = readAll(std::string(PUMPKIN_FIXTURES) + "/" + name);
    CHECK(!json.empty());
    std::vector<std::string> codes;
    CHECK(porchCodes(json, codes));
    CHECK(codes.size() == want.size());
    for (size_t k = 0; k < codes.size() && k < want.size(); ++k) {
        Carving c;
        CHECK(decode(codes[k], c));
        int stamps = 0, closed = 0, open = 0, points = 0;
        for (const Op& op : c.ops) {
            if (op.isStamp) {
                ++stamps;
            } else {
                ++(op.cut.closed ? closed : open);
                points += static_cast<int>(op.cut.points.size());
            }
        }
        CHECK(c.variety == want[k].variety);
        CHECK(c.ops.size() == want[k].ops);
        CHECK(stamps == want[k].stamps);
        CHECK(closed == want[k].closedCuts);
        CHECK(open == want[k].openCuts);
        CHECK(points == want[k].cutPoints);
        if (fingerprint(c) != want[k].print)
            std::printf("  pumpkin %zu: fingerprint %016llx, expected %016llx\n", k,
                        static_cast<unsigned long long>(fingerprint(c)), static_cast<unsigned long long>(want[k].print));
        CHECK(fingerprint(c) == want[k].print);
        // Written by this version: nothing is lost or changed on the way back.
        if (version == kCodeVersion) CHECK(encode(c) == codes[k]);
    }
}

}  // namespace

int main() {
    // Code version 1 (served by the arcade since 2026-10-30): an uncarved pumpkin, the classic
    // face, freehand cuts only, every face piece, and a long wavy cut among
    // stamps (tools/make_save_fixtures.cpp).
    checkPorch("v1-porch.save", 1, {
        {OrangeRound, 0, 0, 0, 0, 0, 0x47fe0d7eaf8e51e3ull},
        {WhiteRound, 4, 4, 0, 0, 0, 0x20f1a711825e447full},
        {GreenSquat, 2, 0, 1, 1, 19, 0xd738f8f89a61174dull},
        {OrangeTall, 14, 14, 0, 0, 0, 0xfc3ed1dbebc52f27ull},
        {OrangeSquat, 5, 3, 1, 1, 78, 0xb130d1f3af067946ull},
    });

    // The page still keeps the porch under the key the fixtures stand for, and
    // still reads #p= links: renaming either would lose every porch and link.
    const std::string shell = readAll(PUMPKIN_SHELL);
    CHECK(shell.find("'pumpkin-porch-v1'") != std::string::npos);
    CHECK(shell.find("[#&]p=([A-Za-z0-9_-]+)") != std::string::npos);
    TEST_MAIN_END();
}
