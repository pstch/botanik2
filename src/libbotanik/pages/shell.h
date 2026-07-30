// REVIEW_35: TODO
#ifndef PAGES_SHELL_H
#define PAGES_SHELL_H

/**
 * @file shell.h
 * @brief ShellPage - interactive shell page with a line editor and interleaved
 *        live log output.
 */

#include "../../libbotanik_config.h"
#include "page.h"
#include "scrollback.h"
#include "../logs/writer.h"
#include "../console/console.h"
#include "../console/editor.h"
#include "../shell/shell.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Interactive shell page with interleaved log output.
     *
     * Layout (N = terminal rows):
     *   Row 0       - inverted header
     *   Rows 1..N-2 - log area (live tail, scrolls on each incoming entry)
     *   Row  N-1    - InputEditor (PS1 + optional continuation row)
     *
     * As a LogWriter, only entries received while active are queued (push()
     * drops background ones - the full log lives in LogsPage). As a
     * ShellWriter, it gives CommandShell an output channel. All lines are
     * stored in a circular buffer for replay on redraw / page-switch.
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

        ShellPage(Terminal::Console& console);

        /** @brief Register a site command table (tried after the built-ins). */
        void set_commands(const Shell::ShellCommand* table, size_t count, void* ctx = nullptr) {
            _shell.set_commands(table, count, ctx);
        }

        /** @brief Wire diagnostics targets (logger, alarm model) for the
         *  loglevel / alarm-ack commands. */
        void set_diagnostics(Logs::Logger* logger, Alarms::AlarmModel* alarms) {
            _shell.set_diagnostics(logger, alarms);
        }

        /** @brief Wire the NTP client the `ntpsync` command resyncs. */
        void set_ntp(Network::NtpClient* ntp) { _shell.set_ntp(ntp); }

        /** @brief Wire the config backend for the `config` get/set command. */
        void set_params(Params::ParamHandler* config) { _shell.set_params(config); }

        /** @brief Wire the status-variable table for the `status` command. */
        /** @brief Forward the variable model to the shell (`vars`). */
        void set_vars(Vars::Io* io) { _shell.set_vars(io); }

        void set_status_vars(const ScalarGroup* groups, size_t count,
                             const void* base) {
            _shell.set_status_vars(groups, count, base);
        }

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

        // One implementation serves both base interfaces.
        void   push(const Logs::LogEntry* entry) override;
        size_t format(const Logs::LogEntry* entry, char* buf, size_t len) override;
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

        /** @brief Submit the editor line: echo into the scrollback, advance to
         *  a fresh prompt, dispatch to the shell. Assumes
         *  suppress_redraw(true); clears it. */
        /** @brief Snapshot the current line into @p line, echo it to history and
         *  reset to a fresh prompt. @return the line length. Shared by submit and
         *  cancel. */
        uint16_t _echo_and_reset(char* line, uint16_t cap);
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
