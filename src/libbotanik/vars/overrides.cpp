/**
 * @file overrides.cpp
 * @brief The override layer: forcing a row, releasing it, and the stored copy.
 *
 * An override shadows the live value for every reader. It is the one write that
 * outlives a reboot without being a parameter, so each of these has a storage
 * side as well as a slot side.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

#include "libbotanik/logs/logger.h"
#include "libbotanik/vars/edit.h"

namespace Botanik { namespace Vars {

    uint16_t Bank::unsaved_override_count() const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++)
            if ((_slots[i].vflags & VF_OVR_UNSAVED) != 0) n++;
        return n;
    }

    SetResult Bank::override_set(Var h, const char* text) {
        const Descriptor* d = h.meta();
        // A T_STR's live text lives in the pool, not the slot, so there is
        // nothing for an override to shadow.
        if (text == nullptr || d->type == T_STR) return SET_INVALID;
        // An output is deliberately NOT refused while control is disabled: with
        // the programs stopped, an override is the only way left to command an
        // actuator.
        Scalar parsed;
        // Band deliberately NOT applied - see the header note.
        if (parse_for(*d, text, /*bounded=*/false, &parsed) != SET_OK) return SET_INVALID;
        override_set(h, parsed);
        return SET_OK;
    }

    void Bank::override_set(Var h, Scalar v) {
        h.override_set(v);
        _mark_override_unsaved(h);
        // A forced value is BELIEVED. Forcing a dead probe's reading so the
        // plant runs on it is what an override is for, and inheriting the doubt
        // underneath would make that impossible. Written into the COMPANION
        // because that is the one store - a rule applied at read time would be
        // an answer only C++ could see, ST reading the row itself.
        // `overrides set <name> <value> invalid` forces the companion after
        // this and so wins, which is the operator saying no.
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        const VarRef q = quality_ref(VarRef{ row });
        if (q.index == VAR_NONE) return;
        Var qh = var(q);
        qh.override_set(Scalar::U((uint64_t)Q_VALID));
        _mark_override_unsaved(qh);
    }

    SetResult Bank::save_override(Var h) {
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE || !_slots[row].ovr_on) return SET_INVALID;
        if (!_persist_override(h)) return SET_NOT_PERSISTED;
        _slots[row].vflags = (uint8_t)(_slots[row].vflags & ~VF_OVR_UNSAVED);
        // The belief that came with the force goes with it: one act taken, one
        // act made durable. The companion recurses no further - it has none.
        const VarRef q = quality_ref(VarRef{ row });
        if (q.index != VAR_NONE && _slots[q.index].ovr_on) (void)save_override(var(q));
        return SET_OK;
    }

    uint16_t Bank::save_overrides(uint16_t* refused) {
        uint16_t saved = 0, bad = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if ((_slots[i].vflags & VF_OVR_UNSAVED) == 0) continue;
            if (save_override(var(i)) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    void Bank::_mark_override_unsaved(Var h) {
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        _slots[row].vflags = (uint8_t)(_slots[row].vflags | VF_OVR_UNSAVED);
    }

    void Bank::override_clear(Var h) {
        h.override_clear();
        const uint16_t row = _row_of(h);
        if (row == VAR_NONE) return;
        // ...and the belief that came with it, or a released reading would go
        // on being believed by whatever put it there.
        const VarRef q = quality_ref(VarRef{ row });
        if (q.index != VAR_NONE && _slots[q.index].ovr_on) override_clear(var(q));
        _slots[row].vflags = (uint8_t)(_slots[row].vflags & ~VF_OVR_UNSAVED);
        // Forget the stored copy too, or the override comes back at the next
        // boot. Unconditional: an override cleared before it was ever saved may
        // still have an older durable copy underneath it.
        if (_store != nullptr) (void)_store->clear_override(*h.meta());
    }

    bool Bank::_persist_override(Var h) {
        const uint16_t row = _row_of(h);
        if (_store == nullptr || row == VAR_NONE) return false;
        const Descriptor* d = h.meta();
        if (!_store->save_override(*d, _slots[row])) {
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "override not persisted\n'%s'", d->name);
            return false;
        }
        return true;
    }

    uint16_t Bank::load_overrides() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            const Descriptor& d = descriptor(i);
            if (!_store->load_override(d, _slots[i])) continue;
            _slots[i].ovr_on = true;
            // It came OUT of the store, so it is durable by definition.
            _slots[i].vflags = (uint8_t)(_slots[i].vflags & ~VF_OVR_UNSAVED);
            if (d.role == ROLE_OUTPUT) _slots[i].vflags |= VF_FIELD_DIRTY;
            n++;
            // CRITICAL, not a warning: an override explains behaviour nothing
            // else accounts for, and after a reboot nobody remembers setting it.
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "override restored\non '%s'", d.name);
        }
        return n;
    }

    uint16_t Bank::override_clear_all() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; i++) {
            if (!_slots[i].ovr_on) continue;
            _slots[i].ovr_on = false;
            _slots[i].vflags = (uint8_t)(_slots[i].vflags & ~VF_OVR_UNSAVED);
            if (_store != nullptr) (void)_store->clear_override(descriptor(i));
            // The field owes the programs' demand again; a release never written
            // out would leave the actuator where the maintainer left it.
            if (descriptor(i).role == ROLE_OUTPUT)
                _slots[i].vflags |= VF_FIELD_DIRTY;
            n++;
        }
        return n;
    }
}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
