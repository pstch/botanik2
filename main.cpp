// Global definitoins
// ==========================================================================

// Includes
// ---------------------------------------------------------------------------

#include <cstdint>
#include <atomic>                       // For std::atomic
#include <ctime>                        // For time_t, struct tm
#include <cstring>                      // For memset

// PLC runtime
#include <AlPlc_Opta.h>                 // For m_PLCShardVarsInputBuf and m_PLCSharedVarsOutputBuf

// Generic utilities
// ---------------------------------------------------------------------------

#ifdef abs
#undef abs
#endif

// Simple comparisons
template <typename T>
constexpr const T& MIN(const T& a, const T& b) { return (a < b) ? a : b; }
template <typename T>
constexpr const T& MAX(const T& a, const T& b) { return (a > b) ? a : b; }
template <typename T>
constexpr const T& POS(const T& a) { return MAX((T)0, a); }
template <typename T>
constexpr const T& NEG(const T& a) { return MIN((T)0, a); }

// Duration conversions
#define TIMEPOINT_TO_MS(x) (std::chrono::duration_cast<std::chrono::milliseconds>(x.time_since_epoch())).count()
#define TIMEPOINT_TO_US(x) (std::chrono::duration_cast<std::chrono::microseconds>(x.time_since_epoch())).count()


// In-order memory copy
template <typename T>
constexpr void ORDERED_COPY(T* dst, const T* src) {
    ARM_MPU_OrderedMemcpy((uint32_t*)dst, (uint32_t*)src, sizeof(T)); 
    __DSB();
}

// In-order memory move
template<typename T>
constexpr void ORDERED_MOVE(T* dst, T src) {
    *dst = src; 
    __DSB();
}

// Value formatting
#define FMT_BOOL(val) (String((val) ? "Enabled" : "Disabled"))
#define FMT_MAC_ADDRESS(mac_array) \
    (String(mac_array[0], HEX) + ":" + \
     String(mac_array[1], HEX) + ":" + \
     String(mac_array[2], HEX) + ":" + \
     String(mac_array[3], HEX) + ":" + \
     String(mac_array[4], HEX) + ":" + \
     String(mac_array[5], HEX))

// Local definitions
// ---------------------------------------------------------------------------

// Logging
#define LOG_PLC_BUFFER_SIZE     32      // should be set accordingly to the PLC's log buffer size
#define LOG_PLC_MESSAGE_LEN     128     // should be set accordingly to the PLC's log buffer size

#define LOG_QUEUE_SIZE          16
#define LOG_FORMATTED_LEN       192

#define LOG_SERIAL_QUEUE_SIZE   16
#define LOG_SERIAL_MSG_LEN      192

#define LOG_MQTT_QUEUE_SIZE     32
#define LOG_MQTT_MSG_LEN        192


// Delays
#define STARTUP_DELAY_MS        5000

// Heartbeat and watchdog
#define WATCHDOG_TIMEOUT_MS     30000
#define HEARTBEAT_INTERVAL_MS   0
#define HEARTBEAT_TIMEOUT_MS    1500
#define HEARTBEAT_ALARM_MS      5000
#define STARTUP_BLINK_MS        250
#define RUNNING_BLINK_MS        2000
#define TIMEOUT_BLINK_MS        500

#define TIMEOUT_BEEP_MS 500
#define ALARM_BEEP_MS 250

// Status notifiers output pins
#define STATUS_BEEPER_PIN 10
#define STATUS_LED_PIN 13

#pragma pack(push, 1)
#pragma pack(pop)

// Mocked PLC interface structures
// ===========================================================================

#pragma pack(push, 1)
enum LogLevel : uint8_t {
    LOG_DEBUG = 1,       // debugging messages, only printed when debugging mode is enabled
    LOG_INFO = 2,        // informational messages, they don't need any particular attention
    LOG_NOTICE = 4,      // important messages, but not indicating any kind of problem
    LOG_WARNING = 8,     // important messages that indicate a potential problem
    LOG_ERROR = 16,      // error messages, something is happenning that should never happen
    LOG_CRITICAL = 32,   // critical message, something very bad is going to happen soon
};


enum LogTopic : uint8_t {
    NONE = 0,
    BOOT = 2,
    NETWORK = 3,
    TIME = 4,               
    STATUS = 5,                   // State machine's state updates
    PLC_IO = 1,            // messages about the PLC shared memory

};

struct LogEntry_t {
    uint32_t        timestamp;
    LogLevel        level;
    LogTopic        topic;
    uint16_t        length;        
    char            message[LOG_PLC_MESSAGE_LEN];
    uint16_t        seqNum;
};

// Input/output flags, used to synchronize the shared memory with the PLC
struct PlcInputCounters_t {
    uint32_t            rxSeqNum; // Sequence number for data sent to the PLC
    uint32_t            txAckNum; // Acknowledgement number for data received from the PLC
};
struct PlcOutputCounters_t {
    uint32_t            txSeqNum; // Sequence number for data received from the PLC
    uint32_t            rxAckNum; // Acknowledgement number for data sent to the PLC
};

// Input/output payload (should be kept in sync with the PLC's shared memory definitions)
struct PlcInputData_t {
    // PLC log read position
    uint32_t            readIndex;
};
struct PlcOutputData_t {
    // Logs received from the PLC
    LogEntry_t          logBuffer[LOG_PLC_BUFFER_SIZE];
    uint32_t            writeIndex;
};

// Input/output data (combined flags and payload)
struct PlcInput_t {
    PlcInputCounters_t  sync;
    PlcInputData_t      data;
};
struct PlcOutput_t {
    PlcOutputCounters_t sync;
    PlcOutputData_t     data;
};
#pragma pack(pop)

