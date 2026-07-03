// REVIEW_35: TODO
#ifndef PAGES_METRIC_PAGE_H
#define PAGES_METRIC_PAGE_H

/**
 * @file metric_page.h
 * @brief MetricPage - shared base for read-only metric displays (Home/Status).
 *
 * Shares the terminal binding, focus/periodic-refresh lifecycle, drawing
 * helpers, and a per-slot value cache so a refresh re-emits only changed bytes
 * ("diff, don't repaint") - on the low-baud link, repainting every cell would
 * saturate TX.
 *
 * A changed digit run is drawn reverse-video and stays lit until it settles,
 * then rewritten normal - watch(1) -d highlighting. Fields are fixed-width
 * space-padded, so each subfield keeps a stable column.
 *
 * Subclasses implement draw_static() (labels/headers, once on focus) and
 * draw_dynamic() (values, per refresh).
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/utils/assert.h"       // BOTANIK_ASSERT (DEBUG slot guard)
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/field.h"
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
        /** @brief Column at which values are written (labels start at column
         *  2). */
        uint16_t      _value_col;
        /** @brief True while this page owns the screen. */
        bool          _active          = false;
        /** @brief Last dynamic refresh (uptime ms). */
        uint64_t      _refresh_last_ms = 0;
        /** @brief Interval between full dynamic refreshes (ms); a subclass may
         *  lower it. */
        uint32_t      _refresh_ms      = Config::PAGE_REFRESH_MS;
        /** @brief Last hot-field refresh (uptime ms). */
        uint64_t      _hot_last_ms     = 0;
        /** @brief Hot-field refresh interval (ms); 0 disables. Paired with
         *  draw_hot() for fields that change every second (e.g. a clock); a hot
         *  field repaints at most once per cycle (see tick()). Cold fields live
         *  in draw_dynamic(). */
        uint32_t      _hot_ms          = 0;
        /** @brief Fields emitted this pass (reset per pass). Bounds the
         *  per-tick burst to TX_HOT/COLD_FIELDS_PER_TICK so a refresh spreads
         *  over ticks, not floods TX. */
        uint16_t      _emit_count       = 0;
        /** @brief Set by _paint() when it skips a field (cap hit or cold gate
         *  closed), so the pass knows it did not fully drain. */
        bool          _emit_deferred    = false;
        /** @brief A cold draw_dynamic() pass was unfinished; resume it on later
         *  ticks. */
        bool          _dynamic_pending  = false;
        /** @brief A draw_hot() pass was unfinished; resume every tick (the hot
         *  ordinal skips drawn fields) so all hot fields get a first paint
         *  without waiting a full _hot_ms cadence or the slower cold pass. */
        bool          _hot_pending       = false;
        /** @brief Alternates hot/cold first-claim on the shared TX floor each
         *  tick: a fixed hot-first order lets a per-tick hot field take every
         *  drain opening and starve cold under congestion; flipping shares the
         *  openings. */
        bool          _paint_cold_first  = false;
        /** @name Cooperative static draw (see _static_step())
         *  draw_static() issues the same deterministic element sequence each
         *  pass, so an ordinal cursor can pace it: skip emitted elements, draw
         *  a few, stop at the cap/gate, resume later. The cold pass waits until
         *  static completes.
         *  @{ */
        /** @brief Element index within the current draw_static() pass (reset
         *  each pass). */
        uint16_t      _static_idx        = 0;
        /** @brief Elements emitted across passes (persists; reset on focus). */
        uint16_t      _static_done       = 0;
        /** @brief This pass stopped early (cap/gate). */
        bool          _static_blocked    = false;
        /** @brief draw_static() unfinished; keep resuming it. */
        bool          _static_pending    = false;
        /** @} */
        /** @name Cooperative cold draw (see _field())
         *  The static ordinal idea applied to the periodic value pass: each
         *  cold field paints at most once per refresh period. Without it a
         *  deferred pass resuming every tick would re-emit the fields that
         *  change every tick (cpu, counters), starving the rest and never
         *  completing.
         *  @{ */
        /** @brief Field index within the current draw_dynamic() pass (reset
         *  each pass). */
        uint16_t      _cold_idx          = 0;
        /** @brief Fields handled this period (persists across resumes; reset
         *  per period). */
        uint16_t      _cold_done         = 0;
        /** @brief True while draw_dynamic() runs, so _field() applies the cold
         *  ordinal. */
        bool          _cold_active       = false;
        /** @} */
        /** @name Cooperative hot draw (see _field())
         *  The cold ordinal applied to the per-second hot pass. Hot fields
         *  change every tick, so a resuming pass would re-emit them many times
         *  per _hot_ms; the ordinal caps each to one paint per period
         *  (_hot_done resets only on a fresh due tick).
         *  @{ */
        /** @brief Field index within the current draw_hot() pass (reset each
         *  pass). */
        uint16_t      _hot_idx           = 0;
        /** @brief Hot fields handled this period (persists across resumes;
         *  reset per period). */
        uint16_t      _hot_done          = 0;
        /** @brief True while draw_hot() runs, so _field() applies the hot
         *  ordinal. */
        bool          _hot_active        = false;
        /** @} */

        /** @brief Diff-cache slots and per-slot value width. Single-column
         *  pages key a slot by row; column layouts (_cell()) pass an explicit
         *  slot so two cells share a row. CACHE_COLS is 32 so a per-column mask
         *  fits a uint32. */
        static constexpr uint16_t CACHE_SLOTS = 32;
        static constexpr uint16_t CACHE_COLS  = 32;
        static_assert(CACHE_COLS  <= 32,
                      "CACHE_COLS must fit a uint32 per-column mask");
        static_assert(CACHE_SLOTS <= 32,
                      "CACHE_SLOTS must fit a uint32 per-slot mask");
        /** @brief Last value written per slot, for change detection. */
        char          _vcache[CACHE_SLOTS][CACHE_COLS];
        /** @brief Columns drawn reverse-video per slot (bit i = column i), so
         *  the next diff knows the prior highlight state and clears a subfield
         *  once it settles. */
        uint32_t      _vhi[CACHE_SLOTS]   = {};
        /** @brief Slots awaiting a first paint after a clear (bit i = slot i):
         *  drawn unhighlighted (nothing has "changed" yet); the bit clears once
         *  painted. */
        uint32_t      _vfresh = ~0u;

        /** @} */

        /** @name Drawing helpers
         *  @{ */

        /** @brief Forget every cached value so the next pass repaints in full
         *  (on focus, screen just cleared). Marking all slots fresh keeps the
         *  repaint unhighlighted. */
        void _invalidate() {
            memset(_vcache, 0, sizeof(_vcache));
            memset(_vhi,    0, sizeof(_vhi));
            _vfresh = ~0u;
        }

        /** @brief Ordinal gate for one static element. Returns true if it
         *  should draw now: skips elements emitted in an earlier pass, stops
         *  the pass when the per-tick cap or the cold gate closes.
         *  draw_static() must issue the same sequence each pass. */
        bool _static_step() {
            uint16_t k = _static_idx++;
            if (k < _static_done) return false;     // drawn in an earlier pass
            if (_static_blocked)  return false;     // pass already stopped
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

        /** @brief Run one paced draw_static() pass; sets _static_pending if it
         *  could not finish (resumed on later ticks until it drains). */
        void _run_static() {
            _static_idx     = 0;
            _static_blocked = false;
            _emit_count     = 0;        // static draws on the cold budget
            draw_static();
            _static_pending = _static_blocked;
        }

        /** @brief Draw a label at column 2 and clear to EOL. Static, not
         *  cached. */
        void _label(uint16_t row, const char* s) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;
            _terminal.move_to({row, 2});
            _terminal.write_chars(s, (uint16_t)strlen(s));
            _terminal.clear_to_eol();
        }

        /** @brief Write a value at _value_col, keyed by row. The field runs to
         *  EOL, so a shrinking value clears its own tail. @p highlight = false
         *  for fields that change every refresh (a clock). @p invert lights the
         *  whole value as one block on change (for non-numeric values like a
         *  status label) instead of per-digit-run. */
        void _value(uint16_t row, const char* s, bool highlight = true,
                    bool invert = false) {
            if (row >= _terminal.rows()) return;
            uint16_t width = (_value_col < _terminal.cols())
                                 ? (uint16_t)(_terminal.cols() - _value_col) : 0;
            _field(row, row, _value_col, s, width, highlight, invert);
        }

        /** @brief Draw a dash-ruled group header "==== TITLE ====" padded to
         *  @p width. Static, not cached. */
        void _header(uint16_t row, uint16_t col, const char* title, uint16_t width) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;
            char buf[96];
            if (width > (uint16_t)(sizeof(buf) - 1))
                width = (uint16_t)(sizeof(buf) - 1);
            uint16_t n = 0;
            for (int i = 0; i < 4 && n < width; ++i) buf[n++] = '=';
            if (n < width) buf[n++] = ' ';
            for (const char* p = title; *p && n < width; ++p) buf[n++] = *p;
            if (n < width) buf[n++] = ' ';
            while (n < width) buf[n++] = '=';
            _terminal.move_to({row, col});
            _terminal.write_chars(buf, n);
        }

        /** @brief Draw a static label at (@p row, @p col) without
         *  clear-to-EOL, so it never disturbs a second column on the same row.
         *  Not cached. */
        void _label_at(uint16_t row, uint16_t col, const char* s) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;
            _terminal.move_to({row, col});
            _terminal.write_chars(s, (uint16_t)strlen(s));
        }

        /** @brief Diff-rendered value cell for column layouts: write @p s
         *  padded/truncated to @p width (never touching an adjacent column).
         *  Cached by @p slot. */
        void _cell(uint16_t slot, uint16_t row, uint16_t col, const char* s,
                   uint16_t width, bool highlight = true) {
            _field(slot, row, col, s, width, highlight);
        }

        /** @brief Render @p s as a fixed @p width-column field, cached under
         *  @p slot, with watch(1) -d change highlighting. Space-padded, diffed
         *  against the previous draw so only changed bytes re-emit.
         *  Highlighting is per subfield: each digit run lights when any of its
         *  digits changed, persisting until it settles (first paint after a
         *  clear is never lit). @p invert instead lights the whole value as a
         *  block on any change.
         *  All-or-nothing: on a TX-full skip nothing emits and the cache is
         *  untouched, so the draw retries next refresh. */
        void _field(uint16_t slot, uint16_t row, uint16_t col, const char* s,
                    uint16_t width, bool highlight = true, bool invert = false) {
            if (row >= _terminal.rows() || width == 0) return;
            // Pacing ordinal (cold or hot pass): handle each field at most once
            // per period. The cursor advances on every visited field, so a
            // deferred pass resumes past the done fields - a value that changes
            // every tick can't re-emit on resume and starve the fields after
            // it. The two passes never run at once.
            uint16_t* idx  = _cold_active ? &_cold_idx
                                          : (_hot_active ? &_hot_idx  : nullptr);
            uint16_t* done = _cold_active ? &_cold_done
                                          : (_hot_active ? &_hot_done : nullptr);
            const bool paced = (idx != nullptr);
            uint16_t k = 0;
            if (paced) {
                k = (*idx)++;
                if (k < *done || _emit_deferred) return;
            }
            // A slot past the cache renders uncached (repaints every tick, no
            // highlight): assert so a site catches the overflow at bring-up,
            // not as a silent CPU/TX leak. Hard cell limit is CACHE_COLS-1
            // (31); a wider cell is silently clamped.
            BOTANIK_ASSERT(slot < CACHE_SLOTS);
            if (width > CACHE_COLS - 1) width = CACHE_COLS - 1;

            // Render into the fixed-width, space-padded field.
            char     cf[CACHE_COLS];
            uint16_t vlen = Terminal::Field::render(cf, s, width);

            const bool     cached = (slot < CACHE_SLOTS);
            const bool     fresh  = !cached || (_vfresh & (1u << slot)) != 0;
            const char*    prev   = cached ? _vcache[slot] : "";
            const uint32_t oldhi  = cached ? _vhi[slot]    : 0u;

            // Per-column change mask, promoted to whole-subfield highlights: a
            // digit run lights if any of its digits moved.
            uint32_t chg = Terminal::Field::changed(cf, prev, width, fresh), newhi = 0;
            if (!fresh) {
                // Highlight opt-out keeps a settled subfield from re-emitting
                // just to clear an inversion - only changed glyphs are written.
                if (highlight && invert) {
                    // Whole-value invert: light the printed glyphs ([0, vlen),
                    // not padding).
                    if (chg) newhi = (vlen >= 32) ? ~0u : ((1u << vlen) - 1u);
                } else if (highlight) {
                    for (uint16_t i = 0; i < width; ) {
                        if (cf[i] < '0' || cf[i] > '9') { ++i; continue; }
                        uint16_t a = i;
                        while (i < width && cf[i] >= '0' && cf[i] <= '9') ++i;
                        uint32_t run =
                            (((i - a) >= 32) ? ~0u : ((1u << (i - a)) - 1u)) << a;
                        // any digit changed -> light the run
                        if (chg & run) newhi |= run;
                    }
                }
            }

            // Emit a byte when its glyph or its highlight state changed.
            uint32_t emit = chg | (newhi ^ oldhi);
            // unchanged: done
            if (emit == 0) { if (paced) *done = (uint16_t)(k + 1); return; }
            // capped/TX full: _emit_deferred set, resume
            if (!_paint(row, col, cf, width, emit, newhi)) return;

            if (cached) {
                memcpy(_vcache[slot], cf, width);
                _vcache[slot][width] = '\0';
                _vhi[slot]   = newhi;
                _vfresh     &= ~(1u << slot);
            }
            if (paced) *done = (uint16_t)(k + 1);   // emitted: done this period
        }

        /** @brief Pace and paint a changed field: apply the cold-floor +
         *  per-tick cap gate (deferring if exceeded), then hand the @p emit
         *  columns of @p cf (highlighted per @p hi) to the terminal's
         *  paint_field_runs(). All-or-nothing; the guaranteed floor means a
         *  field taken past the gate always fits. */
        bool _paint(uint16_t row, uint16_t col, const char* cf, uint16_t width,
                    uint32_t emit, uint32_t hi) {
            if (emit == 0) return true;

            // Emit only while the ring is shallow (shared depth floor) and
            // under the per-tick cap; else defer (cache untouched, retried next
            // tick).
            // The cap is per-pass - hot and cold each reset _emit_count and use
            // their own budget, so they progress in parallel. The floor leaves
            // room for one whole field, so the shared painter can't truncate.
            const uint16_t cap = _cold_active ? Config::TX_COLD_FIELDS_PER_TICK
                                              : Config::TX_HOT_FIELDS_PER_TICK;
            if (_emit_count >= cap || !_terminal.tx_below_cold()) {
                _emit_deferred = true;
                return false;
            }
            // coalesce + emit
            _terminal.paint_field_runs(row, col, cf, width, emit, hi);
            _emit_count++;   // one field spent this tick
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

        /** @brief Format a ms uptime as "DDDDd HH:MM:SS" (fixed-width fields,
         *  so each subfield keeps a stable column for per-subfield
         *  highlighting). */
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
        /** @brief Rewrite the cold values (global refresh period). Must NOT
         *  touch hot fields, or they would paint twice per cycle and the
         *  highlight would flicker. */
        virtual void draw_dynamic() = 0;
        /** @brief Rewrite the hot values - the per-second subset. Default none;
         *  see _hot_ms. Kept disjoint from draw_dynamic(). */
        virtual void draw_hot() {}

        /** @} */

        /** @brief Bind to a terminal; @p value_col is where values are
         *  written. */
        MetricPage(AnsiTerminal& terminal, uint16_t value_col)
            : _terminal(terminal), _value_col(value_col) { _invalidate(); }

    public:
        /** @name Page interface
         *  @{ */

        void handle_text(char) override {}

        /** @brief ENTER forces an immediate value refresh (otherwise throttled
         *  to _refresh_ms); other keys ignored. ENTER routes through the paced
         *  cold pass next tick rather than painting inline, so it obeys the
         *  same cap/gate/ordinal. */
        void handle_command(Command cmd, uint16_t = 1) override {
            if (cmd == Command::ENTER) {
                _refresh_last_ms = HAL::Clock::uptime_ms();
                _cold_done       = 0;       // revisit every field
                _dynamic_pending = true;    // tick() runs the paced refresh
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _invalidate();   // screen cleared on switch: repaint everything
            _static_done = 0;
            _cold_done   = 0;
            _hot_done    = 0;
            // Run the cold pass as soon as static finishes (even when static is
            // paced over ticks), else tick() would wait a full _refresh_ms for
            // the first value paint.
            _dynamic_pending = true;
            _hot_last_ms     = 0;   // fire hot the moment static drains
            // Static is paced first; hot AND cold wait for it (their values
            // need their labels in place), so only the status bar interleaves
            // during the static phase.
            _run_static();
            if (!_static_pending) {
                _hot_idx = 0;
                _hot_active = true;
                _emit_count = 0;
                _emit_deferred = false;
                draw_hot();       // hot fields (paced)
                _hot_active = false;
                _hot_pending = _emit_deferred;
                _hot_last_ms = HAL::Clock::uptime_ms();
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
            uint64_t now = HAL::Clock::uptime_ms();

            // Hot and cold each get their own per-tick budget (each resets
            // _emit_count and caps on its own constant), so they progress in
            // parallel. Both wait for static to drain (their values need their
            // labels first).

            // Static: paced on the cold budget, runs every tick until complete.
            if (_static_pending) _run_static();

            const bool refresh_due = (now - _refresh_last_ms >= _refresh_ms);

            // Hot fields: once static is drawn, repaint on the per-second
            // cadence or keep resuming an unfinished pass (so every hot field
            // gets a first paint without waiting for the slower cold pass). The
            // cadence clock advances only on a fresh due tick, not a
            // pending-only resume.
            auto run_hot = [&]() {
                if (_static_pending || !_hot_ms) return;
                const bool hot_due = (now - _hot_last_ms >= _hot_ms);
                if (!(hot_due || _hot_pending)) return;
                // Fresh period revisits every hot field; a resume keeps
                // _hot_done so it skips fields already drawn this period (at
                // most one paint per _hot_ms).
                if (hot_due) { _hot_last_ms = now; _hot_done = 0; }
                _hot_idx = 0;
                _hot_active = true;
                _emit_count = 0;   // hot's own per-tick budget
                _emit_deferred = false;
                draw_hot();
                _hot_active = false;
                _hot_pending = _emit_deferred;
            };

            // Cold fields: only once static is fully drawn. On the refresh
            // period or a deferred-pass resume; resume doesn't disturb the
            // cadence (_refresh_last_ms advances only on a fresh refresh). A
            // fresh period resets the ordinal so every field is revisited (the
            // value cache skips the unchanged ones).
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

            // Alternate which pass claims the shared TX floor first: a fixed
            // hot-first order lets a per-tick hot field take every opening and
            // starve cold under congestion; flipping splits the openings
            // fairly. Irrelevant when the link keeps up (the floor never
            // binds).
            if (_paint_cold_first) { run_cold(); run_hot(); }
            else                   { run_hot();  run_cold(); }
            _paint_cold_first = !_paint_cold_first;
        }

        /** @} */
    };

}}

#endif // PAGES_METRIC_PAGE_H
