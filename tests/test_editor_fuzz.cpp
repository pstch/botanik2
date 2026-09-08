/**
 * @file test_editor_fuzz.cpp
 * @brief Random edit sequences, every step held to the wire and to a repaint.
 *
 * The scenario table in test_editor_wire.cpp is what someone thought of. This
 * is what nobody did: a seeded stream of edits and motions over each editor
 * geometry, checked after every step against the independent VT and, every
 * few steps, against what a full repaint would have drawn. A failure names
 * the seed, the step and the operation, which is enough to replay it.
 */

#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik.h"

#include "editor_fixture.h"

namespace tests { namespace editor_fuzz {

using botanik_test::EditorFixture;

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    uint32_t below(uint32_t n) { return next() % n; }
};

/** @brief The operations, weighted by repetition: what an operator does
 *  most gets drawn most, and the rare bulk edits still come up. */
enum Op : uint8_t {
    INS_CHAR, INS_WORD, INS_NL, INS_PASTE,
    BACKSPACE, DEL_FWD,
    LEFT, RIGHT, UP, DOWN, LINE_START, LINE_END, WORD_LEFT, WORD_RIGHT,
    START, END, PAGE_UP, PAGE_DOWN,
    KILL_EOL, KILL_WORD, YANK, MARK, KILL_REGION, COPY_REGION, REPLACE,
};
static const Op OPS[] = {
    INS_CHAR, INS_CHAR, INS_CHAR, INS_CHAR, INS_CHAR, INS_CHAR, INS_CHAR, INS_CHAR,
    INS_WORD, INS_WORD, INS_WORD, INS_NL, INS_NL, INS_PASTE,
    BACKSPACE, BACKSPACE, BACKSPACE, DEL_FWD, DEL_FWD,
    LEFT, LEFT, RIGHT, RIGHT, UP, DOWN, LINE_START, LINE_END, WORD_LEFT, WORD_RIGHT,
    START, END, PAGE_UP, PAGE_DOWN,
    KILL_EOL, KILL_WORD, YANK, MARK, MARK, KILL_REGION, COPY_REGION, REPLACE,
};
static const char* NAMES[] = {
    "insert char", "insert word", "insert newline", "paste",
    "backspace", "delete_forward",
    "left", "right", "up", "down", "line start", "line end", "word left", "word right",
    "start", "end", "page up", "page down",
    "kill to eol", "kill word", "yank", "toggle mark", "kill region", "copy region", "replace_all",
};

static std::string word(Lcg& rng, uint32_t min, uint32_t max) {
    static const char AL[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const uint32_t n = min + rng.below(max - min + 1);
    std::string s;
    for (uint32_t i = 0; i < n; i++) s += AL[rng.below(36)];
    return s;
}

static Op step(EditorFixture& f, Lcg& rng) {
    Botanik::Terminal::TextEditor& e = f.editor;
    const Op op = OPS[rng.below(sizeof(OPS) / sizeof(OPS[0]))];
    std::string s;
    switch (op) {
    case INS_CHAR:  s = rng.below(5) == 0 ? " " : word(rng, 1, 1);
                    e.insert_characters(s.data(), 1); break;
    case INS_WORD:  s = word(rng, 2, 12) + " ";
                    e.insert_characters(s.data(), (uint16_t)s.size()); break;
    case INS_NL:    e.insert_characters("\n", 1); break;
    case INS_PASTE: {
        s = word(rng, 40, 200);
        // A paste with line breaks in it: the one edit that grows several rows.
        for (uint32_t i = 0; i < s.size(); i++) if (rng.below(30) == 0) s[i] = '\n';
        e.insert_characters(s.data(), (uint16_t)s.size()); break;
    }
    case BACKSPACE:   e.backspace((uint16_t)(1 + rng.below(3))); break;
    case DEL_FWD:     e.delete_forward((uint16_t)(1 + rng.below(3))); break;
    case LEFT:        e.move_left(); break;
    case RIGHT:       e.move_right(); break;
    case UP:          e.move_up(); break;
    case DOWN:        e.move_down(); break;
    case LINE_START:  e.move_to_line_start(); break;
    case LINE_END:    e.move_to_line_end(); break;
    case WORD_LEFT:   e.move_word_left(); break;
    case WORD_RIGHT:  e.move_word_right(); break;
    case START:       e.move_to_start(); break;
    case END:         e.move_to_end(); break;
    case PAGE_UP:     e.page_up(); break;
    case PAGE_DOWN:   e.page_down(); break;
    case KILL_EOL:    (void)e.kill_to_line_end(); break;
    case KILL_WORD:   (void)e.kill_word_back(); break;
    case YANK:        (void)e.yank(); break;
    case MARK:        e.toggle_mark(); break;
    case KILL_REGION: (void)e.kill_region_or_word(); break;
    case COPY_REGION: (void)e.copy_region(); break;
    case REPLACE:     s = word(rng, 0, 150);
                      e.replace_all(s.data(), (uint16_t)s.size()); break;
    }
    return op;
}

struct Geometry { const char* name; bool notes_prompts; bool notes_page; };
static const Geometry GEOMETRIES[] = {
    { "shell",        false, false },
    { "NOTES prompts", true, false },
    { "NOTES page",    true, true  },
};

static void sweep(const Geometry& g, uint32_t seed, uint32_t steps) {
    EditorFixture f(24, 80, g.notes_prompts, g.notes_page);
    Lcg rng(seed);
    for (uint32_t i = 0; i < steps; i++) {
        const Op op = step(f, rng);
        (void)f.pump();
        const std::string diff = f.wire_diff();
        const std::string where = std::string(g.name) + " seed " + std::to_string(seed)
                                + " step " + std::to_string(i) + " (" + NAMES[op] + ")";
        if (!diff.empty()) {
            FAIL(where << ": the wire does not show what FRONT drew\n  " << diff);
            return;
        }
        // Convergence less often, so drift between steps gets a chance to
        // build up before a repaint would hide it.
        if (i % 8 == 7) {
            const std::vector<std::string> inc = f.screen();
            const std::vector<std::string> full = f.repainted();
            for (size_t r = 0; r < inc.size(); r++) {
                if (inc[r] == full[r]) continue;
                FAIL(where << ": row " << r << " differs from a repaint"
                     << "\n  incremental: '" << inc[r] << "'"
                     << "\n  full redraw: '" << full[r] << "'");
                return;
            }
        }
        REQUIRE(f.editor.cursor_offset() <= f.editor.buffer_len());
    }
}

TEST_CASE("editor: a random edit sequence keeps the wire and FRONT in step") {
    for (size_t g = 0; g < sizeof(GEOMETRIES) / sizeof(GEOMETRIES[0]); g++)
        for (uint32_t seed = 1; seed <= 4; seed++)
            sweep(GEOMETRIES[g], seed, 300);
}

}}  // namespace tests::editor_fuzz
