/**
 * @file ansi.cpp
 * @brief ANSI helper implementations - decimal emission and the escape-sequence
 *        writers that back the Ansi namespace.
 *
 * Each writer reserves ANSI_MAX_SEQ bytes via availableForWrite() before
 * emitting; if the whole sequence won't fit it writes nothing and returns false.
 * Once the guard passes every write() is guaranteed to complete (the TX ring is
 * single-producer, so free space only grows under us), so an escape can never be
 * split mid-sequence and corrupt the terminal. Callers retry on a later redraw.
 */
#include <cstdint>

#include "libbotanik/terminal/ansi.h"

namespace Botanik { namespace Terminal { namespace Ansi {
    using BufferedSerial = Botanik::Utils::BufferedSerial;

    namespace {
        // Worst-case single sequence: goto_absolute = CSI(2) + row(<=5) + ';' +
        // col(<=5) + 'H' = 14. One guard value for all writers; over-reserving the
        // short ones is harmless and avoids a per-writer count that could drift.
        constexpr uint16_t ANSI_MAX_SEQ = 16;

        // True when the TX ring can take a whole worst-case sequence, so every
        // write() in the writer below is guaranteed to land (see file header).
        inline bool can_emit(BufferedSerial& _serial) {
            return _serial.availableForWrite() >= ANSI_MAX_SEQ;
        }

        // Append @p val as decimal (no leading zeros) to the TX ring.
        void write_uint16(BufferedSerial& _serial, uint16_t val) {
            char buf[5];
            int8_t i = 4;
            if (val == 0) { _serial.write("0", 1); return; }
            while (val > 0 && i >= 0) {
                buf[i--] = (char)('0' + val % 10);
                val /= 10;
            }
            _serial.write(&buf[i + 1], 4 - i);
        }

        // Emit "CSI [count] <final>": the CSI prefix, an optional numeric
        // parameter (written only when > 1, since 1 is the implicit ECMA-48
        // default), then the literal final byte(s). count defaults to 1 for the
        // many sequences that take no parameter (erase, mode set, ...).
        bool _csi_count(BufferedSerial& _serial, const char* final_byte,
                        uint16_t fn, uint16_t count = 1) {
            if (count == 0) return true;
            if (!can_emit(_serial)) return false;
            _serial.write(CSI, sizeof(CSI) - 1);
            if (count > 1) write_uint16(_serial, count);
            _serial.write(final_byte, fn);
            return true;
        }
    }

    // Generic utilities
    // ------------------------------------------------------------------------

    bool is_printable(uint8_t b) {
        return b >= 0x20 && b <= 0x7E;
    }

    // State management
    // ------------------------------------------------------------------------

    bool set_insert_mode(BufferedSerial& _serial, bool active) {
        // SM4/RM4 are the same length.
        return _csi_count(_serial, active ? SM4 : RM4, sizeof(SM4) - 1);
    }

    bool set_auto_wrap(BufferedSerial& _serial, bool enable) {
        // DECAWM_SET/RESET are the same length.
        return _csi_count(_serial, enable ? DECAWM_SET : DECAWM_RESET,
                          sizeof(DECAWM_SET) - 1);
    }

    bool set_cursor_visible(BufferedSerial& _serial, bool show) {
        // DECTCEM_SET/RESET are the same length.
        return _csi_count(_serial, show ? DECTCEM_SET : DECTCEM_RESET,
                          sizeof(DECTCEM_SET) - 1);
    }

    bool minitel_setup(BufferedSerial& _serial) {
        // One-time PRO2 handshake: 80-col téléinformatique + lowercase keyboard +
        // page mode. 12 bytes, within the can_emit() reserve, so it lands atomically.
        if (!can_emit(_serial)) return false;
        _serial.write(MINITEL_TELINFO,   sizeof(MINITEL_TELINFO) - 1);
        _serial.write(MINITEL_LOWERCASE, sizeof(MINITEL_LOWERCASE) - 1);
        _serial.write(MINITEL_PAGE_MODE, sizeof(MINITEL_PAGE_MODE) - 1);
        return true;
    }

