#ifndef BOTANIK_TEST_VT_MODEL_H
#define BOTANIK_TEST_VT_MODEL_H

/**
 * @file vt_model.h
 * @brief VtScreen - an independent VT102 that interprets what the terminal
 *        emitted, so a test can ask what the operator would be looking at.
 *
 * The back-buffer's own FRONT/BACK agreement proves nothing about the wire:
 * present() writes BACK itself, from the same code that builds the escape. This
 * model shares no line with it. Feed it every byte that left the ring and its
 * grid is the second opinion.
 *
 * It implements only what ansi.cpp can emit (@ref Botanik::Terminal::Ansi), and
 * asserts on anything else rather than ignoring it - an escape nobody models is
 * an escape nobody checks.
 */

#include <cstdint>
#include <cstdlib>
// std::hex, used by the FAIL() messages below. It reached here transitively
// through doctest under one configuration and not another.
#include <ios>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik/config/terminal.h"

namespace tests { namespace botanik_test {

    /** @brief A VT102 screen driven by the bytes a terminal emitted.
     *
     *  @note Erasures refill with the CURRENT SGR (background-colour erase).
     *  Real VT102s vary; the strict reading is the one the emitter already
     *  defends against, so modelling it holds that defence in place. */
    struct VtScreen {
        // An enum, not a static constexpr member: C++11 makes the latter a
        // declaration needing an out-of-line definition once it is odr-used,
        // and every TU including this header emits one.
        enum : uint16_t {
            MAX_COLS = Botanik::Config::TERMINAL_MAX_COLS,
            MAX_ROWS = Botanik::Config::TERMINAL_MAX_ROWS
        };

        uint16_t cols = 80, rows = 24;
        char     glyph[MAX_ROWS][MAX_COLS];
        bool     inv  [MAX_ROWS][MAX_COLS];

        uint16_t cr = 0, cc = 0;
        bool     sgr_inv       = false;
        bool     insert_mode   = false;
        bool     auto_wrap     = true;
        bool     cursor_shown  = true;

        /** @brief Bytes fed since the last reset_counter(), the byte ledger's
         *  measurement. */
        uint32_t bytes = 0;

        VtScreen(uint16_t r = 24, uint16_t c = 80) { reshape(r, c); }

        void reshape(uint16_t r, uint16_t c) {
            // Cast, so the comparison takes the VALUE rather than binding the
            // member to a reference: an ODR-used `static constexpr` needs an
            // out-of-class definition, which a header included by several TUs
            // cannot have before C++17.
            REQUIRE(r <= (uint16_t)MAX_ROWS);
            REQUIRE(c <= (uint16_t)MAX_COLS);
            rows = r; cols = c;
            blank(0, rows);
            cr = cc = 0;
            sgr_inv = insert_mode = false;
            auto_wrap = cursor_shown = true;
        }

        void reset_counter() { bytes = 0; }

        /** @brief Row @p r as text, right-trimmed. */
        std::string row_text(uint16_t r) const {
            std::string s(glyph[r], glyph[r] + cols);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        }

        std::vector<std::string> screen() const {
            std::vector<std::string> out;
            for (uint16_t r = 0; r < rows; r++) out.push_back(row_text(r));
            return out;
        }

        void feed(const std::string& s) {
            bytes += (uint32_t)s.size();
            for (size_t i = 0; i < s.size(); i++) _byte((uint8_t)s[i]);
        }

        /** @name Parser
         *  @{ */
      private:
        enum State : uint8_t { GROUND, ESC_SEEN, CSI_SEEN, PRO2_SEEN };
        State       _state = GROUND;
        std::string _params;
        uint8_t     _pro2_left = 0;

        void _byte(uint8_t b) {
            if (b == 0x18) { _state = GROUND; return; }         // CAN
            switch (_state) {
            case GROUND:
                if      (b == 0x1B)                _state = ESC_SEEN;
                else if (b == '\r')                cc = 0;
                else if (b == '\n')                _line_feed();
                else if (b >= 0x20 && b <= 0x7E)   _put((char)b);
                else FAIL("VtScreen: unmodelled control byte 0x"
                          << std::hex << (unsigned)b);
                return;
            case ESC_SEEN:
                if      (b == '[') { _state = CSI_SEEN; _params.clear(); }
                // Minitel PRO2 handshake: two bytes follow and touch no cell.
                else if (b == ':') { _state = PRO2_SEEN; _pro2_left = 2; }
                else FAIL("VtScreen: unmodelled escape ESC 0x"
                          << std::hex << (unsigned)b);
                return;
            case PRO2_SEEN:
                if (--_pro2_left == 0) _state = GROUND;
                return;
            case CSI_SEEN:
                if (b >= 0x30 && b <= 0x3F) { _params += (char)b; return; }
                if (b >= 0x40 && b <= 0x7E) { _final((char)b); _state = GROUND; return; }
                FAIL("VtScreen: unmodelled CSI byte 0x" << std::hex << (unsigned)b);
                return;
            }
        }

        /** @brief Parameter @p k of the pending CSI, @p def when absent or
         *  empty - which is how ansi.cpp spells a count of 1. */
        uint16_t _param(size_t k, uint16_t def) const {
            size_t start = 0, idx = 0;
            const char* p = _params.c_str();
            if (*p == '?') { start = 1; p++; }
            std::string cur;
            for (size_t i = start; i <= _params.size(); i++) {
                if (i == _params.size() || _params[i] == ';') {
                    if (idx == k) return cur.empty() ? def : (uint16_t)atoi(cur.c_str());
                    idx++; cur.clear();
                } else cur += _params[i];
            }
            return def;
        }
        bool _private() const { return !_params.empty() && _params[0] == '?'; }

