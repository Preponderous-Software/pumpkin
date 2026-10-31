// Links: a carving survives encode -> decode exactly, and anything malformed
// is refused without touching what was there.
#include <cmath>
#include <string>

#include "carve/carving.h"
#include "carve/editor.h"
#include "check.h"

using namespace pumpkin;

int main() {
    // An empty pumpkin is a tiny code.
    {
        Carving c;
        const std::string code = encode(c);
        CHECK(code.size() <= 4);
        Carving back;
        back.variety = GreenSquat;
        CHECK(decode(code, back));
        CHECK(back == c);
    }
    // A face: stamps (quantised by the editor) and freehand cuts.
    {
        Editor e;
        e.setVariety(WhiteRound);
        e.addStamp(TriangleEye);
        e.addStamp(TriangleEye);
        e.addStamp(TriangleNose);
        e.addStamp(GrinMouth);
        e.edit(Edit::TurnLeft);
        e.edit(Edit::Bigger);
        std::vector<Vec2> loop;
        for (int i = 0; i <= 40; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 40;
            loop.push_back({0.5f + 0.1f * std::cos(a), 0.55f + 0.1f * std::sin(a)});
        }
        CHECK(e.cut(loop));
        std::vector<Vec2> slit = {{-0.6f, -0.6f}, {-0.4f, -0.65f}, {-0.2f, -0.6f}};
        CHECK(e.cut(slit));
        const Carving& c = e.carving();
        CHECK(c.ops.size() == 6);
        CHECK(!c.ops[4].isStamp && c.ops[4].cut.closed);
        CHECK(!c.ops[5].isStamp && !c.ops[5].cut.closed);
        const std::string code = encode(c);
        Carving back;
        CHECK(decode(code, back));
        CHECK(back == c);
        CHECK(encode(back) == code);
        CHECK(code.size() < 200);
        for (char ch : code) CHECK((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_');

        // Malformed codes are refused, and the carving given is left alone.
        Carving keep = back;
        CHECK(!decode("", keep));
        CHECK(!decode("!!!!", keep));
        CHECK(!decode("A", keep));
        CHECK(!decode(code.substr(0, code.size() - 3), keep) || keep == back);
        std::string wrongVersion = code;
        wrongVersion[0] = 'B';
        CHECK(!decode(wrongVersion, keep));
        CHECK(keep == back);
        CHECK(!decode(std::string(5000, 'A'), keep));
    }
    // Every truncation of a code either decodes or is refused: never a crash.
    {
        Editor e;
        e.addStamp(StarEye);
        std::vector<Vec2> s;
        for (int i = 0; i < 30; ++i) s.push_back({-0.5f + 0.03f * static_cast<float>(i), 0.1f * std::sin(static_cast<float>(i))});
        e.cut(s);
        const std::string code = encode(e.carving());
        for (size_t n = 0; n <= code.size(); ++n) {
            Carving c;
            decode(code.substr(0, n), c);
        }
        CHECK(true);
    }
    TEST_MAIN_END();
}
