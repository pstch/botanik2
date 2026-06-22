# Event Dispatch Architecture

This document describes how input events flow from the hardware serial port to
the active terminal page. For the pages themselves see
[PAGES.md](PAGES.md); for the output side (log entries flowing *out* to the
pages and serial sinks) see [LOG_FLOW.md](LOG_FLOW.md).

## Layers

### BufferedSerial

The lowest layer. @ref Botanik::Utils::BufferedSerial wraps the hardware UART
with a 256-byte RX ring buffer and a 4 KB TX ring buffer. All serial reads and
writes go through this component; it never blocks - a full TX ring drops the
tail rather than stalling the loop.

### AnsiTerminal

The central coordinator. @ref Botanik::Terminal::AnsiTerminal owns the
`BufferedSerial` connection and drives all terminal I/O. Each call to
@ref Botanik::Terminal::AnsiTerminal::step "step()":

1. Bootstraps the display on the first call (clear + status row + focus the
   active page), if no `setup_*()` already did.
2. Refreshes the row-0 status bar when its interval has elapsed and the TX ring
   has room.
3. Pulls the next @ref Botanik::Terminal::InputEvent from the parser and
   dispatches it (one event per `step()`).
4. **Ticks every page** - not just the active one - so each can drain its
   queued log entries (see [LOG_FLOW.md](LOG_FLOW.md)).

@ref Botanik::Terminal::InputParser decodes raw bytes into structured
`InputEvent`s, handling X3.64 CSI sequences, Alt-prefixed keys, function-key
escape sequences, and the kitty `CSI u` form. Held BACKSPACE / Ctrl+D presses
are coalesced into one event carrying a repeat `count`.

The @ref Botanik::Terminal::Ansi helper provides a stateless, write-only API for
emitting escape sequences (cursor movement, erase, insert/delete rows, etc.).

### Pages

Each page implements the @ref Botanik::Pages::Page interface. `AnsiTerminal`
holds up to 8 page pointers and routes events to whichever page is currently
active. See [PAGES.md](PAGES.md).

## Dispatch flow

\dot "input: serial bytes -> parser -> terminal dispatch -> the active page"
digraph dispatch {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; nodesep=0.4; ranksep=0.45;
  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9, margin="0.12,0.06"];
  edge [arrowsize=0.7, fontname="Helvetica", fontsize=8];

  serial [shape=oval, class="neutral", label="hardware serial port"];

  bs [shape=plaintext, label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="5" color="#9e9e9e">
      <tr><td colspan="2" bgcolor="#37474f"><font color="#ffffff"><b>BufferedSerial</b></font></td></tr>
      <tr><td bgcolor="#d6e4f0">RX ring<br/>256 B</td><td bgcolor="#d6e4f0">TX ring<br/>4 KB</td></tr>
    </table>
  >];

  parse [class="logic", label="InputParser.parse()\n-> InputEvent"];

  content [shape=plaintext, label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
      <tr><td bgcolor="#37474f"><font color="#ffffff"><b>CONTENT event</b></font></td></tr>
      <tr><td bgcolor="#fff3cd"><font color="#555555">printable run</font></td></tr>
    </table>
  >];
  command [shape=plaintext, label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
      <tr><td bgcolor="#37474f"><font color="#ffffff"><b>COMMAND event</b></font></td></tr>
      <tr><td bgcolor="#fff3cd"><font color="#555555">key + repeat count</font></td></tr>
    </table>
  >];

  intercept [class="intercept", label="Terminal intercepts (consumed):\nCtrl+L, HOME, Alt+1..0,\nTAB / Ctrl+PgUp/PgDn, Ctrl+C"];
  htext [class="ui", label="page->handle_text_batch()"];
  hcmd  [class="ui", label="page->handle_command(cmd, count)"];
  active [class="plc", label="active page\nShellPage / LogsPage / HomePage / StatusPage"];

  editor [shape=plaintext, label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="5" color="#9e9e9e">
      <tr><td bgcolor="#37474f"><font color="#ffffff"><b>InputEditor</b> (TextEditor)</font></td></tr>
      <tr><td bgcolor="#e6dcf0" align="left">ShellPage's line editor</td></tr>
      <tr><td bgcolor="#e6dcf0" align="left"><font color="#555555">CONTENT &#8594; insert; COMMAND &#8594; move/delete</font></td></tr>
    </table>
  >];

  tickall [class="neutral", label="tick(active) on every page (active + background)"];

  serial -> bs [label="raw bytes"];
  bs -> parse [label="drained in step()"];
  parse -> content;
  parse -> command;
  command -> intercept;
  intercept -> hcmd [label="remaining"];
  content -> htext;
  { rank=same; htext; hcmd; }
  htext -> active;
  hcmd  -> active;
  active -> editor [label="ShellPage", style=dashed];
  active -> tickall;
}
\enddot

## Terminal-level intercepts

A handful of commands are consumed by
@ref Botanik::Terminal::AnsiTerminal::step "AnsiTerminal::step()" and never reach
the active page - `Ctrl+L` (full redraw), `HOME` / `Alt+1`...`Alt+0` (jump to a
page), and `TAB` / `Ctrl+PageUp`/`PageDown` (cycle pages). `Ctrl+C` is a hybrid:
the terminal offers it to the active page first via
@ref Botanik::Pages::Page::handle_cancel "handle_cancel()", and only if the page
declines does it apply the default - quit on the simulator, a **no-op on
hardware** (a stray Ctrl+C must never reboot the controller). Everything else is
forwarded to the active page. See [KEYBINDINGS.md](KEYBINDINGS.md) for the exact
bindings, including their effects.