#ifdef __INTELLISENSE__
// In actual builds, these are automatically added by the build system.
PlcInput_t& PLCIn = (PlcInput_t&)m_PLCSharedVarsInputBuf;
PlcOutput_t& PLCOut = (PlcOutput_t&)m_PLCSharedVarsOutputBuf;
#else
// In actual builds, we use our mocked definitions to check that their layout
// matches the automatically included type definitions for the input/output structures.
static_assert(sizeof(PLCSharedVarsInput_t) == sizeof(PlcInput_t));
static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcOutput_t));
// XXX: we could improve this by also checking field offsets, but it would
//      require substantial maintenance, unless we can find some macros that
//      handles that for us (or at least make it easier to write)
//      
#endif

// Global rate-limiter parameters
#define LOG_GLOBAL_BUCKET_BURST 20           // Bucket size (maximum burst)
#define LOG_GLOBAL_BUCKET_PERIOD_MS 1000     // Refill period (steady rate)

// Per-topic rate-limiter parameters
#define LOG_PERTOPIC_BUCKET_BURST 20         // Bucket size (maximum burst)
#define LOG_PERTOPIC_BUCKET_PERIOD_MS 5000   // Refill period (steady rate)

#define LOG_TOPIC_MAX 255  // NOTE: the rate-limiter requires 8 bytes per topic 
#define LOG_WRITERS_MAX 8  // NOTE: we just store pointers to writers

#define PLC_HEARTBEAT_MS 1000
#define PLC_TIMEOUT_MS 5000
#define PLC_WATCHDOG_MS 30000


// Check that the defined topic count can fit in the LogTopic enum
static_assert(LOG_TOPIC_MAX < (1 << (sizeof(LogTopic)*8)));

// Utilities
// ===========================================================================

// Non-blocking function to update the status of a digital output that should
// blink with the given period. This function should be called at least once per 
// period.
inline void blink(uint8_t pin, uint32_t period_ms) {
    if (period_ms == 0) { digitalWrite(pin, LOW); return; }
    digitalWrite(pin, (millis() % (period_ms * 2) < period_ms) ? HIGH : LOW);
}

const char* _format_log_level(LogLevel level) { switch (level) {
    case LOG_DEBUG:     return "[~]";
    case LOG_INFO:      return "[-]";
    case LOG_NOTICE:    return "[+]";
    case LOG_WARNING:   return "[!]";
    case LOG_ERROR:     return "[§]";
    case LOG_CRITICAL:  return "[@]";
}; }

const char* _format_log_topic(LogTopic topic) { switch (topic) {
    case NONE:      return "";
    case BOOT:      return "BOOT:";
    case NETWORK:   return "NETWORK:";
    case TIME:      return "TIME:";
    case STATUS:    return "STATUS:";
    case PLC_IO:    return "SHARED_IO";
}; }

size_t _format_log_entry(const LogEntry_t* entry, char* buf, size_t len) {
    const char* level = _format_log_level(entry->level);
    const char* topic = _format_log_topic(entry->topic);
    std::array<char, 22> time_buf; 
    TimeUtils::format_ms_timestamp(TimeUtils::absolute_ms(entry->timestamp), &time_buf);
    int written = snprintf(buf, len, "%s %s %s", time_buf.data(), level, topic, entry->message);
    return POS(MIN((size_t)written, len));
};


