// REVIEW_35: TODO
#ifndef PAGES_SHELL_H
#define PAGES_SHELL_H

/**
 * @file shell.h
 * @brief ShellPage - interactive shell page with a line editor and interleaved
 *        live log output.
 */

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scrollback.h"
#include "libbotanik/shell/shell.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Interactive shell page with interleaved log output: a header, a
     *        live log tail, and the editor on the bottom row. @see dev_pages
     * @note As a LogWriter it queues only what arrives while ACTIVE, the full
     *       log being LogsPage's; as a ShellWriter it is CommandShell's output.
     */
    class ShellPage : public Page, public Logs::BufferedLogWriter, public Shell::ShellWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using InputEditor  = Terminal::TextEditor;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry + clear_content/line_feed). */
        Console&           _console;
        /** @brief The VT device it paints through (cached _console.terminal()). */
        AnsiTerminal&      _terminal;
        /** @brief This sink's ring, lent to the BufferedLogWriter base. */
        Logs::LogEntry     _slots[Config::LOG_QUEUE_SHELL];
        /** @brief Buffers the line editor works over. Declared before it: the
         *  editor borrows them in its constructor. */
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> _editor_bufs;
        /** @brief Bottom-row line editor. */
        InputEditor        _editor;
        /** @brief Command interpreter fed by the editor. */
        Shell::CommandShell _shell;
        /** @brief Circular log line buffer + throttled redraw for
         *         redraw/page-switch replay. */
        ShellScrollback _scrollback;
        /** @brief Mirrors tick(active) so write() knows whether it may
         *  paint. */
        bool _active = false;
        /** @brief True inside drain(), so write() uses the indented log
         *  continuation prefix rather than the plain command-output prefix. */
        bool _draining_log = false;
        /** @brief When false ("logs off"), incoming log entries are dropped
         *  instead of queued, inhibiting the live tail. Command output is
         *  unaffected. */
        bool _logs_enabled = true;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        ShellPage(Terminal::Console& console, const SystemContext* sys = nullptr);

        /** @brief Register a site command table (tried before the built-ins). */
        void set_commands(const Shell::ShellCommand* table, size_t count, void* ctx = nullptr) {
            _shell.set_commands(table, count, ctx);
        }

        /** @brief Wire diagnostics targets (logger, alarm model, health
         *  monitor) for the logs / alarms / reboot commands. */
        void set_diagnostics(Logs::Logger* logger, Alarms::AlarmController* alarms,
                             Health::HealthMonitor* health = nullptr) {
            _shell.set_diagnostics(logger, alarms, health);
        }

        /** @brief Wire the NTP client the `ntpsync` command resyncs. */
        void set_ntp(Network::NtpClient* ntp) { _shell.set_ntp(ntp); }

        /** @} */

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "SHELL"; }
        void handle_text(char c) override;
        void handle_text_batch(const char* buf, uint16_t len) override;
        void handle_command(Command cmd, uint16_t count = 1) override;
        bool handle_cancel() override;
        void deactivate() override;
        void focus() override;
        void redraw() override;
        void tick(bool active) override;

        /** @} */

    private:
        /** @name BufferedLogWriter + ShellWriter interface
         *  @{ */

        /** @brief The page's own name, so a loss report says which sink. */
        const char* sink_name() const override { return name(); }
        // One implementation serves both base interfaces.
        void   push(const Logs::LogEntry* entry) override;
        size_t write(const char* buf, size_t len) override;
        void   write_lines(const char* buf, uint16_t len) override;
        void   drain() override;
        void   clear() override;
        void   set_logs_enabled(bool on) override { _logs_enabled = on; }
        bool   logs_enabled() const override { return _logs_enabled; }

        /** @} */

        /** @name Internals
         *  @{ */

        /** @brief Configure the editor's prompts and geometry. */
        void _setup_editor();
        /** @brief Repaint the whole page (header, log area, editor). */
        void _full_redraw();

        /** @brief Rows the transcript window spans: the content area less the
         *  editor's, so the scroll band never reaches the prompt. It moves as
         *  the prompt grows, hence a call and not a member. */
        uint16_t _log_rows() const;

        /** @brief Return the transcript to the live bottom; no-op unless
         *  scrolled back. Every key but PAGE_UP/PAGE_DOWN must do this: the
         *  editor moves the log band without telling the scrollback (line_feed()
         *  on submit, the band shift when the prompt shrinks), so anything
         *  editing the line has to land at the bottom first. */
        void _follow_tail();

        /** @brief Snapshot the current line into @p line, echo it to history and
         *  reset to a fresh prompt. @return the line length. Shared by submit and
         *  cancel. */
        uint16_t _echo_and_reset(char* line, uint16_t cap);

        /** @brief Submit the editor line: echo into the scrollback, advance to
         *  a fresh prompt, dispatch to the shell. Assumes
         *  suppress_redraw(true); clears it. */
        void _submit_command();

        /** @brief Cancel the editor line (Ctrl-C): like submit but runs
         *  nothing. Assumes suppress_redraw(true); clears it. */
        void _cancel_command();

        /** @brief Store each display row of a submitted command in the
         *  scrollback (screen content is already visible). */
        void _record_command(const char* cmd, uint16_t len);

        /** @brief Scroll the log area down by delta rows via IL and fill the
         *  revealed top rows from the scrollback (when the editor shrinks;
         *  avoids a full redraw). */
        void _reveal_log_rows(uint16_t delta);

        /** @} */
    };

}}

#endif // PAGES_SHELL_H
