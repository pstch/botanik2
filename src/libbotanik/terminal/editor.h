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
            len = std::min((uint16_t)(_terminal->cols() - 1), len);
            memcpy(_ps1_buf, buf, len);
            _ps1_len = len;
        }

        /** @brief Set the PS2 prompt text (continuation prompt) */
        void set_ps2(const char* buf, uint16_t len) {
            len = std::min((uint16_t)(_terminal->cols() - 1), len);
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

        /** @brief Insert the given characters at the current position */
        void insert_characters(char* buf, uint16_t len);

        /** @brief Append the given characters at the current position */
        void append_characters(char* buf, uint16_t len);

        /** @brief Replace the given characters at the current position */
        void replace_characters(char* buf, uint16_t len);

        // TODO
        void select_characters(uint16_t anchor, uint16_t len);

        /** @brief Redraw the whole prompt */
        void redraw(bool clear = true);

        // TODO
        void set_contents(const char* buf, uint16_t len);

        /** @} */

    private:
        using enum Ansi::MoveDirection;
        using MoveDirection = Ansi::MoveDirection;
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
        
        /** @brief Scan a row to find the end of the line,
         *         and whether it's a soft or hard line-break.
         */
        RowView InputEditor::_scan_row(uint16_t pos, uint16_t cap) const;
        
        /**
         * @}
         * @name Ripple handling
         * @{
         */

        /** @brief Execute the insertion ripple: pressure new chars were written
         *         at edit_pos; text is the OLD row geometry from edit_row onward. */
        void _insertion_ripple(CursorPos edit_pos, const TextView& text,
                               uint16_t pressure);

        /** @brief Execute the removal ripple: pressure chars were removed at
         *         edit_pos; text is the OLD row geometry from edit_row onward. */
        void _removal_ripple(CursorPos edit_pos, const TextView& text,
                             uint16_t pressure);

        /** @} */
    };

}}

#endif  // TERMINAL_EDITOR_H
