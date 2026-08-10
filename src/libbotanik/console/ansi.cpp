// REVIEW_35: DONE
/**
 * @file ansi.cpp
 * @brief ANSI helper implementations
 *
 * Each writer reserves ANSI_MAX_SEQ bytes via availableForWrite() before
 * emitting; if the whole sequence won't fit it writes nothing and returns false,
 * so an escape is never split mid-sequence. Callers may retry on a later redraw.
 */

#include <cstddef>                  // size_t
#include <cstdint>                  // uint8_t/uint16_t
#include <cstring>                  // strlen

#include "libbotanik/utils/text.h"  // Utils::format_int
#include "libbotanik/console/ansi.h"

namespace Botanik { namespace Terminal { namespace Ansi {

    // Generic utilities
    // ------------------------------------------------------------------------

    // worst-case max X3.64 sequence length
    constexpr uint16_t ANSI_MAX_SEQ = 16;

    inline bool can_emit(ByteSink& _serial) {
        return _serial.availableForWrite() >= ANSI_MAX_SEQ;
    }

    bool is_printable(uint8_t b) { return b >= 0x20 && b <= 0x7E; }

    void write_uint16(ByteSink& _serial, uint16_t val) {
        char b[8];
        const char* s = Utils::format_int(b, sizeof(b), (long long)val);
        _serial.write(s, (uint16_t)std::strlen(s));
    }

    // TODO: find a better name for _csi_count
    template <size_t N>
    bool _csi_count(ByteSink& _serial, const char (&tail)[N], uint16_t count = 1) {
        if (count == 0) return true;
        if (!can_emit(_serial)) return false;
        _serial.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(_serial, count);
        _serial.write(tail, (uint16_t)(N - 1));
        return true;
    }

    // State management
    // ------------------------------------------------------------------------

    bool set_insert_mode(ByteSink& _serial, bool active) {
        // SM4/RM4 are the same length, so one instantiation covers both.
        return active ? _csi_count(_serial, SM4) : _csi_count(_serial, RM4);
    }

    bool cancel_sequence(ByteSink& _serial) {
        if (_serial.availableForWrite() < (uint16_t)(sizeof(CAN) - 1)) return false;
        _serial.write(CAN, sizeof(CAN) - 1);
        return true;
    }

    bool set_auto_wrap(ByteSink& _serial, bool enable) {
        return enable ? _csi_count(_serial, DECAWM_SET)
                      : _csi_count(_serial, DECAWM_RESET);
    }

    bool set_cursor_visible(ByteSink& _serial, bool show) {
        return show ? _csi_count(_serial, DECTCEM_SET)
                    : _csi_count(_serial, DECTCEM_RESET);
    }

    bool terminal_setup(ByteSink& _serial, TerminalType type) {
        if (!is_minitel(type)) return true;   // VT100/VT102 need no handshake
        if (!can_emit(_serial)) return false;
        _serial.write(MINITEL_TELINFO,   sizeof(MINITEL_TELINFO) - 1);
        _serial.write(MINITEL_LOWERCASE, sizeof(MINITEL_LOWERCASE) - 1);
        _serial.write(MINITEL_PAGE_MODE, sizeof(MINITEL_PAGE_MODE) - 1);
        return true;
    }

    // Cursor movement
    // ------------------------------------------------------------------------

    bool goto_absolute(ByteSink& _serial, const CursorPos& cursor) {
        if (!can_emit(_serial)) return false;
        _serial.write(CSI, sizeof(CSI) - 1);
        write_uint16(_serial, (uint16_t)(cursor.row + 1));
        _serial.write(";", 1);
        write_uint16(_serial, (uint16_t)(cursor.col + 1));
        _serial.write(MOVE, sizeof(MOVE) - 1);
        return true;
    }

    bool move_left(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, CUB, count);
    }

    bool move_right(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, CUF, count);
    }

    bool move_up(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, CUU, count);
    }

    bool move_down(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, CUD, count);
    }

    bool move_row_start(ByteSink& _serial) {
        if (!can_emit(_serial)) return false;
        _serial.write("\r", 1);
        return true;
    }

    bool down_to_row_start(ByteSink& _serial, uint16_t count) {
        if (_serial.availableForWrite() < (uint16_t)(1 + count)) return false;
        _serial.write("\r", 1);
        for (uint16_t i = 0; i < count; ++i) _serial.write("\n", 1);
        return true;
    }

    // Content edition
    // ------------------------------------------------------------------------

    bool insert_rows(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, IL, count);
    }

    bool delete_rows(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, DL, count);
    }

    bool delete_chars(ByteSink& _serial, uint16_t count) {
        return _csi_count(_serial, DCH, count);
    }

    // Content clearing
    // ------------------------------------------------------------------------

    bool clear_screen(ByteSink& _serial) {
        if (!can_emit(_serial)) return false;
        _serial.write(CSI, sizeof(CSI) - 1);
        _serial.write("2", 1);
        _serial.write(ED, sizeof(ED) - 1);       // ESC[2J: erase entire display
        _serial.write(CSI, sizeof(CSI) - 1);
        // ESC[H: cursor to home (row 1, col 1)
        _serial.write(MOVE, sizeof(MOVE) - 1);
        return true;
    }

}}}
