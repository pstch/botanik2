/**
 * @file resolve.cpp
 * @brief Bring-up: registering the tables, redeclaration, and name lookup.
 *
 * Everything here runs once, at setup, and nothing here runs in a beat. The
 * name index is what buys that: a program resolves by name at bind time and
 * holds an index for the life of the run.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

#include <cstring>

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/params.h"   // analog_values - the calibration is parameters

namespace Botanik { namespace Vars {

    bool Bank::register_vars(const Descriptor* rows, uint16_t count) {
        if (rows == nullptr || count == 0) return true;   // nothing to add
        // All or nothing: a half-added segment would leave the rows past the
        // ceiling silently absent while the ones before them worked, which is
        // the shape of bug that takes a week to find.
        if (_segments >= Config::VAR_SEGMENTS ||
            (uint32_t)_count + count > Config::VAR_MAX) {
            // Loud here, counted for POST there: every name in the lost segment
            // resolves to the sink, so the subsystem that owns them looks
            // present and does nothing.
            BOTANIK_ASSERT(!"variable table full: raise VAR_MAX / VAR_SEGMENTS");
            _refused = (uint16_t)(_refused + count);
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "%u vars refused ('%s'...): raise VAR_MAX",
                                  (unsigned)count, rows[0].name);
            return false;
        }
        _seg[_segments]      = rows;
        _seg_base[_segments] = _count;
        _seg_len[_segments]  = count;
        _segments++;
        // Identity HERE, not at freeze(): descriptor() goes through _row from the
        // moment a row exists, and a bank that is read before it is frozen (or
        // never frozen at all, as several tests do) would otherwise index the
        // segments through uninitialised memory.
        for (uint16_t i = _count; i < _count + count; i++) _row[i] = i;
        _count = (uint16_t)(_count + count);
        return true;
    }

    void Bank::_resolve_shadows() {
        // _row is still identity from register_vars(), so descriptor() reads
        // physically here and the sort below can use the ordinary accessors.
        _build_index();       // by name, so a redeclared pair lands adjacent

        uint16_t i = 0;
        while (i < _count) {
            uint16_t j = (uint16_t)(i + 1);
            while (j < _count &&
                   std::strcmp(descriptor(_index[i]).name, descriptor(_index[j]).name) == 0) j++;
            if (j > i + 1) {
                // LAST wins: segments register library-then-site, so the site's
                // restatement is the one that survives.
                uint16_t win = _index[i];
                for (uint16_t k = (uint16_t)(i + 1); k < j; k++)
                    if (_index[k] > win) win = _index[k];
                for (uint16_t k = i; k < j; k++) {
                    if (_index[k] == win) continue;
                    _row[_index[k]] = VAR_NONE;         // tombstone
                    _shadowed++;
                    if (_logger != nullptr)
                        _logger->notice(Logs::BOOT, "var '%s' redeclared",
                                        descriptor(win).name);
                }
            }
            i = j;
        }
        if (_shadowed == 0) return;

        // Compact in place: k never outruns p, so this cannot clobber.
        uint16_t k = 0;
        for (uint16_t p = 0; p < _count; p++)
            if (_row[p] != VAR_NONE) _row[k++] = p;
        _count = k;
    }

    void Bank::_resolve_cals() {
        for (uint16_t i = 0; i < _count; i++) {
            const AnalogCal* c = analog_cal(descriptor(i));
            if (c == nullptr || analog_refs(c) != nullptr) continue;   // none, or seen
            if (_cal_count >= Config::ANALOG_BLOCKS_MAX) {
                // The tail would fall back to a name lookup per read, which is
                // slow rather than wrong - but silently so.
                BOTANIK_ASSERT(!"more AnalogCal blocks than ANALOG_BLOCKS_MAX");
                if (_logger != nullptr)
                    _logger->critical(Logs::BOOT,
                                      "analog blocks past %u: raise ANALOG_BLOCKS_MAX",
                                      (unsigned)Config::ANALOG_BLOCKS_MAX);
                return;
            }
            CalRefs& e = _cal[_cal_count++];
            e.block = c;
            const Tunable* t[6] = { &c->raw_min, &c->raw_max, &c->eng_min,
                                    &c->eng_max, &c->min,     &c->max };
            for (uint8_t k = 0; k < 6; k++)
                e.ref[k] = (t[k]->param != nullptr) ? find(t[k]->param)
                                                    : VarRef{ VAR_NONE };
        }
    }

    bool quality_base(const char* name, char* base, size_t cap) {
        static const char SUFFIX[] = "_qlt";
        static const size_t LEN = sizeof(SUFFIX) - 1;
        if (name == nullptr) return false;
        const size_t n = strlen(name);
        if (n <= LEN || strcmp(name + n - LEN, SUFFIX) != 0) return false;
        if (n - LEN >= cap) return false;
        memcpy(base, name, n - LEN);
        base[n - LEN] = '\0';
        return true;
    }

    void Bank::_resolve_quality() {
        char base[Config::VAR_NAME_MAX];
        for (uint16_t i = 0; i < _count; i++) _slots[i].qlt_row = VAR_NONE;
        for (uint16_t i = 0; i < _count; i++) {
            if (!quality_base(descriptor(i).name, base, sizeof base)) continue;
            const uint16_t row = _lookup(base, 0);
            if (row == VAR_NONE) {
                // The companion is generated FROM the row, so this is a table
                // that was hand-edited: the row it speaks for is gone and
                // nothing reads the verdict any more.
                if (_logger != nullptr)
                    _logger->error(Logs::BOOT, "quality '%s' has no row '%s'",
                                   descriptor(i).name, base);
                continue;
            }
            _slots[row].qlt_row = i;
        }
    }

    void Bank::freeze() {
        // BEFORE init(): a shadowed row must not be given a pool slot or a value
        // slot it will never be read through.
        _resolve_shadows();
        init();               // pool layout, then every slot from its default
        _find_param_block();
        _build_index();
        _resolve_cals();      // needs the index: the tunables are found by name
        _resolve_quality();   // needs it too - a companion is paired by name
        // The ROLE_PARAM rows must be ONE run - that is what lets the PARAMS
        // page and the shell address them 0-based. Registration order decides
        // it, so this is where it can be checked. Naming the row is the whole
        // point: "parameters are not contiguous" sends nobody anywhere.
        for (uint16_t i = (uint16_t)(_param_first + _param_count); i < _count; i++) {
            if (descriptor(i).role != ROLE_PARAM) continue;
            BOTANIK_ASSERT(!"a parameter outside the parameter block");
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "param '%s' is outside the parameter block\n"
                                  "register the parameter segment first",
                                  descriptor(i).name);
            _param_stray++;
        }
    }

    bool Bank::_order_before(uint16_t a, uint16_t b) const {
        const int c = Utils::compare_bounded(descriptor(a).name, descriptor(b).name, 0);
        return (c != 0) ? (c < 0) : (a < b);
    }

    void Bank::_build_index() {
        _indexed = false;
        if (_segments == 0 || _count == 0) return;

        for (uint16_t i = 0; i < _count; i++) _index[i] = i;
        // Binary insertion sort: the strcmp is the expensive part and this does
        // n log n of them (~2000 for a 260-row site), the shuffling being
        // uint16_t moves. Once, at construction; nothing invalidates it.
        for (uint16_t i = 1; i < _count; i++) {
            const uint16_t row = _index[i];
            uint16_t lo = 0, hi = i;
            while (lo < hi) {
                const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
                if (_order_before(row, _index[mid])) hi = mid;
                else                                   lo = (uint16_t)(mid + 1);
            }
            for (uint16_t k = i; k > lo; k--) _index[k] = _index[k - 1];
            _index[lo] = row;
        }
        _indexed = true;
    }

    uint16_t Bank::_lookup(const char* name, uint16_t len) const {
        if (!_indexed) {
            for (uint16_t i = 0; i < _count; i++)
                if (Utils::compare_bounded(descriptor(i).name, name, len) == 0) return i;
            return VAR_NONE;
        }
        // Lower bound, then one equality check: with ties ordered by row number,
        // the first match is the lowest row carrying that name.
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (Utils::compare_bounded(descriptor(_index[mid]).name, name, len) < 0)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= _count) return VAR_NONE;
        const uint16_t row = _index[lo];
        return (Utils::compare_bounded(descriptor(row).name, name, len) == 0) ? row : VAR_NONE;
    }

    VarRef Bank::find(const char* name) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr) return r;
        r.index = _lookup(name, 0);
        if (r.index != VAR_NONE) return r;
        // A miss is a name a table or a program uses but vars.h does not
        // declare. Count it and say which, or it reads as a dead variable.
        if (_unresolved < 0xFFFFu) _unresolved++;
        if (_logger != nullptr) _logger->error(Logs::BOOT, "unknown variable\n'%s'", name);
        return r;
    }

    VarRef Bank::find(const char* name, uint16_t len) const {
        VarRef r = { VAR_NONE };
        if (name == nullptr || len == 0) return r;
        r.index = _lookup(name, len);
        return r;
    }
}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
