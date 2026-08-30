/**
 * @file st_runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 *
 * The only translation unit that compiles matiec's output, because the shim
 * has to name the bank before `accessor.h` is read.
 */

#include "st_runtime.h"

/* Named by every generated read, write and resolve. One bank, set before
 * anything generated runs. */
static Botanik::Vars::Bank* g_st_io = 0;
#define BOTANIK_ST_IO (*g_st_io)

#include "resolve_ci.h"
/* slow.c pulls accessor.h, POUS.h and POUS.c itself - matiec compiles one
 * resource per TU and expects exactly that. */
#include "generated/slow.c"

namespace Site {

    using namespace Botanik;

    void StRuntime::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void StRuntime::wire() {
        g_st_io = &_io;
        _dt_ref = _io.find("sys_dt_ms");

        // One window for the whole plant: config_init__() reaches every POU's
        // binding, and ST has no per-program bracket to open a narrower one in.
        const uint16_t missed = _io.unresolved_count();
        _io.begin_writer(Vars::WRITER_ST);
        SLOW_init__();
        _io.end_writer();
        _wired = (_io.unresolved_count() == missed);
    }

    void StRuntime::register_tasks() {
        _tasks->add(*this, &StRuntime::_run_slow, "st.slow",
                    Tasks::GROUP_SLOW, Tasks::PHASE_CONTROL);
    }

    void StRuntime::_run_slow(Component* self, const Tasks::Tick& t) {
        static_cast<StRuntime*>(self)->scan_slow(t.dt_ms);
    }

    void StRuntime::scan_slow(uint32_t dt_ms) {
        if (!_wired) return;
        // This GROUP's elapsed time, published before any body reads it. The
        // row is written by whichever group is about to run, so last-writer-
        // wins is the contract here rather than a smell.
        if (_dt_ref.index != Vars::VAR_NONE)
            _io.var(_dt_ref).set_i((int64_t)dt_ms);
        _io.begin_writer(Vars::WRITER_ST);
        SLOW_run__(0);
        _io.end_writer();
    }

}  // namespace Site