// This namespace provides some standard utility functions to work with the 
// different ways we use to represent time :
//  - UNIX timestamps, stored as uint64_t on this platform
//  - absolute millisecond/microsecond uptime counters, stored as uint64_t
//  - rolling-over millisecond/microsecond uptime counters, stored as uint32_t
//
// Some components of our firmware record and transmit 32-bit rolling-over counters
// without additional context, and we need to project them into the current rollover
// window, assuming that they refer to events that occurred in a rollover window
// centered on current time. This namespace provides functions to handle this projection
// (absolute_ms() and absolute_us()), as well a some standard formatting/conversion functions.
namespace TimeUtils {
    // Calendar time struct, without the idiosyncracies from the
    // standard tm struct and with some additional fields for total
    // days and milliseconds
    struct CalendarTime {
        // standard calendar fields
        uint16_t    year;               // years after BC
        uint8_t     month;              // month of current year, 1-indexed
        uint8_t     day;                // day of current month, 1-indexed
        uint8_t     hour;               // hour of current day, 0-indexed
        uint8_t     minute;             // minute of current hour, 0-indexed
        uint8_t     second;             // second of current minute, 0-indexed
        uint16_t    millisecond;        // millisecond of current second, 0-indexed
        // monotonic counters
        uint16_t    unixDays;           // days after UNIX epoch, 0-indexed
        uint64_t    unixSeconds;        // seconds after UNIX epoch, 0-indexed
    };
    // Convert a 32-bit uptime timestamp to an absolute 64-bit timestamp,
    // assuming the given timestamp occurs in the rollover window centered 
    // on the given absolute timestamp (71 minutes for microseconds, 
    // 49 days for milliseconds).
    static uint64_t _absolute_timestamp(uint64_t uptime, uint32_t timestamp) {
        uint32_t current = uptime % (1ULL << 32);
        uptime -= current;
        if (current - timestamp > (1ULL << 31)) uptime -= (1ULL << 32);
        if (timestamp - current > (1ULL << 31)) uptime += (1ULL << 32);
        return uptime + timestamp;
    }
    // Sawtooth millisecond uptime counter (rolls over every ~49 days), using the low-power clock
    //   (equivalent to Arduino's millis() function)
    static uint32_t current_ms()    { return millis(); }
    // Sawtooth microsecond uptime counter (rolls over every ~71 minutes), using the regular clock
    //   (equivalent to Arduino's micros() function)
    //   WARNING: this counter does not run when the device is in SLEEP mode, 
    //            it should only be used for short intervals during which the
    //            device is guaranteed to stay in RUN mode
    static uint32_t current_us()    { return micros(); }
    // Monotonic millisecond uptime counter (no rollover), using the low-power clock
    static uint64_t uptime_ms()     { return TIMEPOINT_TO_MS(mbed::LowPowerClock::now()); }
    // Monotonic microsecond uptime counter (no rollover), using the low-power clock
    //   WARNING: the low-power clock used for this counter will generally not
    //            have enough precision for true microseconds. On an Arduino OPTA, 
    //            this clock runs at 32.768 kHz, which means that the smallest interval
    //            it can measure is around ~30 microseconds.
    static uint64_t uptime_us()     { return TIMEPOINT_TO_US(mbed::LowPowerClock::now()); }
    // Project a rolling-over millisecond uptime counter into the current rollover window
    static uint64_t absolute_ms(uint32_t timestamp_ms) { 
        return _absolute_timestamp(uptime_ms(), timestamp_ms);
    }
    // Project a rolling-over microsecond uptime counter into the current rollover window
    //   WARNING: see warning in uptime_ms() about the microsecond 
    //                precision of the low-power clock
    static uint64_t absolute_us(uint32_t timestamp_us) {
        return _absolute_timestamp(uptime_us(), timestamp_us);
    }
    // Convert a monotonic uptime millisecond counter to an UNIX integer timestamp
    //  (and copy the fractional part to the pointer given in frac_ms)
    static time_t unix_time_ms(uint64_t timestamp_ms, uint16_t* frac_ms) {
        int64_t delta_ms = timestamp_ms - uptime_ms();
        time_t delta_s = (time_t)(delta_ms / 1000);
        if (frac_ms != nullptr) *frac_ms = (uint16_t)std::abs(delta_ms % 1000);
        if (delta_ms < 0 && *frac_ms > 0) delta_s--;
        return time(NULL) + delta_s;
    }
    // Convert an absolute millisecond uptime counter into a calendar datetime
    static CalendarTime cal_time_ms(uint64_t timestamp_ms) {
        uint16_t time_frac; tm time_info; CalendarTime result;
        time_t time_unix = unix_time_ms(timestamp_ms, &time_frac);
        localtime_r(&time_unix, &time_info);
        result.year = (uint16_t)(time_info.tm_year + 1900);
        result.month = (uint8_t)(time_info.tm_mon + 1);
        result.day = (uint8_t)time_info.tm_mday;
        result.hour = (uint8_t)time_info.tm_hour;
        result.minute = (uint8_t)time_info.tm_min;
        result.second = (uint8_t)time_info.tm_sec;
        result.millisecond = time_frac;
        result.unixSeconds = (uint64_t)time_unix;
        result.unixDays = (uint16_t)(time_unix / (uint64_t)86400);
        return result;
    }
    // Format an absolute millisecond uptime counter into an UTC timestamp 
    void format_ms_timestamp(uint64_t timestamp_ms, std::array<char, 22>* buf) {
        CalendarTime time_info = cal_time_ms(timestamp_ms);
        snprintf(
            buf->data(), 22, "%02u/%02u/%02u %02u:%02u:%02u.%03u", 
            time_info.year % 100, time_info.month, time_info.day,
            time_info.hour, time_info.minute, time_info.second, time_info.millisecond
        );
    }
};

// ASCII Terminal implementation
// ===========================================================================

#define ASCII_TERM_MAX_COLS             40
#define ASCII_TERM_MAX_LINES            25

#define ASCII_TERM_INPUT_BUF_SIZE       256
#define ASCII_TERM_OUTPUT_BUF_SIZE      4096

enum AsciiTerminalState {
    T_WRITE,
    T_PARSE,
    T_EXEC,
    T_READ,
};

struct Cursor {
    uint8_t row;
    uint8_t col;
};

class AsciiTerminal2 {
private:

    char        _in[1024];      // User input buffer
    uint16_t    _in_len;        // Current input length
    uint16_t    _in_pos;        // Current input position
    uint16_t    _in_lines;      // Current input line-count
    uint16_t    _in_col;        // Cursor column
    uint16_t    _in_row;        // Cursor row

    char        _ps1[8];        // Prompt to print before the first input line 
    uint8_t     _ps1_len;       // Length of PS1 prompt

    char        _ps2[8];        // Prompt to print on subsequent input lines
    uint8_t     _ps2_len;       // Length of PS2 prompt

    uint8_t     _rows;          // Ouptut row count
    uint8_t     _cols;          // Output column count

    // Print bytes to the serial output
    // XXX: not implemented yet, we'll add buffered output later
    void _print(char* buf, uint16_t len);
    void _blank(uint16_t len);

    // Move the cursor to the given position
    void _move_cursor(uint8_t row, uint8_t col) {
        char   _buf[16]; 
        int     len;
        len = snprintf(_buf, sizeof(_buf), "\033[%d;%dH", row+1, col+1);
        _print(_buf, len);
    }
    
    // Move the cursor to the current position in the input buffer
    void _reset_cursor() {
        Cursor cursor = _cursor_pos(_in_pos);
        _move_cursor(cursor.row, cursor.col);
    }

    // Trigger a hardware scroll event
    void _scroll_up(uint8_t lines) {
        for (uint8_t i = 1; i < lines; ++i) {
            _move_cursor(_rows, _cols);
            _print(" ", 1);
        }
    }

    // Calculate cursor for the given position in the input buffer
    Cursor _cursor_pos(uint16_t pos) {
        Cursor cursor;
        if (pos < _cols - _ps1_len) {
            cursor.row = 0;
            cursor.col = _ps1_len + pos;
        } else {
            pos -= _cols - _ps1_len;
            cursor.row = 1 +        pos / (_cols - _ps2_len);
            cursor.col = _ps2_len + pos % (_cols - _ps2_len);
        }
        return cursor;
    }

    // XXX: may not be needed
    uint16_t _buffer_pos(uint8_t row, uint8_t col) {
        if (row == 0) { return col - _ps1_len; }
        uint16_t line1_len = _cols - _ps1_len;
        uint16_t lineX_len = _cols - _ps2_len;
        return line1_len + (row - 1) * lineX_len + (col -_ps2_len);
    }

