/**
 * @file editor.h
 * @brief Multi-line input editor with hardware-accelerated ripples.
 */
#ifndef TERMINAL_EDITOR_H
#define TERMINAL_EDITOR_H

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/terminal.h"


namespace Botanik { namespace Terminal {

    using Ansi::CursorPos;

    /** @brief Geometry of a single screen row, computed from the buffer */
    struct RowView {
        uint16_t buf_start;     /**< Offset in _buf where this row's content begins */
        uint16_t buf_len;       /**< Number of content chars (not counting \\n) */
        bool hard_break;        /**< True if the row ends with \\n */
    };

    /** @brief A sequence of RowView entries describing a contiguous region of the buffer */
    struct TextView {
        static const uint16_t MAX_ROWS = 32;
        RowView  rows[MAX_ROWS];
        uint16_t row_count = 0;
        uint16_t start_row = 0; /**< Prompt-relative row index of rows[0] (0 = PS1 line) */

        /** @brief An empty single-row geometry representing a blank screen */
        static TextView empty() {
            TextView t;
            t.row_count = 1;
            t.start_row = 0;
            t.rows[0]   = {0, 0, false};
            return t;
        }
    };

    /**
     * @class InputEditor
     * @brief High-level logic for managing terminal-based text input
     * * @section RippleAlgo The Ripple Algorithm
     * The editor uses a row-by-row displacement logic to minimize serial bandwidth...
     * * @section NewlineHandling Newline Support
     * Note: In-stream \\n characters force a re-calculation of the row geometry...
     */
    class InputEditor {
    public:

        /**
         * @name Lifecycle & Configuration
         * @{
         */

        /** @brief Construct an InputEditor object bound to the given terminal */
        InputEditor(AnsiTerminal* terminal);

        /** @brief Set the PS1 prompt text (first line prompt) */
        void set_ps1(const char* buf, uint16_t len) {
            len = std::min(len, (uint16_t)(sizeof(_ps1_buf)));
            memcpy(_ps1_buf, buf, len);
            _ps1_len = len;
        }

        /** @brief Set the PS2 prompt text (continuation prompt) */
        void set_ps2(const char* buf, uint16_t len) {
            len = std::min(len, (uint16_t)(sizeof(_ps2_buf)));
            memcpy(_ps2_buf, buf, len);
            _ps2_len = len;
        }

        /** @brief Initialize (or reset) the prompt */
        void reset();

        /**
         * @}
         * @name Layout handling
         * @{
         */

        /** @brief Set the prompt's origin row */
        void set_origin(uint16_t row) {
            row = std::min((uint16_t)(_terminal->rows() - 1), row);
            _origin = row;
        }

        /** @brief Get the amount of rows occupied by the input */
        uint16_t occupied_rows() const {
            return _occupied;
        }

        /**
         * @}
         * @name Input Edition
         * @{
         */

        /** @brief Remove the given amount of characters at the current position */
        void remove_characters(uint16_t len);

        /** @brief Insert the given characters at the current position.
         *         If a selection is active, it is replaced by the inserted text. */
        void insert_characters(char* buf, uint16_t len);

        /** @brief Append the given characters at the current position */
        void append_characters(char* buf, uint16_t len);

        /** @brief Replace the given characters at the current position */
        void replace_characters(char* buf, uint16_t len);

        /** @brief Update the selection range and redraw the damaged region */
        void select_characters(uint16_t anchor, uint16_t len);

        /** @brief Move the cursor one position left; extends selection if mark is set */
        void move_left();

        /** @brief Move the cursor one position right; extends selection if mark is set */
        void move_right();

        /** @brief Move the cursor to the start of the current logical line (Ctrl+A) */
        void move_to_line_start();

        /** @brief Move the cursor to the end of the current logical line (Ctrl+E) */
        void move_to_line_end();

        /** @brief Move the cursor one word to the left (Alt+B) */
        void move_word_left();

        /** @brief Move the cursor one word to the right (Alt+F) */
        void move_word_right();

        /** @brief Delete the character before the cursor.
         *         If a selection is active, deletes the selection instead. */
        void backspace();

        /** @brief Delete the character at the cursor (Ctrl+D).
         *         If a selection is active, deletes the selection instead. */
        void delete_forward();

        /** @brief Clear the active mark and selection (Ctrl+G / ESC) */
        void deselect();

        /** @brief Toggle the mark: set if inactive, deselect if active (Ctrl+Space) */
        void set_mark_toggle();

        /** @brief Redraw the whole prompt */
        void redraw(bool clear = true);

        // TODO
        void set_contents(const char* buf, uint16_t len);

        /** @brief Read-only view of the current buffer contents */
        const char* buffer() const { return _buf; }
        /** @brief Length of the current buffer contents */
        uint16_t buffer_len() const { return _len; }

        /** @brief Read-only view of the PS1 prompt */
        const char* ps1() const { return _ps1_buf; }
        /** @brief Length of the PS1 prompt */
        uint16_t ps1_len() const { return _ps1_len; }

        /** @} */

