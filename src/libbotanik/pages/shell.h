#ifndef PAGES_SHELL_H
#define PAGES_SHELL_H

/**
 * @file shell.h
 * @brief ShellPage - interactive shell page with a line editor and interleaved
 *        live log output.
 */

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scrollback.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/terminal/editor.h"
#include "libbotanik/shell/shell.h"

namespace Botanik { namespace Pages {

    /**
     * @brief Interactive shell page with interleaved log output.
     *
     * Layout (N = terminal rows):
     *   Row 0       - inverted header
     *   Rows 1..N-2 - log area (live tail, scrolls on each incoming entry)
     *   Row  N-1    - InputEditor (PS1 + optional continuation row)
     *
     * Implements LogWriter so it can be registered with a Logger.
     * Only entries received while the page is active are queued (push() drops
     * background entries - the full log remains in LogsPage); queued entries are
     * flushed on each tick(). Each entry scrolls the log area (DL at row 1),
     * writes the new line at row N-2, then redraws the editor in place.
     *
     * Implements ShellWriter to provide CommandShell with an output channel.
     * All written lines are stored in a circular buffer for replay on redraw
     * and page-switch (focus).
     */
    class ShellPage : public Page, public Logs::BufferedLogWriter, public Shell::ShellWriter {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using InputEditor  = Terminal::TextEditor;
        using LogEntry_t   = PlcIO::LogEntry_t;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning terminal. */
        AnsiTerminal&      _terminal;
        /** @brief Bottom-row line editor. */
        InputEditor        _editor;
        /** @brief Command interpreter fed by the editor. */
        Shell::CommandShell _shell;
        /** @brief Circular log line buffer + throttled redraw for redraw/page-switch replay. */
        ShellScrollback _scrollback;
        /** @brief Mirrors the latest tick(active) argument so write() - reached
         *  indirectly via drain() - knows whether it may paint. False while in the
         *  background. */
        bool _active = false;
        /** @brief True while inside drain(), so write() - shared by log and command
         *  output - wraps continuations with the indented log prefix rather than the
         *  plain command-output prefix. */
        bool _draining_log = false;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct a ShellPage bound to the given terminal. */
        ShellPage(Terminal::AnsiTerminal& terminal);

        /** @} */

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "SHELL"; }
        void handle_text(char c) override;
        void handle_text_batch(const char* buf, uint16_t len) override;
        void handle_command(Command cmd, uint16_t count = 1) override;
        void deactivate() override;
        void focus() override;
        void redraw() override;
        void tick(bool active) override;

        /** @} */

    private:
        /** @name BufferedLogWriter + ShellWriter interface
         *  @{ */

        // One implementation serves both base interfaces.
        void   push(const LogEntry_t* entry) override;
        size_t format(const LogEntry_t* entry, char* buf, size_t len) override;
        size_t write(const char* buf, size_t len) override;
        void   write_lines(const char* buf, uint16_t len) override;
        void   drain() override;
        void   clear() override;

        /** @} */

        /** @name Internals
         *  @{ */

        /** @brief Configure the editor's prompts and geometry. */
        void _setup_editor();
        /** @brief Repaint the whole page (header, log area, editor). */
        void _full_redraw();

        /** @brief Submit the current editor line: snapshot it, echo it into the
         *         scrollback, advance to a fresh prompt, then dispatch to the shell.
         *         Assumes the caller has set suppress_redraw(true); clears it here. */
        void _submit_command();

        /** @brief Cancel the current editor line (Ctrl-C): echo it into the scrollback
         *         and advance to a fresh prompt exactly like a submit, but run nothing.
         *         Assumes the caller has set suppress_redraw(true); clears it here. */
        void _cancel_command();

        /** @brief Store each display row of a submitted command in the scrollback buffer.
         *         Does not touch the screen - the content is already visible. */
        void _record_command(const char* cmd, uint16_t len);

        /** @brief Scroll the log area down by delta rows via IL, then fill the newly
         *         revealed top rows from the scrollback buffer.
         *         Used when the editor shrinks, to avoid a full log redraw. */
        void _reveal_log_rows(uint16_t delta);

        /** @} */
    };

}}

#endif // PAGES_SHELL_H