    void _insert_char(char byte) {
        // check that we can insert the given character
        if (_in_len >= sizeof(_in)) return;
        if (!isprint(byte)) return;
        bool last = _in_len == _in_pos;
        // if not at the last position, shift left the remaining input buffer
        if (!last) memmove(&_in[_in_pos + 1], &_in[_in_pos], _in_len - _in_pos);
        // store the given character and increment length/position counters
        _in[_in_pos] = byte; _in_len++; _in_pos++;
        // redraw the prompt from the given character
        _print_prompt(_in_pos - 1, _in_len);
        // reset the cursor to its new position
        if (!last) _reset_cursor();
    }

    void _print_prompt_range(uint16_t begin, uint16_t end, bool blank) {
        uint16_t pos = begin;
        Cursor cursor = _cursor_pos(pos);

        
        while (pos < _in_len) {
            // Compute cursor position
            Cursor cursor = _cursor_pos(pos);
            // Compute prompt length for the current row
            uint8_t ps_len = cursor.row == 0 ? _ps1_len : _ps2_len;
            char*   ps_buf = blank ? _blank_buf : (cursor.row == 0 ? _ps1 : _ps2);
            // Write prompt if needed
            if (cursor.col == ps_len) {
                _move_cursor(_rows - _in_lines + cursor.row, 0);
                _print(ps_buf, ps_len);
            } else {
                _move_cursor(_rows - _in_lines + cursor.row, cursor.col);
            }
            // Write current line contents
            uint8_t len = MIN(_cols - cursor.col, _in_len - pos);
            _print((blank ? _blank_buf : _in)[pos], len);
            pos += len;
        }
    }

    void _print_prompt(uint16_t begin, uint16_t end) {
        _print_prompt_range(begin, _in_len, false);
        _print_prompt_range(_in_len, end, true);
        _reset_cursor();
    }

    // Redraw the logs
    void _print_logs() {
        uint8_t rows = _rows - _prompt_lines();
        for (uint8_t line = rows; line > 0; --line) {
            uint8_t row = 
            uint8_t line = lines - row;
            _move_cursor(row, 0);
            _print(_log_buffer.get_line(line), _log_buffer.get_len(line));
        };
        _move_cursor(rows, 0);
    }

    void _read() {
        for (;;) {
            char byte = _serial.read();
            //NOTE: some of these handlers need many steps to run, so we need to store the last character
            //      outside of this function (along with the state required by the multi-step handlers)

            //NOTE: but we could also handle that using a non-blocking output buffer that can contain a full screen

            //QUESTION: how do we handle printing logs into that ?
            switch (byte) {
                case LOGS:
                    _scroll_up(log_lines);
                    _move_cursor(_max_lines - _in_lines() - log_lines, 0);
                    for (uint8_t i = 0; i < log_lines; ++i) {
                        _print(_log_buffer.get_line(i), _max_cols);
                    }
                    _print_prompt();
                    _reset_cursor();
                case -1: 
                    return;
                case EOT:
                    // we received an interruption command, we want to stop the currently executing command
                    break;
                case RETURN:
                    _in_pos = _max_cols - (_in_len % _max_cols);
                    _print(" \b", 2);
                    _in_pos = _in_len;
                    // ASCII return

                    // - move to last position of prompt (last line, last col)
                    // - execute command (will print its output)
                    //   (may take multiple steps)
                    // - show new prompt
                case CTRL_L:
                    _clear();
                    _print_logs();
                    _print_prompt();
                    _reset_cursor();
                    break;
                case CTRL_E:
                    _in_pos = _in_len;
                    _reset_cursor();
                    break;
                case CTRL_A:
                    _in_pos = 0;
                    _reset_cursor();
                    break;
                case CTRL_U:
                    _clear_input();
                    _redraw_prompt();
                    break;
                case CTRL_H:
                case BACKSPACE:
                    if (_in_pos == _in_len) {
                        _in[_in_pos] = '\0';
                        _print("\b \b", 3);
                    } else {
                        _shift_input(_in_pos - 1, -1);
                        _redraw_prompt();
                    };
                    break;
                case CTRL_D:
                    _shift_input(_in_pos, -1);
                    _redraw_prompt();
                    break;
                case CTRL_W:
                    _shift_input(_in_pos, _in_pos-_find_word_begin());
                    _redraw_prompt();
                    break;
                case UP:
                    if (_hist_pos < _hist_len) {
                        _load_history(_hist_pos++);
                        _redraw_prompt();
                    }
                    break;
                case DOWN:
                    if (_hist_pos > 0) {
                        _load_history(_hist_pos--);
                        _redraw_prompt();
                    }
                    break;
                case LEFT:
                    if (_in_pos > 0) { _in_pos -= 1; _move_cursor(); }
                    break;
                case RIGHT:
                    if (_in_pos < _in_len) { _in_pos -= 1; _move_cursor(); }
                    break;
                case ANYCHAR:
                    if (!isprint(byte)) break;
                    _in[_in_pos++] = byte;
                    if (_in_len == _in_pos + 1) {
                        _print(byte, 1);
                    } else {
                        _redraw_prompt();
                    }
            }
        }
    }
}

