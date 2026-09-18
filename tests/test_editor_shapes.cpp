/**
 * @file test_editor_shapes.cpp
 * @brief Random edit shapes over both geometries, each priced against the
 *        repaint of the screen it produced. An incremental edit that costs
 *        more than that repaint is a move the editor announced wrongly or
 *        the terminal applied for nothing. `PROBE_N` widens the search.
 */
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "doctest.h"
#include "libbotanik.h"
#include "editor_fixture.h"
namespace tests { namespace editor_shapes {
using botanik_test::EditorFixture;
using botanik_test::Lcg;
using botanik_test::seek;
static std::string words(Lcg& r, int n, int nl_pct) {
    static const char* W[] = {"the","north","vents","open","when","roof","sensor","reads","above","day","target","and","they","close","on","rain","or","wind","storm","threshold","status","set","zone","humidity","ventilation"};
    std::string s;
    for (int i = 0; i < n; i++) { if (i) s += (int)r.below(100) < nl_pct ? "\n" : " "; s += W[r.below(25)]; }
    return s;
}
struct Case { const char* op; size_t got, repaint; std::string dump; uint32_t seed; bool notes; };
TEST_CASE("editor: no edit costs more than the repaint of its screen") {
    std::vector<Case> dear; size_t total = 0, worst_ratio_pct = 0;
    const int N = getenv("PROBE_N") ? atoi(getenv("PROBE_N")) : 400;
    for (uint32_t seed = 1; seed <= (uint32_t)N; seed++) {
        Lcg r(seed * 2654435761u);
        const bool notes = r.below(2) == 0;
        EditorFixture f(24, 80, notes, notes);
        const std::string text = words(r, 3 + (int)r.below(notes ? 60 : 30), notes ? 12 : 3);
        f.editor.insert_characters(text.data(), (uint16_t)text.size());
        const uint16_t pos = (uint16_t)r.below((uint32_t)text.size() + 1);
        seek(f, pos);
        (void)f.repaint_from_scratch(); f.drain();
        const char* op = "";
        switch (r.below(8)) {
            case 0: op = "insert char"; { char c = 'a' + (char)r.below(26); f.editor.insert_characters(&c, 1); } break;
            case 1: op = "insert newline"; f.editor.insert_characters("\n", 1); break;
            case 2: op = "backspace"; (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::BACKSPACE, 1); break;
            case 3: op = "delete_forward"; (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::DELETE, 1); break;
            case 4: op = "kill_to_line_end"; (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::CTRL_K); break;
            case 5: op = "kill_word_back"; (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::CTRL_W); break;
            case 6: op = "insert word"; { std::string w = " " + words(r, 1, 0); f.editor.insert_characters(w.data(), (uint16_t)w.size()); } break;
            case 7: op = "backspace x3"; (void)f.editor.handle_key(Botanik::Terminal::InputEvent::Command::BACKSPACE, 3); break;
        }
        const std::string bytes = f.pump();
        f.check_wire(op);
        const size_t repaint = f.repaint_from_scratch().size();
        total += bytes.size();
        if (repaint && bytes.size() * 100 / repaint > worst_ratio_pct) worst_ratio_pct = bytes.size() * 100 / repaint;
        if (bytes.size() > repaint) dear.push_back({op, bytes.size(), repaint, botanik_test::shown(bytes), seed, notes});
    }
    std::sort(dear.begin(), dear.end(), [](const Case& a, const Case& b) { return a.got * 1000 / a.repaint > b.got * 1000 / b.repaint; });
    for (size_t i = 0; i < dear.size() && i < 12; i++) {
        INFO("seed " << dear[i].seed << " " << (dear[i].notes ? "NOTES" : "SHELL") << " " << dear[i].op
             << ": " << dear[i].got << " bytes against a " << dear[i].repaint << "-byte repaint\n  " << dear[i].dump);
        CHECK(dear[i].got <= dear[i].repaint);
    }
    if (getenv("BOTANIK_LEDGER") != nullptr)
        MESSAGE("edit shapes: " << N << " cases, " << total << " bytes, worst " << worst_ratio_pct << "% of a repaint");
}
}}
