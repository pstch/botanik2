/**
 * @file st_runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 *
 * The only translation unit that compiles matiec's output, because the shim
 * has to name the bank before `accessor.h` is read.
 */

#include "st_runtime.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/config/scheduler.h"
#include "libbotanik/logs/logger.h"

#include "st_bank.h"
#include "resolve_ci.h"
/* Generated C, and it says so at the top of every file. Silenced HERE rather
 * than in CMake: a per-source flag can land before a target's own options and
 * be undone by them, where a pragma cannot. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wshadow"
/* plc.c pulls accessor.h, POUS.h and POUS.c itself - matiec compiles one
 * resource per TU and expects exactly that. */
#include "generated/plc.c"
#pragma GCC diagnostic pop

namespace Botanik { namespace St { Vars::Bank* io = 0; } }

/* What st_config.cpp defines, from matiec's CONFIGURATION file. Declared
 * without extern "C": the generated files are compiled as C++ here, so the
 * mangling has to match theirs. */
void config_init__(void);
void config_run__(unsigned long tick);
extern unsigned long long common_ticktime__;
extern unsigned long greatest_tick_count__;

namespace Site {

    using namespace Botanik;

    void StRuntime::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void StRuntime::wire() {
        St::io = &_io;
        _dt_ref = _io.find("sys_now_ms");

        // One window for the whole plant: config_init__() reaches every POU's
        // binding, and ST has no per-program bracket to open a narrower one in.
        const uint16_t missed = _io.unresolved_count();
        _io.begin_writer(Vars::WRITER_ST);
        config_init__();
        _io.end_writer();
        _wired = (_io.unresolved_count() == missed);
    }

    /** @brief The rate group whose period is @p ms, for the TASKS page and the
     *  stagger. The ST's period is matiec's number, so it need not be one of
     *  ours; the nearest group at or under it is what it is staggered with. */
    static Tasks::GroupId _group_for(uint32_t ms) {
        Tasks::GroupId best = Tasks::GROUP_FAST;
        for (uint8_t g = 0; g < Tasks::GROUP_COUNT; g++) {
            const Tasks::GroupId id = (Tasks::GroupId)g;
            if (Tasks::group_period_ms(id) <= ms &&
                Tasks::group_period_ms(id) >= Tasks::group_period_ms(best))
                best = id;
        }
        return best;
    }

    void StRuntime::register_tasks() {
        // matiec's number, the GCD of the TASK intervals in plant.st. Nothing
        // here picks a rate, and nothing here assumes what it will be: adding
        // one program on a faster task moves it, and this follows.
        const uint32_t base_ms = (uint32_t)(common_ticktime__ / 1000000ULL);

        // The beat cannot release a task faster than it runs, and a period
        // that is not a multiple of it DRIFTS: the timer fires on the first
        // beat at or past each deadline, so a 25 ms task on a 10 ms beat runs
        // at 30, 20, 30, 20. Refuse rather than run it wrong - the plant then
        // never drives the field, and the log says which number to fix.
        if (base_ms < Config::TASK_PERIOD_MS ||
            base_ms % Config::TASK_PERIOD_MS != 0) {
            _wired = false;
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "plant period %ums is not a multiple "
                                  "of the %ums beat; nothing will run",
                                  (unsigned)base_ms,
                                  (unsigned)Config::TASK_PERIOD_MS);
            return;
        }
        _tasks->add_at(*this, &StRuntime::_run_plant, "st.plant",
                       _group_for(base_ms), Tasks::PHASE_CONTROL, base_ms);
    }

    void StRuntime::_run_plant(Component* self, const Tasks::Tick& t) {
        static_cast<StRuntime*>(self)->scan(t.now_ms);
    }

    void StRuntime::scan(uint64_t now_ms) {
        if (!_wired) return;
        // The CLOCK, published before any body reads it. Every block
        // differences it against its own last reading, so one row serves every
        // rate and nothing here has to know which programs are about to run.
        if (_dt_ref.index != Vars::VAR_NONE)
            _io.var(_dt_ref).set_i((int64_t)(uint32_t)now_ms);
        _io.begin_writer(Vars::WRITER_ST);
        // The tick DIVIDES: matiec compiled one `tick % N` per task from the
        // intervals, so this counter is what decides which programs run. It
        // wraps where matiec says, which is the largest multiple of every
        // divisor that fits - anywhere else and the pattern would skip.
        config_run__(_tick);
        if (++_tick >= greatest_tick_count__) _tick = 0;
        _io.end_writer();
    }

}  // namespace Site
