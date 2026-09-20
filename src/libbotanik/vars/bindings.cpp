/**
 * @file vars/bindings.cpp
 * @brief ConfigBinder's own walk: the table, and the push at PHASE_CONFIG.
 *
 * The readers that know a component's rows are in firmware/bindings.cpp, which
 * a build with no variable table drops whole. This half has no rows in it, so
 * a consumer reusing a component still gets a binder that holds nothing.
 */
#include "libbotanik/vars/bindings.h"

#include "libbotanik/config/vars.gen.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    void ConfigBinder::add(uint16_t serial_row, void* target,
                           void (*apply)(void*, const Bank&, bool)) {
        if (target == nullptr || apply == nullptr) return;
        if (_len >= Config::CONFIG_BINDINGS_MAX) {
            // A dropped binding leaves its component on its CONSTRUCTED
            // defaults while the rows it should have read sit in the table
            // looking live - the parameter edits, and nothing moves.
            BOTANIK_ASSERT(!"config bindings full: raise CONFIG_BINDINGS_MAX");
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "a config binding was refused: "
                                  "raise CONFIG_BINDINGS_MAX (%u)",
                                  (unsigned)Config::CONFIG_BINDINGS_MAX);
            return;
        }
        ConfigBinding& b = _tab[_len++];
        b.serial_row = serial_row;
        b.target = target;
        b.apply = apply;
        b.applied = 0;
        b.ever = false;
    }

    void ConfigBinder::register_tasks() {
        _tasks->add(*this, &ConfigBinder::_task, "sys_config", Config::CONFIG_COMMIT_MS,
                    Tasks::PHASE_CONFIG);
    }

    void ConfigBinder::_task(Component* c, const Tasks::Tick&) {
        static_cast<ConfigBinder*>(c)->apply_all();
    }

    void ConfigBinder::apply_all() {
        if (_io == nullptr) return;
        for (uint8_t i = 0; i < _len; i++) {
            ConfigBinding& b = _tab[i];
            // An unstaged binding re-reads every beat; set_config() is required
            // to be a no-op when nothing moved, so the cost is the read.
            const bool staged = (b.serial_row != VAR_NONE);
            const uint32_t serial =
                staged ? (uint32_t)_io->var(b.serial_row).as_i64() : 0;
            if (b.ever && staged && serial == b.applied) continue;
            b.applied = serial;
            // Announcing is for a CHANGE, and the first push is not one: it
            // is the stored image arriving at bring-up.
            const bool announce = b.ever;
            b.ever = true;
            b.apply(b.target, *_io, announce);
        }
    }

}}
