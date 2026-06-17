#ifndef PAGES_METRIC_PAGE_H
#define PAGES_METRIC_PAGE_H

/**
 * @file metric_page.h
 * @brief MetricPage - shared base for read-only metric displays (Home/Status).
 *
 * Centralises what the metric pages share: the terminal binding, the
 * focus/periodic-refresh lifecycle, label/value drawing helpers, and a per-slot
 * value cache so a refresh re-emits only the bytes that changed ("diff, don't
 * repaint"). On the low-baud link, repainting every cell would saturate TX.
 *
 * A changed digit run is drawn reverse-video and stays lit until it settles, then
 * is rewritten normal - watch(1) -d highlighting over a refresh window. Fields are
 * fixed-width and space-padded, so each subfield keeps a stable column.
 *
 * Subclasses implement draw_static() (labels/headers, once on focus) and
 * draw_dynamic() (values, per refresh). Row 0 is the status bar; pages own 1..N-1.
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
        /** @brief Hot-field refresh interval (ms); 0 disables. Paired with a
         *  draw_hot() override for fields that change every second (e.g. a clock).
         *  A hot field repaints at most once per cycle - by this tick or the global
         *  refresh, never both (see tick()). Cold fields live in draw_dynamic(). */
        uint32_t      _hot_ms          = 0;
        /** @brief Fields emitted in the current pass (reset at the start of each pass).
         *  Bounds the per-tick paint burst to the pass's budget (TX_HOT/COLD_FIELDS_PER_TICK)
         *  so a full refresh or repaint spreads over several ticks instead of flooding
         *  the TX ring. */
        uint16_t      _emit_count       = 0;
        /** @brief Set by _paint() when it skips a field (per-tick cap hit or the cold
         *  depth gate closed), so tick()/focus() know the pass did not fully drain. */
        bool          _emit_deferred    = false;
        /** @brief A cold draw_dynamic() pass was left unfinished; resume it on later
         *  ticks (the value cache makes already-drawn fields no-ops) until it drains. */
        bool          _dynamic_pending  = false;
        /** @brief A draw_hot() pass was left unfinished (cap hit or floor closed); resume
         *  it every tick (the value cache makes drawn fields no-ops) until it completes,
         *  so all hot fields get a first paint without waiting a full _hot_ms cadence -
         *  and without waiting for the slower cold pass to finish. */
        bool          _hot_pending       = false;
        /** @brief Alternates the hot/cold first-claim on the shared TX floor each
         *  tick. With a fixed hot-before-cold order, a hot field that changes every
         *  tick takes every drain opening and starves cold under congestion (heavy
         *  time dilation, slow link); flipping who goes first shares the openings. */
        bool          _paint_cold_first  = false;
        /** @name Cooperative static draw (see _static_step())
         *  draw_static() issues the same deterministic sequence of static-element
         *  draws each pass (labels/headers/logo), so an ordinal cursor can pace it:
         *  skip the elements already emitted, draw the next few, and stop when the
         *  per-tick cap or the cold gate closes - resuming on later ticks. The cold
         *  pass is held until the static pass fully completes.
         *  @{ */
        /** @brief Element index within the current draw_static() pass (reset each pass). */
        uint16_t      _static_idx        = 0;
        /** @brief Elements successfully emitted across passes (persists; reset on focus). */
        uint16_t      _static_done       = 0;
        /** @brief This pass stopped early (cap/gate); remaining elements are skipped. */
        bool          _static_blocked    = false;
        /** @brief draw_static() has not finished painting; keep resuming it. */
        bool          _static_pending    = false;
        /** @} */
        /** @name Cooperative cold draw (see _field())
         *  Same ordinal idea as the static pass, but for the periodic value pass:
         *  each cold field is painted at most once per refresh period. Without it a
         *  capped/deferred pass that resumes every tick would re-diff live values and
         *  re-emit the fields that change every tick (cpu, serial counters), starving
         *  the fields after them and never completing.
         *  @{ */
        /** @brief Field index within the current draw_dynamic() pass (reset each pass). */
        uint16_t      _cold_idx          = 0;
        /** @brief Fields handled so far this period (persists across resume ticks;
         *  reset to 0 when a new refresh period starts). */
        uint16_t      _cold_done         = 0;
        /** @brief True while draw_dynamic() runs, so _field() applies the ordinal
         *  (hot fields and other callers paint normally). */
        bool          _cold_active       = false;
        /** @} */

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

        /** @brief Ordinal gate for one static element (a label/header/logo line).
         *  Returns true if the caller should draw it now: skips elements already
         *  emitted in an earlier pass, and stops the pass (deferring the rest) when
         *  the per-tick field cap is hit or the cold depth gate is closed. draw_static()
         *  must issue the same sequence of these each pass for the ordinal to line up
         *  (it is static layout, so it does). */
        bool _static_step() {
            uint16_t k = _static_idx++;
            if (k < _static_done) return false;     // already drawn in an earlier pass
            if (_static_blocked)  return false;     // pass already stopped this tick
            if (_emit_count >= Config::TX_COLD_FIELDS_PER_TICK ||
                !_terminal.tx_below_cold()) {
                _emit_deferred = true;
                _static_blocked = true;
                return false;
            }
            _static_done = (uint16_t)(k + 1);
            _emit_count++;
            return true;
        }

        /** @brief Run one paced draw_static() pass; sets _static_pending if it could
         *  not finish (resumed on later ticks until it drains). */
        void _run_static() {
            _static_idx     = 0;
            _static_blocked = false;
            _emit_count     = 0;        // static draws on the cold budget (TX_COLD_FIELDS_PER_TICK)
            draw_static();
            _static_pending = _static_blocked;
        }

        /** @brief Draw a label at column 2 and clear the rest of the row. Labels are
         *  static (drawn once on focus), so they are not cached. */
        void _label(uint16_t row, const char* s) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;
            _terminal.move_to({row, 2});
            _terminal.write_chars(s, (uint16_t)strlen(s));
            _terminal.clear_to_eol();
        }

        /** @brief Write a value at _value_col (single-column pages), keyed by row.
         *  The field runs from _value_col to end-of-line, so a shrinking value clears
         *  its own tail. Pass @p highlight = false for fields that change every
         *  refresh (a clock), where the highlight would just churn. Pass @p invert to
         *  light the whole value as one reverse-video block on change, instead of the
         *  default per-digit-run highlight (for non-numeric values like a tray label). */
        void _value(uint16_t row, const char* s, bool highlight = true, bool invert = false) {
            if (row >= _terminal.rows()) return;
            uint16_t width = (_value_col < _terminal.cols())
                                 ? (uint16_t)(_terminal.cols() - _value_col) : 0;
            _field(row, row, _value_col, s, width, highlight, invert);
        }

        /** @brief Draw a dash-ruled group header at (@p row, @p col), rendered as
         *  "==== TITLE ============" padded with '=' to @p width. Static (drawn once
         *  on focus, after the screen is cleared); not cached. */
        void _header(uint16_t row, uint16_t col, const char* title, uint16_t width) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;
            char buf[96];
            if (width > (uint16_t)(sizeof(buf) - 1)) width = (uint16_t)(sizeof(buf) - 1);
            uint16_t n = 0;
            for (int i = 0; i < 4 && n < width; ++i) buf[n++] = '=';
            if (n < width) buf[n++] = ' ';
            for (const char* p = title; *p && n < width; ++p) buf[n++] = *p;
            if (n < width) buf[n++] = ' ';
            while (n < width) buf[n++] = '=';
            _terminal.move_to({row, col});
            _terminal.write_chars(buf, n);
        }

        /** @brief Draw a static label at (@p row, @p col) without clearing to the
         *  end of the line, so it never disturbs a second column on the same row.
         *  Drawn once on focus (after a clear); not cached. */
        void _label_at(uint16_t row, uint16_t col, const char* s) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;
            _terminal.move_to({row, col});
            _terminal.write_chars(s, (uint16_t)strlen(s));
        }

        /** @brief Diff-rendered value cell for column layouts: write @p s at
         *  (@p row, @p col), padded/truncated to @p width chars (so it never touches
         *  an adjacent column). Cached by @p slot so two cells can share a row. */
        void _cell(uint16_t slot, uint16_t row, uint16_t col, const char* s, uint16_t width,
                   bool highlight = true) {
            _field(slot, row, col, s, width, highlight);
        }

        /** @brief Render @p s as a fixed @p width-column field at (@p row, @p col),
         *  cached under @p slot, with watch(1) -d change highlighting.
         *
         *  Space-padded to @p width (truncated if longer) and diffed against the
         *  previous draw, so only changed bytes are re-emitted. Highlighting is per
         *  subfield: each digit run lights reverse-video when any of its digits
         *  changed, persisting until it settles (the first paint after a clear is
         *  never lit). Pass @p invert to instead light the whole value as a single
         *  block whenever any byte changes. All-or-nothing: on a TX-full skip nothing
         *  is emitted and the cache is left untouched, so the draw retries next refresh. */
        void _field(uint16_t slot, uint16_t row, uint16_t col, const char* s, uint16_t width,
                    bool highlight = true, bool invert = false) {
            if (row >= _terminal.rows() || width == 0) return;
            // Cold pacing ordinal: handle each cold field at most once per refresh
            // period. The cursor advances on every visited field, so a deferred pass
            // resumes past the fields already done - a value that changes every tick
            // can't re-emit on each resume and starve the fields after it.
            const bool cold = _cold_active;
            uint16_t k = 0;
            if (cold) {
                k = _cold_idx++;
                if (k < _cold_done || _emit_deferred) return;
            }
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
                // clear an inversion - only genuinely changed glyphs are written.
                if (highlight && invert) {
                    // Whole-value invert: any change lights the printed glyphs (cols
                    // [0, vlen), not the trailing padding) as a single block.
                    if (chg) newhi = (vlen >= 32) ? ~0u : ((1u << vlen) - 1u);
                } else if (highlight) {
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
            if (emit == 0) { if (cold) _cold_done = (uint16_t)(k + 1); return; }  // unchanged: done
            if (!_paint(row, col, cf, width, emit, newhi)) return;  // capped/TX full: _emit_deferred set, resume

            if (cached) {
                memcpy(_vcache[slot], cf, width);
                _vcache[slot][width] = '\0';
                _vhi[slot]   = newhi;
                _vfresh     &= ~(1u << slot);
            }
            if (cold) _cold_done = (uint16_t)(k + 1);   // emitted: done this period
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

            // Cooperative pacing: emit only while the ring is shallow (the shared depth
            // floor) and we are under the per-tick field cap; otherwise defer (cache
            // untouched, so a later tick repaints this field). The cap is per-pass - the
            // cold pass and the hot pass each reset _emit_count and use their own budget,
            // so they progress in parallel at independent rates. The shallow floor leaves
            // ample room for one field, so no separate fit check is needed.
            const uint16_t cap = _cold_active ? Config::TX_COLD_FIELDS_PER_TICK
                                              : Config::TX_HOT_FIELDS_PER_TICK;
            if (_emit_count >= cap || !_terminal.tx_below_cold()) {
                _emit_deferred = true;
                return false;
            }

            uint16_t i = 0;
            while (i < width) {
                if (!((render >> i) & 1u)) { i++; continue; }
                uint16_t a = i;
                while (i < width && ((render >> i) & 1u)) i++;
                uint16_t b = i;                              // run spans [a, b)
                // Shared run-emit (cursor move + lazy SGR segmenting + write).
                _terminal.emit_field_run(row, col, cf, a, b, hi);
            }
            _terminal.set_inverted(false);                   // leave the terminal normal
            _emit_count++;                                    // one field spent this tick
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
        /** @brief Rewrite the cold values - everything refreshed on the global period
         *  only (via _value()). Must NOT touch hot fields, or they would be painted
         *  twice per cycle and the change highlight would flicker. */
        virtual void draw_dynamic() = 0;
        /** @brief Rewrite the hot values - the per-second subset (e.g. a clock's
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

        /** @brief Ctrl+C exits (same path as the shell "exit" command); ENTER forces an
         *  immediate value refresh; other keys are ignored (periodic refresh is otherwise
         *  throttled to _refresh_ms). ENTER routes through the paced cold pass on the next
         *  tick rather than painting inline, so it obeys the same field cap / depth gate /
         *  per-period ordinal. */
        void handle_command(Command cmd, uint16_t = 1) override {
            if (cmd == Command::CTRL_C) HAL::shutdown();  // does not return
            if (cmd == Command::ENTER) {
                _refresh_last_ms = HAL::uptime_ms();
                _cold_done       = 0;       // revisit every field
                _dynamic_pending = true;    // tick() runs the paced refresh
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _invalidate();   // screen was cleared on switch: repaint everything
            _static_done = 0;        // repaint all static elements from scratch
            _cold_done   = 0;        // and the cold values from scratch
            // Ensure the cold pass runs as soon as static finishes, even when static is
            // paced over several ticks - tick() would otherwise wait a full _refresh_ms
            // before the first value paint.
            _dynamic_pending = true;
            _hot_last_ms     = 0;   // fire hot the moment static drains (here or a later tick)
            // Static (labels/headers/logo) is paced first. The page hot AND cold passes
            // both wait for static to finish - their values need their labels in place -
            // so during the static phase only the (self-contained) status bar interleaves
            // with it. Once static drains, tick() paints hot then cold.
            _run_static();
            if (!_static_pending) {
                _emit_count = 0;
                _emit_deferred = false;
                draw_hot();       // hot fields (paced)
                _hot_pending = _emit_deferred;
                _hot_last_ms = HAL::uptime_ms();
                _cold_idx = 0;
                _cold_active = true;
                _emit_count = 0;
                _emit_deferred = false;
                draw_dynamic();   // cold fields (paced)
                _cold_active = false;
                _dynamic_pending = _emit_deferred;
            }
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            uint64_t now = HAL::uptime_ms();

            // The hot and cold passes each get their own per-tick field budget (each
            // resets _emit_count and caps on its own constant), so they make progress in
            // parallel at independent rates rather than competing for one. Both wait for
            // static to drain - their values need their labels first - so during the
            // static phase only the status bar interleaves with it.

            // Static: paced on the cold budget, runs every tick until it completes.
            if (_static_pending) _run_static();

            const bool refresh_due = (now - _refresh_last_ms >= _refresh_ms);

            // Hot fields (clock/uptime): once static is drawn, repaint on the per-second
            // cadence, or keep resuming a pass that could not finish (so every hot field
            // gets a first paint without waiting for the slower cold pass). Each field is
            // paced under the shared depth floor by _paint(); the cadence clock advances
            // only on a fresh due tick, not on a pending-only resume.
            auto run_hot = [&]() {
                if (_static_pending || !_hot_ms) return;
                const bool hot_due = (now - _hot_last_ms >= _hot_ms);
                if (!(hot_due || _hot_pending)) return;
                if (hot_due) _hot_last_ms = now;
                _emit_count = 0;   // hot's own per-tick budget
                _emit_deferred = false;
                draw_hot();
                _hot_pending = _emit_deferred;
            };

            // Cold fields: only once static is fully drawn (a value needs its label
            // first). On the refresh period or continued from a deferred pass; if it
            // could not finish, resume next tick without disturbing the steady cadence
            // (_refresh_last_ms is only advanced on a fresh refresh). A fresh period
            // resets the ordinal (_cold_done = 0) so every field is revisited (the value
            // cache still skips the ones that did not change).
            auto run_cold = [&]() {
                if (_static_pending || !(refresh_due || _dynamic_pending)) return;
                if (refresh_due) { _refresh_last_ms = now; _cold_done = 0; }
                _cold_idx = 0;
                _cold_active = true;
                _emit_count = 0;       // cold's own per-tick budget
                _emit_deferred = false;
                draw_dynamic();   // cold fields (paced)
                _cold_active = false;
                _dynamic_pending = _emit_deferred;
            };

            // Alternate which pass gets first claim on the shared TX floor: a fixed
            // hot-first order lets a hot field that changes every tick take every drain
            // opening and starve cold under congestion. Flipping each tick splits the
            // openings fairly. Order is irrelevant when the link keeps up (the floor
            // never binds, so both passes fully drain regardless).
            if (_paint_cold_first) { run_cold(); run_hot(); }
            else                   { run_hot();  run_cold(); }
            _paint_cold_first = !_paint_cold_first;
        }

        /** @} */
    };

}}

#endif // PAGES_METRIC_PAGE_H
