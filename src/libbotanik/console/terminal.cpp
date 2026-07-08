// REVIEW_35: TODO
/**
 * @file terminal.cpp
 * @brief AnsiTerminal implementation - the VT device: cursor/geometry tracking,
 *        text output, content scrolling, the shared field painter, and TX gating.
 */
#include <cstdint>
#include <algorithm>

#include "libbotanik_config.h"

#include "libbotanik/utils/serial.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/ansi.h"


namespace Botanik { namespace Terminal {

    // Lifecycle

    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols,
                               uint16_t rows, uint32_t baud) :
        _serial(serial),
        _cols(cols),
        _rows(rows)
    {
        // Size the shared cooperative-TX depth floor from the link baud so the
        // felt echo latency target (Config::TX_COLD_TARGET_MS) holds across links.
        _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
    }

    // Cursor & geometry

    void AnsiTerminal::move_to(const CursorPos& target_in, bool skip) {
        // The hardware caps the cursor at the last row/column, so clamp any
        // out-of-range target here to keep _hw_pos honest.
        CursorPos target = {
            (uint16_t)std::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row),
            (uint16_t)std::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col),
        };
        // A direct move (not recording a transaction, not draining one) sets the
        // desired live cursor - where tx_drain() re-homes a visible cursor to.
        if (!_recording && !_draining) _live_cursor_pos = target;
        // While recording, the first move_to fixes the transaction's base: the
        // scheduler emits the optimal move to it at drain, so it is not recorded.
        if (_recording && _rec_base_pending) {
            _txn_base = target;
            _hw_pos = target;          // content moves optimize from the base
            _hw_at_eol = false;
            _rec_base_pending = false;
            return;
        }
        if (target == _hw_pos) {
            return;
        }

        // All-or-nothing: on TX-full leave _hw_pos untouched (a wrong _hw_pos
        // smears every later write). skip=true tracks without emitting.
        bool ok = true;
        if (!skip) {
            // After a row-filling write the real column is unknown (a wider
            // terminal parks further right), so a relative move lands short:
            // reposition absolutely.
            if (_hw_at_eol) {
                ok = Ansi::goto_absolute(*_sink, target);
            }
            // Prefer a relative move when sharing the target's row or column
            // (never longer than an absolute goto); absolute is for true
            // diagonals only.
            else if (target.row == _hw_pos.row) { // Same row
                if (target.col == 0) {
                    ok = Ansi::move_row_start(*_sink);   // 1-byte CR beats CUB
                } else if (target.col < _hw_pos.col) {
                    ok = Ansi::move_left(*_sink, _hw_pos.col - target.col);
                } else {
                    ok = Ansi::move_right(*_sink, target.col - _hw_pos.col);
                }
            } else if (target.col == _hw_pos.col) { // Same column
                if (target.row < _hw_pos.row) {
                    ok = Ansi::move_up(*_sink, _hw_pos.row - target.row);
                } else {
                    ok = Ansi::move_down(*_sink, target.row - _hw_pos.row);
                }
            } else if (target.col == 0 && target.row > _hw_pos.row
                       && (target.row - _hw_pos.row) < 4
                       && target.row < (uint16_t)(_rows - 1)) {
                // Common "spawn next row" target - CR + LF(s) is cheaper than
                // goto_absolute. Bounded off the last row so a stray LF can't
                // scroll.
                ok = Ansi::down_to_row_start(*_sink,
                                             (uint16_t)(target.row - _hw_pos.row));
            } else { // Different row and column, use absolute move
                ok = Ansi::goto_absolute(*_sink, target);
            }
        }
        if (ok) {
            _hw_pos = target;
            _hw_at_eol = false;
        }
    }

    void AnsiTerminal::restore_cursor(const CursorSnapshot& snap, bool restore_pos) {
        set_inverted(snap.inverted);
        if (restore_pos) {
            move_to(snap.pos);
            // move_to() cleared it; the page may have parked at EOL.
            _hw_at_eol = snap.at_eol;
        }
        set_cursor_visible(snap.cursor_visible);
    }

    // Text output & character attributes

    uint16_t AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        if (len == 0) return 0;

        // Lazy SM4/RM4 toggling: keep insert mode latched so consecutive
        // inserts cost just the char byte, instead of re-bracketing each with
        // SM4/RM4.
        if (insert != _hw_insert_mode) {
            // Commit the latch only if the toggle reached the wire. Latching
            // past a dropped RM4 would let a later replace-mode write paint in
            // insert mode and corrupt the line; bail and let the caller redraw.
            if (!Ansi::set_insert_mode(*_sink, insert)) return 0;
            _hw_insert_mode = insert;
        }

        // Track only what reached the wire: write() drops the tail when TX
        // fills, so advancing _hw_pos by the full len would desync tracking.
        // Dropped glyphs are content loss a redraw fixes; a wrong cursor isn't.
        uint16_t written = _sink->write(s, len);
        uint32_t new_col = (uint32_t)_hw_pos.col + written;
        if (new_col >= _cols) {
            if (_hw_auto_wrap) {
                _hw_pos.col = 0;
                _hw_pos.row++;
                if (_hw_pos.row >= _rows) _hw_pos.row = _rows - 1;
                _hw_at_eol = false;
            } else {
                _hw_pos.col = _cols - 1;
                // Parked at the last cell, which holds a char we just wrote;
                // flag it so a follow-up clear_to_eol doesn't erase it.
                _hw_at_eol = true;
            }
        } else {
            _hw_pos.col = (uint16_t)new_col;
            _hw_at_eol = false;
        }
        return written;
    }

    void AnsiTerminal::set_inverted(bool enable) {
        if (enable == _hw_inverted) return;
        // Reserve the longest form (CSI + ATTR_NEG) up front so a split SGR
        // can't swallow following payload bytes.
        constexpr uint16_t SGR_MAX =
            (sizeof(Ansi::CSI) - 1) + (sizeof(Ansi::ATTR_NEG) - 1);
        if (_sink->availableForWrite() < SGR_MAX) return;

        _sink->write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        if (enable) _sink->write(Ansi::ATTR_NEG,  sizeof(Ansi::ATTR_NEG) - 1);
        else        _sink->write(Ansi::ATTR_NONE, sizeof(Ansi::ATTR_NONE) - 1);
        _hw_inverted = enable;
    }

    void AnsiTerminal::set_auto_wrap(bool enable) {
        // Only commit the tracked mode if the sequence reached the wire: a stale
        // _hw_auto_wrap makes write_chars mispredict the wrap and smear _hw_pos.
        if (!Ansi::set_auto_wrap(*_sink, enable)) return;
        _hw_auto_wrap = enable;
    }

    void AnsiTerminal::set_cursor_visible(bool show) {
        if (show == _hw_cursor_visible) return;
        // Only commit the tracked state if the sequence actually reached the
        // wire, so a TX-full skip is retried on the next call rather than
        // silently lost.
        if (!Ansi::set_cursor_visible(*_sink, show)) return;
        _hw_cursor_visible = show;
    }

    void AnsiTerminal::fill_spaces(uint16_t to_col) {
        // 31 spaces + NUL; the NUL must never reach the wire (terminals drop it
        // without moving, but write_chars would still bump _hw_pos by its len).
        static const char sp[32] = "                               ";
        constexpr uint16_t SP_LEN = sizeof(sp) - 1;
        // Drive termination off a local counter, not _hw_pos.col: filling to the
        // last column parks the cursor at _cols-1 (auto-wrap is off UI-wide), so
        // it never reaches to_col == _cols and a _hw_pos.col loop would spin.
        for (uint16_t col = _hw_pos.col; col < to_col; col = (uint16_t)(col + SP_LEN)) {
            write_chars(sp, std::min((uint16_t)(to_col - col), SP_LEN));
        }
    }

    // Content manipulation

    void AnsiTerminal::delete_chars(uint16_t len) {
        if (len == 0) return;
        // DCH shifts content left; cursor unaffected
        Ansi::delete_chars(*_sink, len);
    }

    void AnsiTerminal::clear_to_eol() {
        // Cursor parked at the last cell after a row-filling write: nothing
        // past it to erase, and the parked cell holds valid content.
        if (_hw_at_eol) return;
        Ansi::clear_to_eol(*_sink);  // EL leaves the cursor in place
    }

    void AnsiTerminal::clear_to_eos() {
        Ansi::clear_to_eos(*_sink);
        _hw_at_eol = false;
    }

    void AnsiTerminal::clear_screen() {
        // Drop bytes still queued for TX - they'd paint onto a display we're
        // wiping. Emptying the ring also guarantees the clear sequence fits, so
        // the _hw_pos reset below is correct with no dropped-write path.
        _serial.tx_discard();
        // Resync the terminal: the discard may have torn a half-sent sequence
        // on the wire, which would otherwise swallow the leading bytes of the
        // clear below.
        Ansi::cancel_sequence(*_sink);
        establish_screen();
    }

    void AnsiTerminal::clear_screen_now() {
        clear_screen();
        // Push the clear out now: the loop won't run again to drain the ring.
        // transmit() never blocks, so spin until empty, bounded so a stuck wire
        // can't hang the exit path.
        for (uint16_t i = 0; i < tx_capacity() && !_serial.tx_empty(); i++)
            _serial.transmit();
    }

    void AnsiTerminal::establish_screen() {
        // Clear+home, then reset tracked cursor/wrap state. No TX discard here
        // - the caller owns that (clear_screen drops stale paint; the
        // first-step bootstrap keeps the setup_*() handshake it queued).
        Ansi::clear_screen(*_sink);
        _hw_pos = {0, 0};
        _hw_at_eol = false;
        // Boot marker: emit "ACAB" once per process (not on Ctrl-L redraws) to
        // mark a fresh start in the stream. Track the 4 cols it advances (cols
        // >= 5, so it can't wrap).
        if (!_screen_ready) {
            _sink->write("ACAB", 4);
            _hw_pos.col = 4;
        }
        // Auto-wrap stays off UI-wide (pages pre-wrap; a stray wrap parks the
        // cursor and the next clear_to_eol eats a glyph). Emit unconditionally
        // (a prior copy may have been discarded).
        set_auto_wrap(false);
        // Hide the cursor by default (only the shell editor opts back in via
        // focus()). A fresh clear leaves it shown, so force the emit by
        // flipping the shadow past the lazy guard.
        _hw_cursor_visible = true;
        set_cursor_visible(false);
        // Reset reverse-video and insert mode: ED clears neither, so the unit
        // may still hold them while our shadows read "normal". Force both emits
        // to resync.
        force_inverted(false);                  // -> SGR normal (ESC[m)
        Ansi::set_insert_mode(*_sink, false);  // -> replace mode (ESC[4l)
        _hw_insert_mode = false;
        _screen_ready = true;
        // The clear wiped the alarm-bar row; the Console re-flags it for the next
        // bar pass (the device knows nothing of the bar).
    }

    void AnsiTerminal::setup_vt102() {
        // Plain VT100/VT102 (the default): no special handshake. DECAWM and the
        // clear are handled later (establish_screen / first step()).
        Ansi::vt102_setup(*_sink);
    }

    void AnsiTerminal::setup_minitel() {
        // Drive a real Minitel (1B+/2) over the DIN link. The PRO2 handshake
        // (sent before the ANSI clear, while the unit is still native) selects
        // 80-col teleinformatique mode, a lowercase keyboard, and page mode.
        Ansi::minitel_setup(*_sink);
    }

    void AnsiTerminal::newline(bool carriage_return) {
        // Emit and track a line feed; the caller confines it to the content band.
        if (carriage_return) {
            _sink->write("\r\n", 2);
            _hw_pos.col = 0;
        } else {
            _sink->write("\n", 1);
        }
        _hw_pos.row++;
    }

    void AnsiTerminal::tx_discard() {
        _serial.tx_discard();
    }

    void AnsiTerminal::cancel_sequence() {
        Ansi::cancel_sequence(*_sink);
    }

    void AnsiTerminal::reset_to(const CursorPos& pos) {
        // Absolute reposition after a tx_discard left _hw_pos unknown; sync the
        // shadow to match (a relative move would land wrong).
        Ansi::goto_absolute(*_sink, pos);
        _hw_pos = pos;
        _hw_at_eol = false;
    }

    void AnsiTerminal::scroll_region(uint16_t top, uint16_t bottom, bool up,
                                     uint16_t n) {
        if (n == 0 || top > bottom) return;
        uint16_t phys_last = (uint16_t)(_rows > 0 ? _rows - 1 : 0);
        if (bottom > phys_last) bottom = phys_last;
        uint16_t height = (uint16_t)(bottom - top + 1);
        if (n > height) n = height;   // scrolling the whole band just clears it

        // DL/IL pair: the first op displaces every row below the band, the
        // compensating op restores it (a band running to the physical bottom
        // needs no restore). Best-effort: sub-ops are all-or-nothing, so a tight
        // ring drops the scroll rather than tearing the layout.
        bool restore = (bottom < phys_last);

        if (up) {
            // Drop the top n rows: DL n at the band top slides rows
            // top+n..bottom up to top..bottom-n (and pulls everything below the
            // band up with them).
            move_to({top, 0});
            delete_rows(n);
            // IL n at the band's new bottom pushes the displaced below-band
            // rows back down and leaves the n freed rows blank. Skipped when
            // the band runs to the screen bottom: the DL already left those
            // rows blank and there is nothing below to restore.
            if (restore) {
                move_to({(uint16_t)(bottom - n + 1), 0});
                insert_rows(n);
            }
        } else {
            // Drop the bottom n rows first (when the band stops short of the
            // screen bottom) so the IL below cannot shove band content onto
            // rows it must leave alone; when the band runs to the bottom those
            // rows just fall off.
            if (restore) {
                move_to({(uint16_t)(bottom - n + 1), 0});
                delete_rows(n);
            }
            // IL n at the band top slides rows top..bottom-n down to
            // top+n..bottom and opens the n freed rows at the top.
            move_to({top, 0});
            insert_rows(n);
        }
    }

    void AnsiTerminal::insert_rows(uint16_t count) {
        if (count == 0) return;
        // IL shifts content; cursor unaffected
        Ansi::insert_rows(*_sink, count);
    }

    void AnsiTerminal::delete_rows(uint16_t count) {
        if (count == 0) return;
        // DL shifts content; cursor unaffected
        Ansi::delete_rows(*_sink, count);
    }

    // Field painter (shared by StatusBar and MetricPage)

    uint16_t AnsiTerminal::emit_field_run(uint16_t row, uint16_t col, const char* cf,
                                          uint16_t a, uint16_t b, uint32_t hi) {
        move_to({row, (uint16_t)(col + a)});
        uint16_t seg = a;
        bool seg_inv = ((hi >> a) & 1u) != 0;
        uint16_t written = 0;
        for (uint16_t k = a; k < b; ++k) {
            bool ki = ((hi >> k) & 1u) != 0;
            if (ki != seg_inv) {
                set_inverted(seg_inv);
                written += write_chars(&cf[seg], (uint16_t)(k - seg));
                seg = k; seg_inv = ki;
            }
        }
        set_inverted(seg_inv);
        written += write_chars(&cf[seg], (uint16_t)(b - seg));
        return written;   // contiguous leading cells that reached the wire
    }

    void AnsiTerminal::paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                                        uint16_t width, uint32_t emit, uint32_t hi) {
        if (emit == 0) return;

        // Bridge clean gaps up to MERGE wide: a ~7-byte cursor move costs more
        // than overwriting a couple of unchanged characters in place.
        const uint16_t MERGE = 4;
        uint32_t render = emit;
        int last = -1;
        for (uint16_t i = 0; i < width; ++i) {
            if (!((emit >> i) & 1u)) continue;
            if (last >= 0 && (int)i - last - 1 <= (int)MERGE)
                for (int g = last + 1; g < (int)i; ++g) render |= (1u << g);
            last = (int)i;
        }

        uint16_t i = 0;
        while (i < width) {
            if (!((render >> i) & 1u)) { i++; continue; }
            uint16_t a = i;
            while (i < width && ((render >> i) & 1u)) i++;
            // [a, i); SGR segmented per hi
            emit_field_run(row, col, cf, a, i, hi);
        }
        set_inverted(false);   // leave the terminal normal
    }

    // Transaction path
    // ------------------------------------------------------------------------

    void AnsiTerminal::tx_begin(uint8_t lane, uint32_t key) {
        _txn_lane = lane;
        _txn_key  = key;
        _txn_saved_pos = _hw_pos;
        _txn_saved_inv = _hw_inverted;
        _txn_saved_eol = _hw_at_eol;
        _txn_saved_cur = _hw_cursor_visible;
        // Recording baseline: the first move_to fixes the base (captured by
        // move_to), content records relative to it, in normal video.
        _rec_base_pending = true;
        _txn_base    = {0, 0};
        _hw_inverted = false;
        _hw_at_eol   = false;
        _recording   = true;
        // Route the ops' bytes into the record buffer instead of the ring.
        _record.reset(_txn_buf, (uint16_t)sizeof(_txn_buf));
        _sink = &_record;
    }

    void AnsiTerminal::tx_commit() {
        _txn_end = _hw_pos;               // where the content left the cursor
        uint16_t len = _record.len;
        _sink = &_serial;                 // ops emit to the ring again
        _recording = false;
        _rec_base_pending = false;
        // The recorded bytes are queued, not sent: the real terminal is unchanged,
        // so restore the shadow to what it was before tx_begin() (no emit).
        _hw_pos            = _txn_saved_pos;
        _hw_inverted       = _txn_saved_inv;
        _hw_at_eol         = _txn_saved_eol;
        _hw_cursor_visible = _txn_saved_cur;
        if (len > 0)
            _tx.submit(_txn_lane, _txn_key, _txn_base.row, _txn_base.col,
                       _txn_end.row, _txn_end.col, _txn_buf, len);
    }

    uint16_t AnsiTerminal::emit(uint16_t base_row, uint16_t base_col,
                                const char* content, uint16_t len,
                                uint16_t end_row, uint16_t end_col) {
        // Conservative atomicity gate: content + a reserve for the leading move
        // and SGR reset. If it fits, the sub-ops below cannot be truncated.
        if (_serial.availableForWrite() < (uint16_t)(len + TX_MOVE_RESERVE)) return 0;
        uint16_t before = tx_used();
        move_to({base_row, base_col});          // minimal move from the real cursor
        if (_hw_inverted) set_inverted(false);  // content assumes normal video
        _serial.write(content, len);            // fits (gated above)
        _hw_pos      = {end_row, end_col};       // content left the cursor here
        _hw_inverted = false;                    // content is balanced (ends normal)
        _hw_at_eol   = false;
        return (uint16_t)(tx_used() - before);
    }

    void AnsiTerminal::tx_drain() {
        uint16_t used   = tx_used();
        uint16_t budget = (used < _tx_cold_depth) ? (uint16_t)(_tx_cold_depth - used) : 0;
        // The terminal is the emitter; emit() tracks the real cursor exactly.
        _draining = true;
        uint16_t sent = _tx.drain(*this, budget, _tx_cold_depth);
        _draining = false;
        // A drained transaction left the real cursor at its end. If a live cursor
        // is showing (the shell editor), re-home it to where the renderer wants it
        // so a row-0 value update can't strand it.
        if (sent > 0 && _hw_cursor_visible) move_to(_live_cursor_pos);
    }

}}