        void _final(char f) {
            const uint16_t n = _param(0, 1);
            switch (f) {
            case 'A': cr = (n > cr) ? 0 : (uint16_t)(cr - n); break;
            case 'B': cr = (uint16_t)(cr + n); if (cr >= rows) cr = (uint16_t)(rows - 1); break;
            case 'C': cc = (uint16_t)(cc + n); if (cc >= cols) cc = (uint16_t)(cols - 1); break;
            case 'D': cc = (n > cc) ? 0 : (uint16_t)(cc - n); break;
            case 'H': {
                uint16_t r = _param(0, 1), c = _param(1, 1);
                cr = (uint16_t)(r ? r - 1 : 0); cc = (uint16_t)(c ? c - 1 : 0);
                if (cr >= rows) cr = (uint16_t)(rows - 1);
                if (cc >= cols) cc = (uint16_t)(cols - 1);
                break;
            }
            case 'J': {
                const uint16_t mode = _param(0, 0);
                if      (mode == 2) blank(0, rows);
                else if (mode == 0) { _blank_cells(cr, cc, cols); blank((uint16_t)(cr + 1), rows); }
                else                { blank(0, cr); _blank_cells(cr, 0, (uint16_t)(cc + 1)); }
                break;
            }
            case 'L': _scroll(cr, rows, /*up=*/false, n); cc = 0; break;
            case 'M': _scroll(cr, rows, /*up=*/true,  n); cc = 0; break;
            case 'P': {
                const uint16_t k = (n > (uint16_t)(cols - cc)) ? (uint16_t)(cols - cc) : n;
                for (uint16_t c = cc; (uint16_t)(c + k) < cols; c++) {
                    glyph[cr][c] = glyph[cr][c + k];
                    inv  [cr][c] = inv  [cr][c + k];
                }
                _blank_cells(cr, (uint16_t)(cols - k), cols);
                break;
            }
            case 'K':
                if (_param(0, 0) != 0) FAIL("VtScreen: unmodelled EL mode " << _param(0, 0));
                _blank_cells(cr, cc, cols);
                break;
            case 'm': sgr_inv = (_param(0, 0) == 7); break;
            case 'h':
            case 'l': {
                const bool set = (f == 'h');
                const uint16_t which = _param(0, 0);
                if (_private()) {
                    if      (which == 7)  auto_wrap    = set;
                    else if (which == 25) cursor_shown = set;
                    else FAIL("VtScreen: unmodelled private mode " << which);
                } else if (which == 4) insert_mode = set;
                else FAIL("VtScreen: unmodelled mode " << which);
                break;
            }
            default: FAIL("VtScreen: unmodelled CSI final '" << f << "'");
            }
        }

        void _put(char ch) {
            if (insert_mode) {
                for (uint16_t c = (uint16_t)(cols - 1); c > cc; c--) {
                    glyph[cr][c] = glyph[cr][c - 1];
                    inv  [cr][c] = inv  [cr][c - 1];
                }
            }
            glyph[cr][cc] = ch;
            inv  [cr][cc] = sgr_inv;
            // DECAWM off parks the cursor on the last column: further glyphs
            // overwrite it rather than wrapping.
            if ((uint16_t)(cc + 1) < cols)  cc++;
            else if (auto_wrap)           { cc = 0; _line_feed(); }
        }

        void _line_feed() {
            if ((uint16_t)(cr + 1) < rows) cr++;
            else _scroll(0, rows, /*up=*/true, 1);
        }

        /** @brief Move rows within [@p top, @p bottom) by @p n, blanking what
         *  it frees. IL/DL carry no scroll region here: ansi.cpp emits no
         *  DECSTBM, so they run to the bottom of the screen. */
        void _scroll(uint16_t top, uint16_t bottom, bool up, uint16_t n) {
            if (top >= bottom) return;
            const uint16_t span = (uint16_t)(bottom - top);
            if (n > span) n = span;
            if (up) {
                for (uint16_t r = top; (uint16_t)(r + n) < bottom; r++) _copy_row(r, (uint16_t)(r + n));
                blank((uint16_t)(bottom - n), bottom);
            } else {
                for (uint16_t r = (uint16_t)(bottom - 1); r >= (uint16_t)(top + n); r--)
                    _copy_row(r, (uint16_t)(r - n));
                blank(top, (uint16_t)(top + n));
            }
        }

        void _copy_row(uint16_t dst, uint16_t src) {
            for (uint16_t c = 0; c < cols; c++) {
                glyph[dst][c] = glyph[src][c];
                inv  [dst][c] = inv  [src][c];
            }
        }
        void _blank_cells(uint16_t r, uint16_t from, uint16_t to) {
            for (uint16_t c = from; c < to; c++) { glyph[r][c] = ' '; inv[r][c] = sgr_inv; }
        }

      public:
        void blank(uint16_t from, uint16_t to) {
            for (uint16_t r = from; r < to && r < rows; r++) _blank_cells(r, 0, cols);
        }
        /** @} */
    };

}}  // namespace tests::botanik_test

#endif // BOTANIK_TEST_VT_MODEL_H
