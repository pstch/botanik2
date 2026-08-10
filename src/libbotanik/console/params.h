#ifndef CONSOLE_PARAMS_H
#define CONSOLE_PARAMS_H

/** @file console/params.h
 *  @brief The console's own configuration rows, for a site's variable table.
 *
 *  Which terminal is on the wire and how big it is used to be three compile-time
 *  constants, which meant swapping a VT102 for a Minitel - or a 80x24 for a
 *  132x32 - was a firmware build. `BotanikFirmware` now reads them BY NAME (the
 *  `term` group), so the library declares them rather than leaving every site to
 *  spell them correctly. Same rule as the net_ / ntp_ / mqtt_ / mb_ rows.
 *
 *  The `Config::` values behind the defaults stay: they are what a site pins for
 *  its installation, and what a wiped store comes back at.
 *
 *  @section term_commit Why a commit row
 *
 *  `term_serial` is the group's commit serial, as elsewhere. It earns its place
 *  here more than most: the three rows describe ONE physical terminal, and
 *  applying them one at a time walks the screen through geometries that match
 *  nothing on the wire - a 40-column Minitel briefly driven as 80-column VT102
 *  is a garbled repaint per edit. Staged, the swap lands in one.
 *
 *  @warning These are persisted, so a geometry the terminal does not have
 *  survives a reboot and there is no resize handshake to correct it. Same class
 *  of footgun as `serial_console_baud`, which can lock the console out
 *  entirely; the bands below are the whole guard.
 *
 *  Registers 960-963, inside the library's 900-999 reserve: clear of the mb_
 *  rows below them and of the alarms at its top.
 */

#include "libbotanik/config/terminal.h" // the rows' defaults and their bands
#include "libbotanik/vars/declare.h"    // the P() factory these rows are written with

/** @brief The console's configuration rows. One group of BOTANIK_LIBRARY_VARS.
 *
 *  `term_cols` is banded at EDITOR_MIN_COLS, which is the width the editor's
 *  layout static_asserts already assume: the band enforces at runtime what they
 *  enforce at build time. `term_rows` is banded at 12 because the deepest layout
 *  offset in the tree is the ALARMS page's `rows() - 8`, and a floor below that
 *  has the subtraction wrap.
 *
 *  Fully qualified, because a site table may be in a namespace that has not
 *  pulled `Botanik::Vars` in; each row carries its own trailing comma, so an
 *  empty group vanishes cleanly. */
#define BOTANIK_CONSOLE_PARAM_VARS                                             \
    /* term_: BotanikFirmware reads these by name, so the library declares them */ \
    ::Botanik::Vars::P("term_serial", ::Botanik::Vars::T_U32,                  \
        1, 0, 9999, 0, "", 960,                                                \
        "bump to apply staged term_ edits", ""),                               \
    ::Botanik::Vars::P("term_type", ::Botanik::Vars::T_U8,                     \
        BOTANIK_TERMINAL_TYPE, 0, 3, 0, "", 961,                               \
        "which terminal is on the console",                                    \
        "0=VT100 1=VT102 2=Minitel 1B 3=Minitel 2"),                           \
    ::Botanik::Vars::P("term_cols", ::Botanik::Vars::T_U16,                    \
        BOTANIK_CONSOLE_COLS, BOTANIK_EDITOR_MIN_COLS,                         \
        BOTANIK_TERMINAL_MAX_COLS, 0, "col", 962,                              \
        "console width, in columns", "the screen buffer bounds the maximum"),  \
    ::Botanik::Vars::P("term_rows", ::Botanik::Vars::T_U16,                    \
        BOTANIK_CONSOLE_ROWS, 12, BOTANIK_TERMINAL_MAX_ROWS, 0, "row", 963,    \
        "console height, in rows", "the screen buffer bounds the maximum"),

#endif // CONSOLE_PARAMS_H