    private:
        using MoveDirection  = Ansi::MoveDirection;
        using CursorPos      = Ansi::CursorPos;
        static constexpr MoveDirection LEFT  = MoveDirection::LEFT;
        static constexpr MoveDirection RIGHT = MoveDirection::RIGHT;
        static constexpr MoveDirection UP    = MoveDirection::UP;
        static constexpr MoveDirection DOWN  = MoveDirection::DOWN;
        using BufferedSerial = Utils::BufferedSerial;

        /**
         * @name State variables
         * @{
         */

        AnsiTerminal*       _terminal;          /**< Reference to the parent terminal */

        char                _buf[512];          /**< Input contents */
        uint16_t            _len = 0;           /**< Input length */
        uint16_t            _pos = 0;           /**< Current position in input */

        uint16_t            _sel_anchor = 0;    /**< Start of selection */
        uint16_t            _sel_len = 0;       /**< Selection length */

        bool                _mark_active = false; /**< True while a mark has been set */
        uint16_t            _mark_pos = 0;      /**< Buffer position of the mark */

        uint16_t            _origin = 0;        /**< Origin row of the prompt */
        uint16_t            _occupied = 1;      /**< Amount of occupied rows */

        char                _ps1_buf[16];       /**< PS1 contents */
        uint16_t            _ps1_len = 0;       /**< PS1 length */

        char                _ps2_buf[16];       /**< PS2 contents */
        uint16_t            _ps2_len = 0;       /**< PS2 length */

        /**
         * @}
         * @name Prompt handling
         * @{
         */

        /** @brief Write the PS1 prompt */
        inline void _write_ps1() { _terminal->write_chars(_ps1_buf, _ps1_len); }
        inline void _skip_ps1()  { _terminal->move_cursor(RIGHT,    _ps1_len); }

        /** @brief Write the PS2 prompt */
        inline void _write_ps2() { _terminal->write_chars(_ps2_buf, _ps2_len); }
        inline void _skip_ps2()  { _terminal->move_cursor(RIGHT,    _ps2_len); }

        /**
         * @}
         * @name Cursor mapping and change planning/execution
         * @{
         */

        /** @brief Get hardware position from logical position
         *  @note This operation may be O(N) where N is the input buffer length
        */
        CursorPos _map_cursor_pos(uint16_t pos) const;

        /** @brief Build row geometry from the current buffer, starting at the
         *         row containing buf_pos. */
        TextView _build_rows(uint16_t buf_pos) const;

        /** @brief Get how many characters we can write to this line, taking
         *         into account the prompt prefix length. */
        uint16_t _get_row_cap(uint16_t row) const;

        /** @brief Look for a newline at the given position in the input buffer. */
        inline uint16_t _is_newline(uint16_t pos) const {
            return _buf[pos] == '\n';
        }

        /** @brief Remove the selected text, clear selection and mark state.
         *         Leaves _pos at the former selection anchor.
         *  @return true if a non-empty selection was removed, false otherwise. */
        bool _remove_selection();

        /** @brief Common tail for all move operations: update the selection or clear
         *         it, then physically move the cursor to _pos. */
        void _finalize_move();

        /** @brief Scan a row to find the end of the line,
         *         and whether it's a soft or hard line-break.
         */
        RowView _scan_row(uint16_t pos, uint16_t cap) const;

        /**
         * @}
         * @name Ripple handling
         * @{
         */

        /** @brief Shift one screen row by |pressure| chars using ICH/DCH (small
         *         pressure) or IL/DL (large pressure).
         *  @param row           Row index relative to _origin (0 = PS1 row).
         *  @param pos           Offset in _buf where this row's new content begins.
         *  @param pressure  > 0: chars pushed in from the previous row's right edge.
         *                   < 0: chars pulled out into the previous row's vacuum.
         *  @param geometry      Old row geometry snapshot.
         *  @return Overflow (> 0) or remaining vacuum (< 0) to pass to the next row. */
        int16_t _shift_row(uint16_t row, uint16_t pos, int16_t pressure, const TextView& geometry);

        /** @brief Insertion half of _shift_row: open a gap of `pressure` chars at
         *         the cursor using insert mode (small) or IL (large), fill it from _buf,
         *         and return the overflow that must be propagated to the next row.
         *  @param row  Index within the geometry snapshot (0 = first affected row).
         *              Note: different coordinate space from _shift_row's `row`. */
        uint16_t _shift_insert(uint16_t row, uint16_t pos, uint16_t pressure, const TextView& geometry);

        /** @brief Removal half of _shift_row: close a gap of `vacuum` chars at the
         *         cursor using DCH (small) or DL (large), fill the right-edge gap from
         *         the next row's new content, and return the remaining vacuum.
         *  @param row  Index within the geometry snapshot (0 = first affected row).
         *              Note: different coordinate space from _shift_row's `row`. */
        uint16_t _shift_remove(uint16_t row, uint16_t pos, uint16_t vacuum, const TextView& geometry);

        /** @brief Perform the full hardware insertion at the edit point: shift rows
         *         using _shift_row and write any overflow onto new screen lines. */
        void _do_insertion(CursorPos pos, const TextView& geometry, uint16_t pressure);

        /** @brief Perform the full hardware removal at the edit point: shift rows
         *         using _shift_row and erase the tail when vacuum reaches the last row. */
        void _do_removal(CursorPos pos, const TextView& old, uint16_t pressure);

        /** @} */
    };

}}

#endif // TERMINAL_EDITOR_H