    bool vt102_setup(BufferedSerial& _serial) {
        // No one-time setup needed for a VT100/VT102: the persistent DECAWM-off
        // state is (re)asserted on every screen establish in clear_screen(). See
        // the declaration for where future VT102 one-time setup would live.
        (void)_serial;
        return true;
    }

    // Cursor movement
    // ------------------------------------------------------------------------

    bool goto_absolute(BufferedSerial& _serial, const CursorPos& cursor) {
        if (!can_emit(_serial)) return false;
        _serial.write(CSI, sizeof(CSI) - 1);
        write_uint16(_serial, (uint16_t)(cursor.row + 1));
        _serial.write(";", 1);
        write_uint16(_serial, (uint16_t)(cursor.col + 1));
        _serial.write(MOVE, sizeof(MOVE) - 1);
        return true;
    }

    bool move_left(BufferedSerial& _serial, uint16_t count) {
        // Always use the explicit CUB form. We deliberately avoid the BS (\b)
        // shortcut for short hops: BS is non-destructive on a strict VT100/VT102,
        // but the Minitel serial port can treat it as a destructive backspace,
        // which would erase the glyph the cursor steps back over.
        return _csi_count(_serial, CUB, sizeof(CUB) - 1, count);
    }

    bool move_right(BufferedSerial& _serial, uint16_t count) {
        return _csi_count(_serial, CUF, sizeof(CUF) - 1, count);
    }

    bool move_up(BufferedSerial& _serial, uint16_t count) {
        return _csi_count(_serial, CUU, sizeof(CUU) - 1, count);
    }

    bool move_down(BufferedSerial& _serial, uint16_t count) {
        return _csi_count(_serial, CUD, sizeof(CUD) - 1, count);
    }

    bool move_row_start(BufferedSerial& _serial) {
        if (!can_emit(_serial)) return false;
        _serial.write("\r", 1);
        return true;
    }

    bool down_to_row_start(BufferedSerial& _serial, uint16_t count) {
        // CR + count line feeds. The length is caller-controlled, so guard on the
        // exact run rather than ANSI_MAX_SEQ. The caller keeps the feeds clear of
        // the last row, else a line feed scrolls the screen.
        if (_serial.availableForWrite() < (uint16_t)(1 + count)) return false;
        _serial.write("\r", 1);
        for (uint16_t i = 0; i < count; ++i) _serial.write("\n", 1);
        return true;
    }

    // Content edition
    // ------------------------------------------------------------------------

    bool insert_rows(BufferedSerial& _serial, uint16_t count) {
        return _csi_count(_serial, IL, sizeof(IL) - 1, count);
    }

    bool delete_rows(BufferedSerial& _serial, uint16_t count) {
        return _csi_count(_serial, DL, sizeof(DL) - 1, count);
    }

    bool delete_chars(BufferedSerial& _serial, uint16_t count) {
        return _csi_count(_serial, DCH, sizeof(DCH) - 1, count);
    }

    // Content clearing
    // ------------------------------------------------------------------------

    bool clear_to_eol(BufferedSerial& _serial) {
        return _csi_count(_serial, EL, sizeof(EL) - 1);   // ESC[K: erase cursor to EOL
    }

    bool clear_to_eos(BufferedSerial& _serial) {
        return _csi_count(_serial, ED, sizeof(ED) - 1);   // ESC[J: erase cursor to end of display
    }

    bool clear_screen(BufferedSerial& _serial) {
        if (!can_emit(_serial)) return false;
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write("2", 1);
        _serial.write(ED, sizeof(ED) - 1);       // ESC[2J: erase entire display
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write(MOVE, sizeof(MOVE) - 1);   // ESC[H: cursor to home (row 1, col 1)
        return true;
    }

}}}
