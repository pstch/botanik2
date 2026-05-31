#ifndef TERMINAL_TERMINAL_H
#define TERMINAL_TERMINAL_H

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/ansi.h"
#include "libbotanik/logs/logger.h"

#include "libbotanik/pages/page.h"

// Forward declarations to avoid pulling in heavy headers
namespace Botanik { namespace Scheduler { struct SchedulerContext; } }
namespace Botanik { namespace PlcIO     { struct PlcSyncContext;   } }

namespace Botanik { namespace Terminal {


    class AnsiTerminal {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;
    private:
        // Owned objects
        InputParser         _parser;                // Input parser used to read from the remote terminal
        // Multiplexed pages
        Pages::Page*        _pages[8];              // Children pages
        uint8_t             _pages_len  = 0;        // Page count
        uint8_t             _page_index = 0;        // Active page index
        // Referenced objects
        BufferedSerial&     _serial;                // Reference to the buffered serial UART wrapper
        // Display size
        uint16_t            _cols;                  // Columns
        uint16_t            _rows;                  // Rows
        // Local state
        CursorPos           _hw_pos = {0, 0};       // Hardware position tracking
        bool                _hw_inverted = false;   // Character attribute tracking
        bool                _hw_auto_wrap = true;   // DECAWM state (default: on)
        bool                _hw_at_eol = false;     // Cursor parked at last col after a row-filling write
        bool                _hw_insert_mode = false;// SM4/RM4 tracking — toggled lazily by write_chars
        Logs::Logger*                _logger       = nullptr;
        Scheduler::SchedulerContext* _sched_ctx    = nullptr;
        PlcIO::PlcSyncContext*       _plc_ctx      = nullptr;
        uint32_t                     _status_tick  = 0;
        char                         _status_metrics_cache[27] = {}; // zero-init forces first draw
    public:
        /** @name Lifecycle & State Management
         *  @{ */

        /** @brief Initialize the terminal with given dimensions */
        AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                     uint32_t* timestamp_ms, Logs::Logger* logger = nullptr,
                     Scheduler::SchedulerContext* sched_ctx = nullptr,
                     PlcIO::PlcSyncContext* plc_ctx = nullptr);

        /** @brief Process input and update terminal state */
        void step();

        /** @} */

        /** @name Cursor & Geometry Management
         *  @{ */

        /** @brief Move cursor to an absolute position
         *  @param position Targeted coordinates
         *  @param skip If true, only update internal state without sending ANSI sequence
         */
        void move_to(const CursorPos& position, bool skip = false);

        /** @brief Move cursor relatively in the given direction */
        void move_cursor(Ansi::MoveDirection direction, uint16_t amount = 1);

        /** @brief Get the current logical cursor position */
        inline CursorPos cursor_pos() const { return _hw_pos; }

        /** @brief Get the total number of columns (width) */
        inline uint16_t cols() const { return _cols; }

        /** @brief Get the total number of rows (height) */
        inline uint16_t rows() const { return _rows; }

        /** @} */

        /** @name Text Output & Character Attributes
         *  @{ */

        /** @brief Write characters at the current cursor position
         *  @param buf Buffer to write
         *  @param len Number of characters to write
         *  @param insert If true, use insertion mode (shifts existing text right)
         */
        void write_chars(const char *buf, uint16_t len, bool insert = false);

        /** @brief Set or clear the reverse-video (inverted) attribute */
        void set_inverted(bool enable);

        /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l) */
        void set_auto_wrap(bool enable);

        /** @} */

        /** @name Content Manipulation
         *  @{ */

        /** @brief Delete characters at current position, shifting the line left */
        void delete_chars(uint16_t len);

        /** @brief Erase content from cursor to the end of the line */
        void clear_to_eol();

        /** @brief Erase an entire row */
        void clear_row(uint16_t row);

        /** @brief Erase the entire display and home the cursor (ESC[2J + ESC[H) */
        void clear_screen();

        /** @brief Emit a line feed, optionally with a carriage return */
        void line_feed(bool carriage_return = true);

        /** @brief Insert blank rows at cursor, pushing subsequent rows down */
        void insert_rows(uint16_t count);

        /** @brief Delete rows at cursor, pulling subsequent rows up */
        void delete_rows(uint16_t count);

        /** @} */

        /** @name Viewport & Page Management
         *  @{ */

        /** @brief Register a page; returns its index, or -1 if the slot array is full */
        int8_t add_page(Pages::Page* page);

        /** @brief Switch to the page at the given index */
        void switch_to(uint8_t index);

        /** @brief Number of bytes that can still be written to the serial TX buffer */
        uint16_t tx_available() const;

        /** @brief Redraw the always-visible status bar on row 0 */
        void draw_status_row();

        /** @brief Refresh only the metrics portion of the status bar (skips if unchanged) */
        void update_status_metrics();

        /** @brief Access the attached scheduler context (may be null) */
        Scheduler::SchedulerContext* sched_ctx() { return _sched_ctx; }

        /** @} */


    };

}}

#endif // TERMINAL_TERMINAL_H