// This class provides support for reading      
class AsciiTerminal {
    arduino::UART       _serial;
    AsciiTerminalState  _state;
    char                _in[ASCII_TERM_INPUT_BUF_SIZE];
    uint8_t             _in_len;
    char                _out[ASCII_TERM_OUTPUT_BUF_SIZE];
    uint16_t            _out_len;
    uint16_t            _out_sent;

private:
    // Set the output buffer to the given string
    void _set_output(char* buf, size_t len) { memcpy(_out, buf, len); _out_len = len; }
    void _clear_output() { _out_sent = 0; _out_len = 0; memset(_out, 0, sizeof(_out)); }
    // Read bytes from the serial port until the RX buffer is empty or we switch to another state
    AsciiTerminalState _read() { for (;;) {
        char byte = _serial.read();
        switch (byte) {
            case -1:                        // No character read :
                return T_IDLE;
            case 0x08:                      // Handle ASCII backspace :
                if (_in_len == 0) continue;         // if nothing to erase, just skip the backspace
                _in[--_in_len] = '\0';              // clear the last character in the input buffer
                _set_output("\b \b", 3);            // set the output buffer to a character clearing sequence
                return T_WRITE;                     // enter writing mode
            case '\r':                      // Handle ASCII return :
                return T_EXEC;                      // enter command execution mode
            default:                        // Handle ASCII characters :
                if (_in_len == sizeof(_in)) break;     // if no more space, ignore the character
                if (!isprint(byte)) break;          // if not a printable character, ignore the character
                _in[_in_len++] = byte;              // otherwise, store the character in the input buffer
        };
    }; }

    // Attempt to drain the output buffer to the serial port
    bool _write() { 
        int avail = _serial.availableForWrite();
        if (avail == 0) return false;
        size_t len = (size_t)MAX(_out_len -_out_sent, avail);
        _out_sent += _serial.write(&_out[_out_sent], len);
        if (_out_sent < _out_len) return false;
        _clear_output();
        return true;
    }

    bool _dumpstats() {

    }

    bool _dumplogs() {
        for (uint8_t )
        while (!_log_buffer_notempty()) {
            entry = _log_bu
        }
    }

    bool _exec() {
        // XXX: here we could have an inner state machine that handles command execution
        //      because the command may not finish in a single step, or because it may
        //      output more than the output buffer, etc
        // XXX: for now, we just acknowledge the received command, 
        //      the actual implementation of commands will come later
        _out_len = snprintf(_out, ASCII_TERM_OUTPUT_BUF_SIZE, "received command : %s", _in);
        _out_len = MAX(_out_len, (uint16_t)ASCII_TERM_OUTPUT_BUF_SIZE);
        return T_WRITE;


        do_some_stuff();
        print_lines();
        do_some_more_stuff();
        print_more_lines();
    }

    bool _cmd1() {
        switch (_cmd1_state) {
            case C1_A:  
                // do some stuff
                //    
            case C2_A: 
        }
    }

    // 
    void step() { switch (_state) {
        case T_READ:   _state = _read();
        case T_WRITE:  _state = _write() ? T_READ : T_WRITE;
        case T_EXEC:   _state = _exec() ? T_READ : T_EXEC;
        }; 
    }

    void log(LogEntry_t entry) {
        // 
    }



    void print_prompt() {
        _serial.print("\e[");
        _serial.print(MINITEL_1B_LINES);
        _serial.print(";1H");                
        _serial.print("cmd> ");
        _serial.print(_buffer);
    }

    bool readchar() {
        int byte = _serial.read();
        if (byte < 0) return false;
        char chr = (char)chr;

        switch (chr) {
            case '\r': // ASCII return
                process();
                memset(_buffer, 0, sizeof(_buffer));
                _buffer_idx = 0;

                _serial.print("\e[");
                _serial.print(PROMPT_LINE_INDEX + 1);
                _serial.print(";1H");                

                break;
            case 0x08: // ASCII backspace
                if (_buffer_idx == 0) break;
                _buffer_idx--;
                _buffer[_buffer_idx] = '\0';
                _serial.print("\b \b");
                break;
            default:  // Regular character
                if (_buffer_idx == MINITEL_1B_COLS) break;
                _serial.write(chr);
                _buffer[_buffer_idx] = chr;
                break;
        }

    }
}

// Logging
// ===========================================================================

// Log writer interface
// ---------------------------------------------------------------------------

// Base class for the log writers used by the Logger class. Typically
// implemented for raw or human serial outputs, MQTT logging channels, etc.
// 
// The interface defined by this base class allows writers to send a message
// in multiple passes, so that we avoid blocking if the associated transmit
// buffer is full.
class LogWriter {
protected:
    // Byte output handler, implemented by derived classes
    virtual size_t  write(const char* buf, size_t len) = 0;
    // Message formatting handler, implemented by derived classes.
    virtual size_t  format(const LogEntry_t* entry, char* buf, size_t len) = 0;
public:
    // Push a new message to the writer's queue
    virtual void    push(const LogEntry_t* entry) = 0;
    // Drain messages in the writer's queue
    virtual void    drain() = 0;
};

// Abstract buffered log writer
// ---------------------------------------------------------------------------

// This intermediate class handles fixed-size log entry queues so that we can 
// send messages to outputs with small transmit buffers in a non-blocking way.
// This is often required for serial outputs, as they have a small output buffer,
// they often block when data is written to it.

class BufferedLogWriter : public LogWriter {
private:
    // Internal queue for this specific output channel
    LogEntry_t _queue[LOG_QUEUE_SIZE];
    uint8_t _readIndex = 0;
    uint8_t _writeIndex = 0;
    // Outgoing message
    char _message[LOG_FORMATTED_LEN];
    size_t _length = 0;
    size_t _sent = 0;
    // Counters
    uint32_t _dropped = 0;
    uint32_t _failed = 0;
    // Drain bytes from the currently outgoing message
    bool _drain_message() {
        _sent += write(&_message[_sent], _length - _sent);          // write remaining bytes, record actually written bytes
        if (_sent < _length) return false;                          // yield if not fully sent
        _length = 0; _sent = 0; return true;                        // otherwise reset the writer state
    }
public:
    // Add a new entry to the writer's queue
    void push(const LogEntry_t* entry) override {
        uint8_t nextIndex = (_writeIndex + 1) % LOG_QUEUE_SIZE;     // determine next write index
        if (nextIndex == _readIndex) { _dropped += 1; return; }     // drop message if queue is full
        memcpy(&_queue[_writeIndex], entry, sizeof(LogEntry_t));    // otherwise, store the log entry
        _writeIndex = nextIndex;                                    // and record the incremented write index
    }
    // Attempt to drain entries from the queue and print them
    void drain() override {
         if (_length > 0 && !_drain_message()) return;              // drain the current message, yield if not complete
        while (_readIndex != _writeIndex) {                         // check for new entries in the queue,
            const LogEntry_t* entry = &_queue[_readIndex];          // get the next entry in the queue
            _length = format(entry, _message, LOG_FORMATTED_LEN);   // try to format the queue's next message
            if (_length <= 0) { _failed++; continue; }              // if failed, record failure and skip message
            if (!_drain_message()) return;                          // drain the formatted message, yield if not complete
            _readIndex = (_readIndex + 1) % LOG_QUEUE_SIZE;         // increment the read index
        }
    }
};

