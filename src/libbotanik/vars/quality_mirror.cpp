/**
 * @file quality_mirror.cpp
 * @brief HAND-WRITTEN - pairing the companions, and keeping them current.
 */

#include "libbotanik/vars/quality_mirror.h"

#include <cstring>

#include "libbotanik/logs/logger.h"
#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Vars {

    static const char QLT_SUFFIX[] = "_qlt";
    static const size_t QLT_LEN = sizeof(QLT_SUFFIX) - 1;

    void QualityMirror::wire() {
        _count = _refused = 0;
        char base[64];   // a row name, less the suffix

        for (uint16_t i = 0; i < _io.count(); i++) {
            const char* name = _io.descriptor(i).name;
            const size_t n = strlen(name);
            if (n <= QLT_LEN || strcmp(name + n - QLT_LEN, QLT_SUFFIX) != 0)
                continue;
            if (n - QLT_LEN >= sizeof base) continue;
            memcpy(base, name, n - QLT_LEN);
            base[n - QLT_LEN] = '\0';

            const VarRef row = _io.find(base);
            if (row.index == VAR_NONE) continue;
            // A DERIVED row's companion is a program's verdict, not a copy of
            // anything: mirroring it would overwrite the law that owns it.
            if (_io.descriptor(row.index).bind.transport == XP_NONE) continue;

            if (_count >= Config::QUALITY_MIRROR_MAX) { _refused++; continue; }
            _pairs[_count].row = row;
            _pairs[_count].qlt = VarRef{ i };
            _count++;
        }

        if (_refused != 0 && _logger != nullptr)
            _logger->critical(Logs::BOOT,
                              "%u quality companions past QUALITY_MIRROR_MAX; "
                              "the laws reading them see a stuck false",
                              (unsigned)_refused);
    }

    void QualityMirror::register_tasks() {
        // PHASE_INPUT, and AFTER the drivers that fill the rows - registration
        // order inside a phase is dispatch order, and the composition root is
        // what knows which came first.
        _tasks->add(*this, &QualityMirror::_run, "qmirror",
                    Tasks::EVERY_BEAT, Tasks::PHASE_INPUT);
    }

    void QualityMirror::_run(Component* self, const Tasks::Tick& t) {
        (void)t;
        static_cast<QualityMirror*>(self)->scan();
    }

    void QualityMirror::scan() {
        for (uint16_t i = 0; i < _count; i++)
            _io.var(_pairs[i].qlt).set(_io.var(_pairs[i].row).valid());
    }

}}  // namespace Botanik::Vars
