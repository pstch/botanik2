#ifndef TERMINAL_TERMINAL_H
#define TERMINAL_TERMINAL_H

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/ansi.h"

#include "libbotanik/panes/home.h"
#include "libbotanik/panes/shell.h"
#include "libbotanik/panes/logs.h"
#include "libbotanik/panes/status.h"

namespace Botanik { namespace Terminal {


    class AnsiTerminal {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;
    private:
        // Owned objects
        InputParser         _parser;            // Input parser used to read from the remote terminal
        // Multiplexed panes
        Panes::Pane*        _panes[8];
        uint8_t             _panes_len  = 0;
        uint8_t             _pane_index = 0;
        // Referenced objects
        BufferedSerial&     _serial;            // Reference to the buffered serial UART wrapper
        // Display size
        uint16_t            _cols;
        uint16_t            _rows;
        // Local state
        CursorPos           _hw_pos = {0, 0};   // Hardware position tracking
        bool                _hw_inverted;       // Character attribute tracking
    public:
        // Constructor
        AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows);
        // Step through the terminal handling code
        void step();

        // Moves the cursor position using the most efficient ANSI sequence
        void move_to(const CursorPos& position, bool skip = false);
        // Get the current cursor position
        CursorPos cursor_pos();
        uint16_t col() const;
        uint16_t row() const;

        // Relative moves
        void move_cursor(Ansi::MoveDirection direction, uint16_t amount = 1);

        // Insert characters
        void write_chars(const char *s, uint16_t len, bool insert = false);
        // Delete characters
        void delete_chars(uint16_t len);

        // Switch to the next row (or scroll)
        void line_feed(bool carriage_return = true);
        // Clear the given row
        void clear_row(uint16_t row);
        // Insert rows (shifting subsequent rows)
        void insert_rows(uint8_t rows);
        // Delete rows (shifting subsequent rows)
        void delete_rows(uint8_t rows);

        // Enter/leave inverted display attribute (SGR)
        void set_inverted(bool enable);

        // Switch to the given pane index
        void switch_to(uint8_t index);

        // Get the display size
        uint16_t cols() const;
        uint16_t rows() const;


    };
    
}}

#endif // TERMINAL_TERMINAL_H