#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/terminal.h"

#include "libbotanik/panes/home.h"
#include "libbotanik/panes/shell.h"
#include "libbotanik/panes/logs.h"
#include "libbotanik/panes/status.h"


namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using namespace Botanik::Panes;
    }

    void AnsiTerminal::move_to(const CursorPos& target, bool skip) {
        if (target == _hw_pos) { return; }
        else if (target.row == _hw_pos.row) {
            if (target.col == 0) {
                Ansi::move_line_begin(_serial);
            } else if (target.col < _hw_pos.col && (_hw_pos.col - target.col) < 4) {
                Ansi::move_left(_serial, _hw_pos.col - target.col);
            } else if (target.col > _hw_pos.col && (target.col - _hw_pos.col) < 4) {
                Ansi::move_right(_serial, target.col - _hw_pos.col);
            } else {
                Ansi::goto_absolute(_serial, target);
            }
        } else if (target.col == _hw_pos.col) {
            if (target.row < _hw_pos.row && (_hw_pos.row - target.row) < 4) {
                Ansi::move_up(_serial, _hw_pos.row - target.row);
            } else if (target.row > _hw_pos.row && (target.row - _hw_pos.row) < 4) {
                Ansi::move_down(_serial, target.row - _hw_pos.row);
            } else {
                Ansi::goto_absolute(_serial, target);
            }
        } else if (target.col == 0) {
            if (target.row < _hw_pos.row && (_hw_pos.row - target.row) < 4) {
                Ansi::move_line_begin(_serial);
                Ansi::move_up(_serial, _hw_pos.row - target.row);
            } else if (target.row > _hw_pos.row && (target.row - _hw_pos.row) < 4) {
                Ansi::move_line_begin(_serial);
                Ansi::move_down(_serial, _hw_pos.row - target.row);
            } else {
                Ansi::goto_absolute(_serial, target);
            }
        } else {
            Ansi::goto_absolute(_serial, target);
        }
        _hw_pos = target;
    }
    


}}