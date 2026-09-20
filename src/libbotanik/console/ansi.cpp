/**
 * @file ansi.cpp
 * @brief ANSI helper implementations
 *
 * Each writer reserves ANSI_MAX_SEQ bytes via availableForWrite() before
 * emitting; if the whole sequence won't fit it writes nothing and returns false,
 * so an escape is never split mid-sequence. Callers may retry on a later redraw.
 */

#include <cstddef>
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE
#include <cstdint>
#include <cstring>

#include "libbotanik/utils/text.h"
#include "libbotanik/console/ansi.h"

namespace Botanik { namespace Terminal { namespace Ansi {

    // Generic utilities

    // worst-case max X3.64 sequence length
    constexpr uint16_t ANSI_MAX_SEQ = 16;

    inline bool can_emit(ByteSink& out) {
        return out.availableForWrite() >= ANSI_MAX_SEQ;
    }

    bool is_printable(uint8_t b) { return b >= 0x20 && b <= 0x7E; }

    void write_uint16(ByteSink& out, uint16_t val) {
        char b[8];
        const char* s = Utils::format_int(b, sizeof(b), (long long)val);
        out.write(s, (uint16_t)std::strlen(s));
    }

    template <size_t N>
    bool emit_csi(ByteSink& out, const char (&tail)[N], uint16_t count = 1) {
        if (count == 0) return true;
        if (!can_emit(out)) return false;
        out.write(CSI, sizeof(CSI) - 1);
        if (count > 1) write_uint16(out, count);
        out.write(tail, (uint16_t)(N - 1));
        return true;
    }

    // State management

    bool set_insert_mode(ByteSink& out, bool active) {
        // SM4/RM4 are the same length, so one instantiation covers both.
        return active ? emit_csi(out, SM4) : emit_csi(out, RM4);
    }

    bool set_inverse(ByteSink& out, bool on) {
        return on ? emit_csi(out, ATTR_NEG) : emit_csi(out, ATTR_NONE);
    }

    bool cancel_sequence(ByteSink& out) {
        if (out.availableForWrite() < (uint16_t)(sizeof(CAN) - 1)) return false;
        out.write(CAN, sizeof(CAN) - 1);
        return true;
    }

    bool set_auto_wrap(ByteSink& out, bool enable) {
        return enable ? emit_csi(out, DECAWM_SET)
                      : emit_csi(out, DECAWM_RESET);
    }

    bool set_cursor_visible(ByteSink& out, bool show) {
        return show ? emit_csi(out, DECTCEM_SET)
                    : emit_csi(out, DECTCEM_RESET);
    }

    bool terminal_setup(ByteSink& out, TerminalType type) {
        if (!is_minitel(type)) return true;   // VT100/VT102 need no handshake
        if (!can_emit(out)) return false;
        out.write(MINITEL_TELINFO,   sizeof(MINITEL_TELINFO) - 1);
        out.write(MINITEL_LOWERCASE, sizeof(MINITEL_LOWERCASE) - 1);
        out.write(MINITEL_PAGE_MODE, sizeof(MINITEL_PAGE_MODE) - 1);
        return true;
    }

    // Cursor movement

    bool goto_absolute(ByteSink& out, const CursorPos& cursor) {
        if (!can_emit(out)) return false;
        out.write(CSI, sizeof(CSI) - 1);
        write_uint16(out, (uint16_t)(cursor.row + 1));
        out.write(";", 1);
        write_uint16(out, (uint16_t)(cursor.col + 1));
        out.write(MOVE, sizeof(MOVE) - 1);
        return true;
    }

    bool move_left(ByteSink& out, uint16_t count) {
        return emit_csi(out, CUB, count);
    }

    bool move_right(ByteSink& out, uint16_t count) {
        return emit_csi(out, CUF, count);
    }

    bool move_up(ByteSink& out, uint16_t count) {
        return emit_csi(out, CUU, count);
    }

    bool move_down(ByteSink& out, uint16_t count) {
        return emit_csi(out, CUD, count);
    }

    bool move_row_start(ByteSink& out) {
        if (!can_emit(out)) return false;
        out.write("\r", 1);
        return true;
    }

    bool feed_lines(ByteSink& out, uint16_t count) {
        if (out.availableForWrite() < count) return false;
        for (uint16_t i = 0; i < count; ++i) out.write("\n", 1);
        return true;
    }
    bool down_to_row_start(ByteSink& out, uint16_t count) {
        if (out.availableForWrite() < (uint16_t)(1 + count)) return false;
        out.write("\r", 1);
        return feed_lines(out, count);
    }

    // Content edition

    bool insert_rows(ByteSink& out, uint16_t count) {
        return emit_csi(out, IL, count);
    }

    bool delete_rows(ByteSink& out, uint16_t count) {
        return emit_csi(out, DL, count);
    }

    bool delete_chars(ByteSink& out, uint16_t count) {
        return emit_csi(out, DCH, count);
    }

    // Content clearing

    bool erase_to_eol(ByteSink& out) {
        return emit_csi(out, EL, 1);
    }

    bool clear_screen(ByteSink& out) {
        if (!can_emit(out)) return false;
        out.write(CSI, sizeof(CSI) - 1);
        out.write("2", 1);
        out.write(ED, sizeof(ED) - 1);       // ESC[2J: erase entire display
        out.write(CSI, sizeof(CSI) - 1);
        // ESC[H: cursor to home (row 1, col 1)
        out.write(MOVE, sizeof(MOVE) - 1);
        return true;
    }

}}}

#endif // BOTANIK_FEATURE_CONSOLE
