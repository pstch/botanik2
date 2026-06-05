#include <algorithm>
#include <cstring>

#include "libbotanik_config.h"
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
        // First line: up to `cols` bytes verbatim.
        uint16_t first = (cols == 0) ? (uint16_t)len
                                     : (uint16_t)std::min(len, (size_t)cols);
        store(buf, first);
        if (cols == 0 || len <= first) return 1;

        // Continuation lines: "> " + up to (cols - 2) bytes of the remainder, so
        // each wrapped row stays within the terminal width.
        static const char PREFIX[] = "> ";
        const uint16_t    plen     = (uint16_t)(sizeof(PREFIX) - 1);
        const uint16_t    body     = (cols > plen) ? (uint16_t)(cols - plen) : 1;
        char seg[Config::SHELL_SCROLLBACK_LINE_LEN];
        memcpy(seg, PREFIX, plen);

        uint16_t count = 1;
        for (size_t off = first; off < len; ) {
            uint16_t take = (uint16_t)std::min(len - off, (size_t)body);
            memcpy(seg + plen, buf + off, take);
            store(seg, (size_t)(plen + take));
            off += take;
            count++;
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
        // Emit a small batch per call, stopping early if the TX buffer can't fit a
        // worst-case row, so the historical redraw can never overrun the serial TX
        // buffer in a single tick.
        auto saved = terminal.cursor_pos();
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
        terminal.move_to(saved);
    }

}}