// Log writer implementations
// ---------------------------------------------------------------------------

class SerialLogWriter : public BufferedLogWriter {
private:
    arduino::UART* _serial;
protected:
    size_t format(const LogEntry_t* entry, char* buf, size_t len) override {
        const char* level = _format_log_level(entry->level);
        const char* topic = _format_log_topic(entry->topic);
        std::array<char, 22> time_buf; 
        TimeUtils::format_ms_timestamp(TimeUtils::absolute_ms(entry->timestamp), &time_buf);
        int written = snprintf(buf, len, "%s %s %s", time_buf.data(), level, topic, entry->message);
        return POS(MIN((size_t)written, len));
    };        
    size_t write(const char* buf, size_t len) override {
        int available = MAX((int)len, _serial->availableForWrite());
        if (available == 0) return 0;
        return _serial->write(buf, available);
    }
public:
    SerialLogWriter(arduino::UART* serial) : BufferedLogWriter(), _serial(serial) {}
};

class DebugLogWriter : public SerialLogWriter {
public:
    DebugLogWriter(arduino::UART* serial): SerialLogWriter(serial) {}
};

class MonitorLogWriter : public SerialLogWriter {
protected:
    void push(const LogEntry_t* entry) override {
        if (entry->level != LOG_DEBUG) SerialLogWriter::push(entry);
    }
public:
    MonitorLogWriter(arduino::UART* serial): SerialLogWriter(serial) {}
};

// Log dispatcher class
// ---------------------------------------------------------------------------

// This class stores references to log writers and pushes
// log entries to their queues. It also provides global and
// per-topic rate-limiting using token bucket filters.
class Logger {
private:
    // bucket type definition
    struct LogBucket_t {
        uint16_t    state                   = 0;
        uint16_t    suppressed              = 0;
        uint32_t    count                   = 0;
    };
    // global rate-limiter
    uint16_t        _global_burst           = LOG_GLOBAL_BUCKET_BURST;
    uint16_t        _global_period_ms       = LOG_GLOBAL_BUCKET_PERIOD_MS;
    uint32_t        _global_fillTime_ms     = 0;
    LogBucket_t     _global_bucket;
    // per-topic rate-limiter
    uint16_t        _perTopic_burst         = LOG_PERTOPIC_BUCKET_BURST;
    uint16_t        _perTopic_period_ms     = LOG_PERTOPIC_BUCKET_PERIOD_MS;
    uint32_t        _perTopic_fillTime_ms   = 0;
    LogBucket_t     _perTopic_buckets[LOG_TOPIC_MAX];
    // output writers list
    LogWriter*      _writers[LOG_WRITERS_MAX];
    uint8_t         _writerCount            = 0;
protected:
    // try to take a token from a bucket
    bool _try_bucket(LogBucket_t* bucket) {
        if (bucket->state <= 0) { bucket->suppressed++;             return false; }
        else                    { bucket->count++; bucket->state--; return true; }
    }
    // try to fill a bucket with the given amount of tokens
    void _fill_bucket(LogBucket_t* bucket, uint16_t amount, uint16_t burst) {
        if (amount > 0 && bucket-> state < burst) {
            bucket->state = MIN((uint16_t)(bucket->state + amount), burst);
        }
    }
public:
    // Constructor
    Logger() {
        _perTopic_fillTime_ms = millis();
        _global_fillTime_ms = millis();
        _global_bucket.state = _global_burst;
        for (uint8_t i = 0; i < LOG_TOPIC_MAX; ++i) {
            _perTopic_buckets[i].state = _perTopic_burst;
        }
    }    
    // Add a writer
    bool add_writer(LogWriter* writer) {
        if (writer == nullptr || _writerCount >= LOG_WRITERS_MAX) return false;    // bail out if given a nullpointer or if the writer list is full
        _writers[_writerCount++] = writer; return true;                            // store the pointer and increment writer count
    }
    // Remove a writer
    bool remove_writer(LogWriter* writer) {
        // do some checks and find the writer's current index
        if (writer == nullptr || _writerCount == 0) return false;                   // bail out if given a nullpointer of if we have no writers
        uint8_t i = 0; while (i < _writerCount && _writers[i] != writer) i++;       // find the index of the given writer
        if (i == _writerCount) return false;                                        // bail out if no writer was found
        for (uint8_t j = i; j < _writerCount - 1; ++j) _writers[j] = _writers[j+1]; // otherwise, shift subsequent writers
        _writers[--_writerCount] = nullptr; return true;                            // decrement the writer count, unset the last pointer
    }    
    // Fill rate-limiting buckets
    void fill_buckets() {
        // XXX: the choice of timer method here determines whether we want to buckets to fill in SLEEP/STOP or not.
        uint32_t _time_ms = millis();
        uint16_t _global_amount = (_time_ms - _global_fillTime_ms) / _global_period_ms;
        _global_fillTime_ms += _global_amount * _global_period_ms;
        _fill_bucket(&_global_bucket, _global_amount, _global_burst);
        uint16_t _perTopic_amount = (_time_ms - _perTopic_fillTime_ms) / _perTopic_period_ms;
        _perTopic_fillTime_ms += _perTopic_amount * _perTopic_period_ms;
        for (uint8_t i = 0; i < LOG_TOPIC_MAX; ++i) {
            _fill_bucket(&_perTopic_buckets[i], _perTopic_amount, _perTopic_burst);
        }
        // TODO: we should report suppressed messages when filling the buckets
    }
    // Push a new log entry to the writers, after applying rate-limiting
    void push_entry(const LogEntry_t entry) {
        LogTopic topic = entry.topic >= LOG_TOPIC_MAX ? LogTopic::NONE : entry.topic;  // determine the topic to use for this entry
        LogBucket_t* topic_bucket = &_perTopic_buckets[topic];                         // get the bucket for this topic
        if (_try_bucket(&_global_bucket))                                              // apply the global filter
            if (topic == LogTopic::NONE || _try_bucket(topic_bucket))                  // if the entry has a topic, apply the per-topic filter
                for (uint8_t i = 0; i < _writerCount; i++)                             // then, for each registered writer
                    _writers[i]->push(&entry);                                          // push the entry to the writer's queue
    }
};

