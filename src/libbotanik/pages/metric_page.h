#ifndef PAGES_METRIC_PAGE_H
#define PAGES_METRIC_PAGE_H

/**
 * @file metric_page.h
 * @brief MetricPage — shared base for read-only metric displays (Home/Status).
 *
 * Centralises the bits the metric pages have in common: the terminal binding,
 * the focus/periodic-refresh lifecycle, small label/value drawing helpers, and a
 * per-slot value cache so a refresh only re-emits the bytes that actually changed
 * (the same "diff, don't repaint" approach the status bar uses). This matters on
 * the low-baud serial link: repainting every cell each cycle would saturate the
 * TX buffer and starve the status bar.
 *
 * Values are diffed per field (_field()) and only the bytes that change are
 * re-emitted. A digit run that moved is drawn in reverse video and stays lit
 * until the next refresh, when it is rewritten normal once it stops changing —
 * per-subfield watch(1) -d highlighting over a refresh window. Fields are fixed-
 * width and space-padded, so a shrinking value clears its own tail and each subfield keeps
 * a stable column.
 *
 * Subclasses implement draw_static() (labels/headers, drawn once on focus) and
 * draw_dynamic() (values, rewritten on each refresh via _value()). Row 0 is the
 * terminal status bar; pages own rows 1..N-1.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace Pages {

    /** @brief Base for diff-rendered metric pages (see file header). */
    class MetricPage : public Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning terminal. */
        AnsiTerminal& _terminal;
        /** @brief Column at which values are written (labels start at column 2). */
        uint16_t      _value_col;
        /** @brief True while this page owns the screen. */
        bool          _active          = false;
        /** @brief Last dynamic refresh (uptime ms). */
        uint64_t      _refresh_last_ms = 0;
        /** @brief Interval between full dynamic refreshes (ms). Defaults to the
         *  shared page cadence; a subclass may lower it. */
        uint32_t      _refresh_ms      = Config::PAGE_REFRESH_MS;
        /** @brief Last hot-field refresh (uptime ms). */
        uint64_t      _hot_last_ms     = 0;
        /** @brief Hot-field refresh interval (ms); 0 disables. A subclass pairs this
         *  with a draw_hot() override holding the fields that change every second
         *  (e.g. a clock). Each cycle a hot field is repainted at most once — by this
         *  per-second tick when the TX buffer has drained, or by the global refresh,
         *  never both (see tick()). All other ("cold") fields live in draw_dynamic()
         *  and only move on the global refresh. */
        uint32_t      _hot_ms          = 0;

        /** @brief Number of diff-cache slots and the per-slot value width.
         *  Single-column pages key a slot by its row; column layouts (see _cell())
         *  pass an explicit slot so two value cells can share one row. CACHE_COLS
         *  is 32 so a per-column byte mask fits in one uint32_t. */
        static constexpr uint16_t CACHE_SLOTS = 32;
        static constexpr uint16_t CACHE_COLS  = 32;
        /** @brief Last value written per slot, for change detection. */
        char          _vcache[CACHE_SLOTS][CACHE_COLS];
        /** @brief Columns currently drawn reverse-video per slot (bit i = column i
         *  within the field), so the next refresh's diff knows the prior highlight
         *  state and rewrites a subfield normal once it stops changing. */
        uint32_t      _vhi[CACHE_SLOTS]   = {};
        /** @brief Slots awaiting a first paint after a clear (bit i = slot i). A
         *  fresh slot is drawn unhighlighted (nothing has "changed" yet); the bit
         *  clears once the slot has been painted at least once. */
        uint32_t      _vfresh = ~0u;

        /** @} */

        /** @name Drawing helpers
         *  @{ */

        /** @brief Forget every cached value so the next draw_dynamic() repaints in
         *  full (used on focus, when the screen has just been cleared). The repaint
         *  is unhighlighted: marking all slots fresh suppresses the change reverse-
         *  video on the first paint after a clear. */
        void _invalidate() {
            memset(_vcache, 0, sizeof(_vcache));
            memset(_vhi,    0, sizeof(_vhi));
            _vfresh = ~0u;
        }

        /** @brief Draw a label at column 2 and clear the rest of the row. Labels are
         *  static (drawn once on focus), so they are not cached. */
        void _label(uint16_t row, const char* s) {
            if (row >= _terminal.rows()) return;
            _terminal.move_to({row, 2});
            _terminal.write_chars(s, (uint16_t)strlen(s));
            _terminal.clear_to_eol();
        }

        /** @brief Write a value at _value_col (single-column pages), keyed by row.
         *  The field is fixed-width — from _value_col to the end of the line — so a
         *  shrinking value overwrites its own tail with spaces and no separate
         *  clear-to-eol is needed. Change highlighting is handled by _field(); pass
         *  @p highlight = false for fields that change every refresh (a clock), where
         *  the highlight would only churn a redundant clear-write each tick. */
        void _value(uint16_t row, const char* s, bool highlight = true) {
            if (row >= _terminal.rows()) return;
            uint16_t width = (_value_col < _terminal.cols())
                                 ? (uint16_t)(_terminal.cols() - _value_col) : 0;
            _field(row, row, _value_col, s, width, highlight);
        }

        /** @brief Draw a dash-ruled group header at (@p row, @p col), rendered as
         *  "==== TITLE ============" padded with '=' to @p width. Static (drawn once
         *  on focus, after the screen is cleared); not cached. */
        void _header(uint16_t row, uint16_t col, const char* title, uint16_t width) {
            if (row >= _terminal.rows()) return;
            char buf[96];
            if (width > (uint16_t)(sizeof(buf) - 1)) width = (uint16_t)(sizeof(buf) - 1);
            uint16_t n = 0;
            for (int i = 0; i < 4 && n < width; ++i) buf[n++] = '=';
            if (n < width) buf[n++] = ' ';
            for (const char* p = title; *p && n < width; ++p) buf[n++] = *p;
            if (n < width) buf[n++] = ' ';
            while (n < width) buf[n++] = '=';
            if (_terminal.tx_available() < (uint16_t)(width + 12)) return;
            _terminal.move_to({row, col});
            _terminal.write_chars(buf, n);
        }

        /** @brief Draw a static label at (@p row, @p col) without clearing to the
         *  end of the line, so it never disturbs a second column on the same row.
         *  Drawn once on focus (after a clear); not cached. */
        void _label_at(uint16_t row, uint16_t col, const char* s) {
            if (row >= _terminal.rows()) return;
            uint16_t len = (uint16_t)strlen(s);
            if (_terminal.tx_available() < (uint16_t)(len + 12)) return;
            _terminal.move_to({row, col});
            _terminal.write_chars(s, len);
        }

        /** @brief Diff-rendered value cell for column layouts: write @p s at
         *  (@p row, @p col), padded (or truncated) to exactly @p width chars. The
         *  fixed width means a shrinking value overwrites its own old tail without a
         *  clear-to-eol, so an adjacent column is never touched. Cached by @p slot
         *  (so two cells can share a row). See _field() for the diff/highlight. */
        void _cell(uint16_t slot, uint16_t row, uint16_t col, const char* s, uint16_t width,
                   bool highlight = true) {
            _field(slot, row, col, s, width, highlight);
        }

        /** @brief Render @p s as a fixed, @p width-column value field at (@p row,
         *  @p col), cached under @p slot, with watch(1) -d change highlighting.
         *
         *  The value is space-padded to @p width (longer values are truncated), so
         *  a shrinking value overwrites its own tail and an adjacent column is never
         *  touched. The field is diffed against the previous draw and only the bytes
         *  that actually change are re-emitted — an unchanged field writes nothing,
         *  so a refresh never repaints the static padding.
         *
         *  Highlighting is per subfield: each maximal run of digits is one subfield,
         *  and a run is drawn reverse-video when any digit in it changed (so the day,
         *  hour, minute and second of a clock, or the two voltages in "X V (Y V)",
         *  light up independently). Fixed-width formatting keeps each subfield at a
         *  stable column, so the diff stays aligned. A highlight persists until the
         *  next refresh, where the subfield is rewritten normal once it stops
         *  changing; the first paint after a clear is never lit.
         *
         *  All-or-nothing, like the other writers: when the update can't fit in the
         *  TX buffer nothing is emitted and the cache is left untouched, so the draw
         *  retries on the next refresh. */
        void _field(uint16_t slot, uint16_t row, uint16_t col, const char* s, uint16_t width,
                    bool highlight = true) {
            if (row >= _terminal.rows() || width == 0) return;
            if (width > CACHE_COLS - 1) width = CACHE_COLS - 1;

            // Render the value into the fixed-width, space-padded field.
            char     cf[CACHE_COLS];
            uint16_t vlen = 0;
            for (const char* p = s; *p && vlen < width; ++p) cf[vlen++] = *p;
            for (uint16_t i = vlen; i < width; ++i) cf[i] = ' ';
            cf[width] = '\0';

            const bool     cached = (slot < CACHE_SLOTS);
            const bool     fresh  = !cached || (_vfresh & (1u << slot)) != 0;
            const char*    prev   = cached ? _vcache[slot] : "";
            const uint32_t oldhi  = cached ? _vhi[slot]    : 0u;

            // Per-column change mask, then promote it to whole-subfield highlights:
            // a digit run lights up if any of its digits moved.
            uint32_t chg = 0, newhi = 0;
            if (fresh) {
                for (uint16_t i = 0; i < width; ++i)
                    if (cf[i] != ' ') chg |= (1u << i);   // screen is blank: emit only glyphs, no highlight
            } else {
                for (uint16_t i = 0; i < width; ++i)
                    if (cf[i] != prev[i]) chg |= (1u << i);
                // Highlighting is opt-out: a field that changes every refresh (a clock)
                // leaves it off, so a settled subfield is never re-emitted just to
                // clear an inversion — only genuinely changed glyphs are written.
                if (highlight) {
                    for (uint16_t i = 0; i < width; ) {
                        if (cf[i] < '0' || cf[i] > '9') { ++i; continue; }
                        uint16_t a = i;
                        while (i < width && cf[i] >= '0' && cf[i] <= '9') ++i;
                        uint32_t run = (((i - a) >= 32) ? ~0u : ((1u << (i - a)) - 1u)) << a;
                        if (chg & run) newhi |= run;                // any digit changed -> light the run
                    }
                }
            }

            // Emit a byte when its glyph changed or its highlight state changed.
            uint32_t emit = chg | (newhi ^ oldhi);
            if (emit == 0) return;
            if (!_paint(row, col, cf, width, emit, newhi)) return;  // TX full: retry, cache untouched

            if (cached) {
                memcpy(_vcache[slot], cf, width);
                _vcache[slot][width] = '\0';
                _vhi[slot]   = newhi;
                _vfresh     &= ~(1u << slot);
            }
        }

        /** @brief Emit the columns set in @p emit from field @p cf at (@p row,
         *  @p col), drawing the columns set in @p hi reverse-video. Dirty columns
         *  are coalesced into runs (short clean gaps are overwritten in place rather
         *  than paid for with a cursor move). All-or-nothing: returns false without
         *  emitting anything when the whole update can't fit in the TX buffer. */
        bool _paint(uint16_t row, uint16_t col, const char* cf, uint16_t width,
                    uint32_t emit, uint32_t hi) {
            if (emit == 0) return true;

            // Bridge clean gaps up to MERGE wide: a ~7-byte cursor move costs more
            // than overwriting a couple of unchanged characters.
            const uint16_t MERGE = 4;
            uint32_t render = emit;
            int last = -1;
            for (uint16_t i = 0; i < width; ++i) {
                if (!((emit >> i) & 1u)) continue;
                if (last >= 0 && (int)i - last - 1 <= (int)MERGE)
                    for (int g = last + 1; g < (int)i; ++g) render |= (1u << g);
                last = (int)i;
            }

            // Budget the whole update up front: one move + a couple of SGR toggles
            // per run, plus the emitted bytes.
            uint16_t runs = 0, count = 0;
            for (uint16_t i = 0; i < width; ++i) {
                if (!((render >> i) & 1u)) continue;
                count++;
                if (i == 0 || !((render >> (i - 1)) & 1u)) runs++;
            }
            if (_terminal.tx_available() < (uint16_t)(runs * 12 + count * 5 + 8)) return false;

            uint16_t i = 0;
            while (i < width) {
                if (!((render >> i) & 1u)) { i++; continue; }
                uint16_t a = i;
                while (i < width && ((render >> i) & 1u)) i++;
                uint16_t b = i;                              // run spans [a, b)
                _terminal.move_to({row, (uint16_t)(col + a)});
                uint16_t seg = a;
                bool seg_inv = ((hi >> a) & 1u) != 0;
                for (uint16_t k = a; k < b; ++k) {
                    bool ki = ((hi >> k) & 1u) != 0;
                    if (ki != seg_inv) {
                        _terminal.set_inverted(seg_inv);
                        _terminal.write_chars(&cf[seg], (uint16_t)(k - seg));
                        seg = k; seg_inv = ki;
                    }
                }
                _terminal.set_inverted(seg_inv);
                _terminal.write_chars(&cf[seg], (uint16_t)(b - seg));
            }
            _terminal.set_inverted(false);                   // leave the terminal normal
            return true;
        }

        /** @brief Map a PLC link status to a short display string. */
        static const char* plc_str(PlcIO::PlcStatus s) {
            switch (s) {
                case PlcIO::PLC_RUNNING: return "RUNNING";
                case PlcIO::PLC_TIMEOUT: return "TIMEOUT";
                case PlcIO::PLC_FAULT:   return "FAULT";
                default:                 return "UNKNOWN";
            }
        }

        /** @brief Format a millisecond uptime as "DDDDd HH:MM:SS" into @p buf.
         *  Every field is fixed-width (days padded to 4 digits) so each clock
         *  subfield keeps a stable column for per-subfield highlighting. */
        static void fmt_uptime(char* buf, size_t cap, uint64_t ms) {
            uint64_t s = ms / 1000;
            unsigned d   = (unsigned)(s / 86400); s %= 86400;
            unsigned h   = (unsigned)(s / 3600);  s %= 3600;
            unsigned m   = (unsigned)(s / 60);
            unsigned sec = (unsigned)(s % 60);
            snprintf(buf, cap, "%04ud %02u:%02u:%02u", d, h, m, sec);
        }

        /** @} */

        /** @name Subclass hooks
         *  @{ */

        /** @brief Draw labels/headers that only change on (re)entry/resize. */
        virtual void draw_static() = 0;
        /** @brief Rewrite the cold values — everything refreshed on the global period
         *  only (via _value()). Must NOT touch hot fields, or they would be painted
         *  twice per cycle and the change highlight would flicker. */
        virtual void draw_dynamic() = 0;
        /** @brief Rewrite the hot values — the per-second subset (e.g. a clock's
         *  seconds). Default none; see _hot_ms. Kept disjoint from draw_dynamic(). */
        virtual void draw_hot() {}

        /** @} */

        /** @brief Bind to a terminal; @p value_col is where values are written. */
        MetricPage(AnsiTerminal& terminal, uint16_t value_col)
            : _terminal(terminal), _value_col(value_col) { _invalidate(); }

    public:
        /** @name Page interface
         *  @{ */

        void handle_text(char) override {}

        /** @brief ENTER forces an immediate value refresh; other keys are ignored.
         *  (Periodic refresh is otherwise throttled to _refresh_ms.) */
        void handle_command(Command cmd, uint16_t = 1) override {
            if (cmd == Command::ENTER) {
                _refresh_last_ms = HAL::uptime_ms();
                draw_dynamic();
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _invalidate();   // screen was cleared on switch: repaint everything
            draw_static();
            // Hot fields before cold: a hot tick gates on an empty TX buffer, so it
            // must get first claim on it before the cold refresh fills it up.
            draw_hot();       // hot fields, so the clock isn't blank until the first tick
            draw_dynamic();   // cold fields
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            uint64_t now = HAL::uptime_ms();

            const bool refresh_due = (now - _refresh_last_ms >= _refresh_ms);

            // Hot fields (a clock's seconds) repaint at most once per cycle. Drawn
            // before the cold refresh so a best-effort tick gets first claim on an
            // empty TX buffer rather than fighting the cold pass for it. Prefer the
            // per-second tick when the TX buffer has drained, so it can't pile up on
            // the low-baud link; otherwise the global refresh below is the guaranteed
            // fallback, so the clock never freezes for longer than one refresh window.
            if (_hot_ms && now - _hot_last_ms >= _hot_ms) {
                if (refresh_due || _terminal.tx_empty()) {
                    _hot_last_ms = now;
                    draw_hot();
                }
                // else: TX congested and no refresh due — hold and retry next tick.
            }

            if (refresh_due) {
                _refresh_last_ms = now;
                draw_dynamic();   // cold fields
            }
        }

        /** @} */
    };

}}

#endif // PAGES_METRIC_PAGE_H
