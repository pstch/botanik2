/**
 * @file scrollback.cpp
 * @brief Scrollback implementation — line storage, soft-wrap splitting, and
 *        throttled incremental redraw.
 */
#include <algorithm>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/pages/scrollback.h"

namespace Botanik { namespace Pages {

    void Scrollback::store(const char* buf, size_t len) {
        uint16_t store_len = (uint16_t)std::min(len, (size_t)(Config::SHELL_SCROLLBACK_LINE_LEN));
        memcpy(_buf[_head], buf, store_len);
        _lens[_head] = store_len;
        _head = (uint16_t)((_head + 1) % Config::SHELL_SCROLLBACK_LINES);
        if (_count < Config::SHELL_SCROLLBACK_LINES) _count++;
    }

    uint16_t Scrollback::store_wrapped(const char* buf, size_t len, uint16_t cols) {
        // Continuation rows (whether forced by a '\n' or by overflowing the
        // width) start with an indent + "> " marker so they align under the
        // message body; only the very first row is emitted verbatim.
        //
        // The 24-space indent is hand-tuned to the formatted log-line prefix from
        // logs/format.h ("YY/MM/DD HH:MM:SS.mmm [x] TOPIC: ") so wrapped log rows
        // line up under the message column. It's an approximation (the TOPIC label
        // varies in width); if the log line layout changes, revisit this width.
        static const char PREFIX[]  = "                        > ";
        const uint16_t    plen      = (uint16_t)(sizeof(PREFIX) - 1);
        const uint16_t    seg_cap   = (uint16_t)Config::SHELL_SCROLLBACK_LINE_LEN;
        char seg[Config::SHELL_SCROLLBACK_LINE_LEN];

        uint16_t count = 0;
        for (size_t off = 0; ; ) {
            uint16_t prefix_len = (count == 0 || cols <= plen) ? 0 : plen;
            // Per-row byte budget: fill to the width, or everything left when
            // unbounded (cols == 0), but never overflow the segment buffer.
            uint16_t budget = (cols == 0) ? (uint16_t)(len - off)
                                          : (uint16_t)(cols - prefix_len);
            if (budget > (uint16_t)(seg_cap - prefix_len)) budget = (uint16_t)(seg_cap - prefix_len);

            // Take up to `budget` bytes, stopping early at a newline.
            uint16_t take = Utils::scan_line_segment(buf, off, len, budget);

            if (prefix_len) memcpy(seg, PREFIX, prefix_len);
            memcpy(seg + prefix_len, buf + off, take);
            store(seg, (size_t)(prefix_len + take));
            count++;

            off += take;
            if (off < len && buf[off] == '\n') off++;  // consume the break, don't print it
            if (off >= len) break;
        }
        return count;
    }

    void Scrollback::clear() {
        _head     = 0;
        _count    = 0;
        _redraw_i = UINT16_MAX;
    }

    const char* Scrollback::line(uint16_t from_end, uint16_t& len) const {
        if (from_end == 0 || from_end > _count) return nullptr;
        uint16_t idx = (uint16_t)((_head + Config::SHELL_SCROLLBACK_LINES - from_end)
                                   % Config::SHELL_SCROLLBACK_LINES);
        len = _lens[idx];
        return _buf[idx];
    }

    void Scrollback::begin_redraw(uint16_t log_rows) {
        // The content area is cleared by AnsiTerminal before focus()/redraw(), so
        // we only stage the rows to (re)draw here.
        _redraw_i = UINT16_MAX; // cancel any pending drain
        if (_count == 0 || log_rows == 0) return;

        _redraw_count     = (_count < log_rows) ? _count : log_rows;
        _redraw_start     = (uint16_t)((_head + Config::SHELL_SCROLLBACK_LINES - _redraw_count)
                                        % Config::SHELL_SCROLLBACK_LINES);
        _redraw_first_row = (uint16_t)(log_rows - _redraw_count + 1);
        _redraw_i         = 0;
    }

    void Scrollback::drain_redraw(AnsiTerminal& terminal) {
        if (_redraw_i == UINT16_MAX) return;
        // Capture the cursor's resting place once, at the start of the replay, so we
        // can restore it when the replay finishes instead of bouncing back to it on
        // every tick — each intermediate restore was a wasted absolute move (in
        // LogsPage, straight back to {1,0}) only to leave it again next tick.
        if (_redraw_i == 0) _redraw_saved = terminal.cursor_pos();
        // Emit a small batch per call, stopping early if the TX buffer can't fit a
        // worst-case row, so the historical redraw can never overrun the serial TX
        // buffer in a single tick.
        for (uint16_t k = 0; k < Config::SCROLLBACK_REDRAW_ROWS_PER_TICK
                             && _redraw_i != UINT16_MAX; k++) {
            if (terminal.tx_available() < (uint16_t)(terminal.cols() + 32)) break;
            uint16_t idx = (uint16_t)((_redraw_start + _redraw_i) % Config::SHELL_SCROLLBACK_LINES);
            terminal.move_to({(uint16_t)(_redraw_first_row + _redraw_i), 0});
            uint16_t write_len = (uint16_t)std::min((size_t)_lens[idx], (size_t)terminal.cols());
            terminal.write_chars(_buf[idx], write_len);
            terminal.clear_to_eol();
            if (++_redraw_i >= _redraw_count) _redraw_i = UINT16_MAX;
        }
        // Only put the cursor back once the whole replay has drained. While it's
        // still pending the page holds off other output, so leaving the cursor in
        // the log area between ticks is safe — the next batch repositions absolutely.
        if (_redraw_i == UINT16_MAX) terminal.move_to(_redraw_saved);
    }

}}