// PLC input/output & synchronization
// ===========================================================================

// Synchronization handler
// ---------------------------------------------------------------------------

// This class provides logic to safely interact with the PLC's shared memory,
// even in the case where we cannot assume that the PLC runtime uses ordered
// memory accesses to interact with the input/output shared memory ranges.
//
// If the PLC runtime is properly implemented, the shared memory ranges should
// be configured to be non-cachable and to use in-order memory accesses. However,
// sometimes the PLC runtime does not document this explicitly, and so we need 
// this wrapper to prevent torn reads and torn writes.
// 
// The synchronization handler is implemented as two distinct states machines,
// one handles reading data from the PLC's output range, and one handles
// writing data to the PLC's input range. Both the reader and the writer use
// explicit in-order operations and a transactional model implemented using
// sequence/acknowledgment counters. Additionally, acknowledging/committing
// memory operations is done in a dedicated state, so that these operations are 
// delayed by the local cycle's period (period between calls to step()).
//
// The reader continuously monitors the sequence number and starts reading when
// it becomes different from the associated acknowledgment number, while the 
// the writer waits for writes to be requested to send a new write message to the
// other side by copying data then incrementing its sequence number and waiting
// for the other side to acknowledge the write.
//
// The synchronization handler also provides a heartbeat mechanism : after a
// successful TX commit, when the heartbeat interval elapses, we do a 
// "dummy write" (incrementing the sequence number without doing an actual write), 
// so that timed_out() can return whether the other side stopped responding.

class PlcSyncHandler {
// NOTE: the seq/ack numbers in PlcInput_t and PlcOutput_t are named from the point of view of the PLC :
//         - TxSeqNum/TxAckNum is RX from our point of view, as it refers to the PLC output data
//         - RxSeqNum/RxAckNum is TX from our point of view, as it refers to the PLC input data
private:
    // Reader state enumeration
    enum PlcRxState {
        RX_IDLE,                                    // wait for a new write message from the other side
        RX_READ,                                    // read data from the PLC's output shared memory
        RX_ACK                                      // acknowledge the write message from the other side
    };
    // Writer state enumeration
    enum PlcTxState {
        TX_WRITE,                                   // write data to the PLC's input shared memory
        TX_COMMIT,                                  // commit a write (or send heartbeat pulse)
        TX_WAIT,                                    // wait for the last commit to be acknowledged
        TX_IDLE                                     // wait for a new write to be requested
                                                    //  (or for the heartbeat interval to elapse)
    };
    // Parameters
    uint32_t        _heartbeat_ms       = PLC_HEARTBEAT_MS;     // heartbeat commit interval
    uint32_t        _timeout_ms         = PLC_TIMEOUT_MS;       // write acknowledgement timeout delay
    uint32_t*       _timestamp_ms       = 0;                    // pointer to the current cycle's timestamp
    // Shared PLC variables
    PlcInput_t*     _plcin;                         // PLC input memory (read-write)
    PlcOutput_t*    _plcout;                        // PLC output memory (read-only)
    // Local buffers
    PlcOutputData_t _rx_buffer;                     // local receive buffer for _plcout
    PlcInputData_t  _tx_buffer;                     // local transmit buffer for _plcin
    // Local state
    PlcRxState      _rx_state           = RX_IDLE;  // reader state
    PlcTxState      _tx_state           = TX_WRITE; // writer state
    uint32_t        _tx_last_ms         = 0;        // last successful write
    bool            _tx_write           = false;    // schedule immediate write
    // Transition conditions
    inline bool _rx_new_message()       { return _plcout->sync.txSeqNum != _plcin->sync.txAckNum;           }
    inline bool _tx_acknowledged()      { return _plcout->sync.rxAckNum == _plcin->sync.rxSeqNum;           }
    inline bool _tx_heartbeat_elapsed() { return *_timestamp_ms - _tx_last_ms > _heartbeat_ms;              }
    // Transition actions
    inline void _rx_memcpy()            { ORDERED_COPY(&_rx_buffer, &_plcout->data);                        }
    inline void _tx_memcpy()            { ORDERED_COPY(&_plcin->data, &_tx_buffer);                         }
    inline void _rx_acknowledge()       { ORDERED_MOVE(&_plcin->sync.txAckNum, _plcout->sync.txSeqNum);     }
    inline void _tx_commit()            { ORDERED_MOVE(&_plcin->sync.rxSeqNum, _plcin->sync.rxSeqNum + 1);  }
    // Reader state machine (handle messages from the PLC's writer)
    PlcRxState _rx_step() { switch (_rx_state) {
        case RX_IDLE:   if (_rx_new_message())              return RX_READ;
        case RX_READ:   _rx_memcpy();                       return RX_ACK;
        case RX_ACK:    _rx_acknowledge();                  return RX_IDLE;
        }; }
    // Writer state machine (send messages to the PLC's reader)
    PlcTxState _tx_step() { switch (_tx_state) {
        case TX_WRITE:  _tx_memcpy(); _tx_write = false;    return TX_COMMIT;
        case TX_COMMIT: _tx_commit();                       return TX_WAIT;
        case TX_WAIT:   if (_tx_acknowledged())             return TX_IDLE; break;
        case TX_IDLE:   if (_tx_write)                      return TX_WRITE;
                   else if (_tx_heartbeat_elapsed())        return TX_COMMIT; break;
    }; }

public:
    // Constructor
    PlcSyncHandler(uint32_t* timestamp_ms) : _timestamp_ms(timestamp_ms) {
        memset(&_tx_buffer, 0, sizeof(PlcInputData_t));
        memset(&_rx_buffer, 0, sizeof(PlcOutputData_t));
    };
    // Get a pointer to the local copy of PLC's output buffer
    const PlcOutputData_t* output() { return &_rx_buffer; }
    // Get a pointer to the local copy of PLC's input buffer
    PlcInputData_t* input() { return &_tx_buffer; }
    // Schedule a write for the next time the handler is stepped through
    void write() { _tx_write = true; }
    // Return whether we have timed out waiting the other side's receiver
    bool timed_out() { return *_timestamp_ms - _tx_last_ms > _timeout_ms; }
    // Step through the input/output synchronization state machines
    void step() { _rx_state = _rx_step(); _tx_state = _tx_step(); }
};

// Log reader
// ---------------------------------------------------------------------------

class PlcLogReader {
private:
    // Parameters
            Logger*         _logger;         // pointer to the output logger
            PlcSyncHandler* _handler;        // pointer to the PLC synchronization handler
    const   uint32_t*       _timestamp_ms;   // pointer to the current cycle's timestamp
    // Reader state
            uint32_t _last_seq = 0;          // last sequence number (to detect dropped entries)
            uint32_t _dropped = 0;           // dropped entry count
    // PLC I/O pointers
    const   LogEntry_t*     _logBuffer;      // pointer to the PLC log buffer
    const   uint32_t*       _writeIndex;     // pointer to the PLC's log entry write index
            uint32_t*       _readIndex;      // pointer to the PLC's log entry read index
    // handle sequence number to detect dropped PLC log entries
    void _detect_dropped_entries(const LogEntry_t* entry) {
        uint32_t _next = _last_seq + 1;
        if (entry->seqNum != _next) _dropped += POS(entry->seqNum - _next);
        _last_seq = entry->seqNum;
    }
    // build an actual log entry, and push it to the logger instance
    void _push_plc_entry(const LogEntry_t* plcEntry) {
        LogEntry_t entry;
        memcpy(&entry, plcEntry, sizeof(entry.message));
        entry.length = (uint8_t)MIN(entry.length, (uint16_t)(LOG_PLC_MESSAGE_LEN-1));
        entry.message[entry.length] = '\0';
        _logger->push_entry(entry);
    }
public:
    // Constructor
    PlcLogReader(PlcSyncHandler* handler, Logger* logger, uint32_t* timestamp_ms) : 
    _logger(logger), _handler(handler), _timestamp_ms(timestamp_ms) {
        _logBuffer = handler->output()->logBuffer;
        _writeIndex = &handler->output()->writeIndex;
        _readIndex = &handler->input()->readIndex;
    }
    // Drain the PLC log buffer
    void drain() {
        while (*_writeIndex != *_readIndex) {
            const LogEntry_t* entry = &_logBuffer[MIN(*_readIndex, (uint32_t)LOG_PLC_BUFFER_SIZE)];
            _detect_dropped_entries(entry);
            _push_plc_entry(entry);
             *_readIndex = (*_readIndex + 1) % LOG_PLC_BUFFER_SIZE;
             _handler->write();
        }
    }

};

// Event loop
// ===========================================================================

Logger              logger;

uint32_t            cycleBegin_ms;

PlcSyncHandler      plcSyncHandler;
PlcLogReader        plcLogReader;
// NetworkManager      networkManager;
// MqttManager         mqttManager;
// NtpManager          ntpManager;
// ProcessManager      processManager;
DebugLogWriter     debugLogWriter;
MonitorLogWriter     monitorLogWriter;
// MqttLogWriter       mqttLogWriter;

void setup() { 
    logger = Logger();

    // PLC synchronization manager and log reader
    plcSyncHandler = PlcSyncHandler(&cycleBegin_ms);
    plcLogReader = PlcLogReader(&plcSyncHandler, &logger, &cycleBegin_ms);

    // Network manager, and MQTT/NTP clients
    // networkManager = NetworkManager(this, &plcManager);
    // mqttManager = MqttManager(this, &plcManager, &networkManager);
    // ntpManager = NtpManager(this, &plcManager, &networkManager);

    // Actual process logic
    // processManager = ProcessManager(this, &plcManager, &ntpManager);

    // Log writers (asynchronous output queues)
    debugLogWriter = DebugLogWriter(&Serial1);
    monitorLogWriter = MonitorLogWriter(&Serial2);
    // TODO: mqttLogWriter = MqttLogWriter(mqttManager);

    // Register writers in logger
    logger.add_writer(&debugLogWriter);
    logger.add_writer(&monitorLogWriter);
    // TODO: logger.add_writer(&mqttLogWriter);
}

// Loop function
void loop() {
    cycleBegin_ms = millis();

    plcSyncHandler.step();
    plcLogReader.drain();

    // networkManager.step();
    // mqttManager.step();
    // ntpManager.step();
    
    // processManager.step();

    debugLogWriter.drain();
    monitorLogWriter.drain();
    // mqttLogWriter.drain();

}
