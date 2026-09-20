/**
 * @file persist.cpp
 * @brief Persist: the storage side of parameters, overrides and retained rows.
 *
 * The narrow value bytes under the row's own name, in three key namespaces. A
 * row may carry a setting, an override and a retained total at once, so each
 * lives behind a prefix no identifier can spell.
 */

#include "libbotanik/vars/persist.h"
#include "libbotanik_features.h"


#include <cstdio>
#include <cstring>

#include "libbotanik/config/scheduler.h"
#include "libbotanik/hal/storage.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/edit.h"
#include "libbotanik/vars/rows.h"

namespace Botanik { namespace Vars {

    namespace {
        const char* const K_OVR = "ovr.";
        const char* const K_RET = "ret.";
        const char* const K_SET = "";

        /** @brief A stored word back into a Scalar of @p t's type. The one copy
         *  of the width-dependent sign extension: get it wrong and a stored
         *  negative total returns as a very large positive one. */
        Scalar decode_scalar(VarType t, uint64_t raw, uint8_t w) {
            if (type_real(t)) { float f; memcpy(&f, &raw, 4); return Scalar::F(f); }
            if (type_signed(t)) {
                const int64_t x = (int64_t)raw;
                const unsigned shift = (unsigned)(64 - 8 * w);
                return Scalar::I((int64_t)((x << shift) >> shift));
            }
            return Scalar::U(raw);
        }

        /** @brief The inverse: a float goes out as its four bytes, everything
         *  else as the union's unsigned arm. */
        uint64_t encode_scalar(VarType t, const Scalar& s) {
            uint64_t raw = 0;
            if (type_real(t)) { const float f = s.f; memcpy(&raw, &f, 4); }
            else              { raw = s.u; }
            return raw;
        }

        bool key_for(const Descriptor& d, const char* prefix,
                     char* out, size_t cap) {
            if (d.name == nullptr) return false;
            const int n = snprintf(out, cap, "%s%s", prefix, d.name);
            return n > 0 && (size_t)n < cap;
        }
    }  // namespace

    // KvStore

    bool KvStore::load(const char* key, void* out, uint16_t width, uint16_t* got) {
        size_t n = 0;
        if (!HAL::Storage::get(key, out, width, &n)) return false;
        if (got != nullptr) *got = (uint16_t)n;
        return true;
    }

    bool KvStore::save(const char* key, const void* in, uint16_t width) {
        return HAL::Storage::set(key, in, width);
    }

    bool KvStore::remove(const char* key) { return HAL::Storage::remove(key); }

    // One row, one key

    bool Persist::_get(const Descriptor& d, const char* prefix, Scalar* out) const {
        char key[HAL::Storage::KEY_MAX];
        const uint8_t w = type_width(d.type);
        if (_store == nullptr || w == 0 || !key_for(d, prefix, key, sizeof key))
            return false;
        uint64_t raw = 0;
        uint16_t got = 0;
        // A value of the wrong width is refused rather than padded: it was
        // written by a firmware whose encoding differed, and forgetting it is
        // the safe direction.
        if (!_store->load(key, &raw, w, &got) || got != w) return false;
        *out = decode_scalar(d.type, raw, w);
        return true;
    }

    bool Persist::_put(const Descriptor& d, const char* prefix, Scalar v) const {
        char key[HAL::Storage::KEY_MAX];
        const uint8_t w = type_width(d.type);
        if (_store == nullptr || w == 0 || !key_for(d, prefix, key, sizeof key))
            return false;
        uint64_t raw = encode_scalar(d.type, v);
        return _store->save(key, &raw, w);
    }

    bool Persist::_drop(const Descriptor& d, const char* prefix) const {
        char key[HAL::Storage::KEY_MAX];
        if (_store == nullptr || !key_for(d, prefix, key, sizeof key)) return false;
        return _store->remove(key);
    }

    // Parameters

    void Persist::_load_params() {
        if (_store == nullptr) return;
        for (uint16_t k = 0; k < _io.param_count(); k++) {
            const Var h = _io.param_at(k);
            const Descriptor& d = h.meta();
            uint16_t cap = 0;
            char* txt = h.text(&cap);
            if (txt != nullptr) {
                uint16_t got = 0;
                if (_store->load(d.name, txt, cap, &got))
                    txt[got < cap ? got : (uint16_t)(cap - 1)] = 0;
            } else {
                Scalar v;
                // A missing key is normal: the parameter keeps the declared
                // default that Bank::init() already seeded.
                if (_get(d, K_SET, &v)) _io.slot(h.row()).v = v;
            }
            // Whatever the row now holds came from the store (or is the seeded
            // default), so nothing is owed a write.
            h.param_saved();
        }
    }

    SetResult Persist::_persist_param(Var h) {
        if (_store == nullptr) return SET_NOT_PERSISTED;
        const Descriptor& d = h.meta();
        uint16_t cap = 0;
        char* txt = h.text(&cap);
        const bool ok = (txt != nullptr) ? _store->save(d.name, txt, cap)
                                         : _put(d, K_SET, h.live().v);
        if (ok) return SET_OK;
        // Loud: the value is live, so nothing looks wrong until the reboot that
        // quietly reverts it.
        if (_logger != nullptr)
            _logger->critical(Logs::STORAGE, "param not persisted\n'%s'", d.name);
        return SET_NOT_PERSISTED;
    }

    SetResult Persist::save(Var h) {
        if (h.kind() != K_PARAM) return SET_INVALID;
        const SetResult r = _persist_param(h);
        // Only a write that landed clears the marks: a refused row stays owed,
        // so the next `params save` tries it again.
        if (r == SET_OK) h.param_saved();
        return r;
    }

    uint16_t Persist::_save_params(bool owed_only, uint16_t* refused) {
        uint16_t saved = 0, bad = 0;
        for (uint16_t k = 0; k < _io.param_count(); k++) {
            const Var h = _io.param_at(k);
            if (!h.param_owed() && (owed_only || !h.param_unsaved())) continue;
            if (save(h) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    // BOTH marks: `params save` is an operator asking for everything
    // outstanding to land, and a law's write is outstanding too.
    uint16_t Persist::save_all(uint16_t* refused) {
        return _save_params(false, refused);
    }

    // OWED only. An operator's unsaved edit is theirs to commit: the cadence
    // committing it would take away the one thing that makes a setting able to
    // break its own console recoverable by rebooting.
    uint16_t Persist::save_owed(uint16_t* refused) {
        return _save_params(true, refused);
    }

    SetResult Persist::reset(Var h) {
        const Descriptor& d = h.meta();
        if (!writable(d) || retained_param(d)) return SET_INVALID;
        uint16_t cap = 0;
        char* txt = h.text(&cap);
        if (txt != nullptr) copy_bounded(txt, cap, d.def.s);
        else                _io.slot(h.row()).v = d.def;
        h.param_saved();
        // The stored copy goes with the live one: left behind, it would put the
        // old value back at the next boot, which is not "back to default".
        if (_store == nullptr) return SET_NOT_PERSISTED;
        if (_drop(d, K_SET)) return SET_OK;
        if (_logger != nullptr)
            _logger->critical(Logs::STORAGE, "param not cleared\n'%s'", d.name);
        return SET_NOT_PERSISTED;
    }

    SetResult Persist::revert(Var h) {
        const Descriptor& d = h.meta();
        if (!writable(d)) return SET_INVALID;
        // Nothing owed, nothing to forget. Not an error: `clear` on a settled
        // row is the no-op an operator expects, not a complaint.
        if (!h.param_unsaved()) return SET_OK;
        uint16_t cap = 0;
        char* txt = h.text(&cap);
        if (txt != nullptr) {
            uint16_t got = 0;
            if (_store == nullptr || !_store->load(d.name, txt, cap, &got))
                copy_bounded(txt, cap, d.def.s);
            else
                txt[got < cap ? got : (uint16_t)(cap - 1)] = 0;
        } else {
            Scalar v;
            // Exactly what a reboot would give it: the stored copy, or the
            // factory default when nothing was ever saved.
            _io.slot(h.row()).v = _get(d, K_SET, &v) ? v : d.def;
        }
        h.param_saved();
        return SET_OK;
    }

    // Overrides

    uint16_t Persist::_load_overrides() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _io.count(); i++) {
            const Descriptor& d = _io.descriptor(i);
            Scalar v;
            if (!_get(d, K_OVR, &v)) continue;
            Slot& s = _io.slot(i);
            s.ovr   = v;
            // It came OUT of the store, so it is durable by definition.
            s.flags = (uint8_t)((s.flags | S_OVR_ON) & ~S_OVR_UNSAVED);
            if (d.kind == K_OUTPUT) s.flags = (uint8_t)(s.flags | S_FIELD_DIRTY);
            n++;
            // CRITICAL, not a warning: an override explains behaviour nothing
            // else accounts for, and after a reboot nobody remembers setting it.
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "override restored\non '%s'", d.name);
        }
        return n;
    }

    SetResult Persist::save_override(Var h) {
        if (h.null() || !h.override_on()) return SET_INVALID;
        // Guarded above, so value() IS the force.
        if (!_put(h.meta(), K_OVR, h.value().v)) {
            if (_logger != nullptr)
                _logger->critical(Logs::STORAGE, "override not persisted\n'%s'",
                                  h.name());
            return SET_NOT_PERSISTED;
        }
        h.override_saved();
        // The belief that came with the force goes with it: one act taken, one
        // act made durable. The companion recurses no further - it has none.
        const Var q = h.quality_var();
        if (q.override_on()) (void)save_override(q);
        return SET_OK;
    }

    uint16_t Persist::save_overrides(uint16_t* refused) {
        uint16_t saved = 0, bad = 0;
        for (uint16_t i = 0; i < _io.count(); i++) {
            const Var h = _io.var(i);
            if (!h.override_unsaved()) continue;
            if (save_override(h) == SET_OK) saved++; else bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    void Persist::override_clear(Var h) {
        if (h.null()) return;
        const Var q = h.quality_var();
        h.override_clear();          // ...and the companion's, in RAM
        // Forget the stored copies too, or the override comes back at the next
        // boot. Unconditional: one cleared before it was ever saved may still
        // have an older durable copy underneath it.
        (void)_drop(h.meta(), K_OVR);
        if (!q.null()) (void)_drop(q.meta(), K_OVR);
    }

    uint16_t Persist::override_clear_all() {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _io.count(); i++) {
            const Var h = _io.var(i);
            if (!h.override_on()) continue;
            // Through the slot, not override_clear(): a sweep is one thing the
            // operator did, and one audit line is what it deserves.
            Slot& s = _io.slot(i);
            s.flags = (uint8_t)(s.flags & ~(S_OVR_ON | S_OVR_UNSAVED));
            // The field owes the programs' demand again; a release never
            // written out would leave the actuator where the maintainer left it.
            if (h.kind() == K_OUTPUT) s.flags = (uint8_t)(s.flags | S_FIELD_DIRTY);
            (void)_drop(h.meta(), K_OVR);
            n++;
        }
        if (n != 0 && _logger != nullptr)
            _logger->notice(Logs::USER, "%u override(s) released", (unsigned)n);
        return n;
    }

    // Retained

    uint16_t Persist::_load_retained() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _io.count(); i++) {
            const Descriptor& d = _io.descriptor(i);
            if (!retained(d)) continue;
            Scalar v;
            if (!_get(d, K_RET, &v)) continue;
            // The slot directly, so the restore owes the store nothing: this
            // value IS what the store holds.
            _io.slot(i).v = v;
            // GOOD, because it is: the value a totalizer carried when the unit
            // went down is what to carry on from. Q_NONE would send every reader
            // to the default this restore exists to avoid.
            _io.var(i).set_quality(Q_GOOD);
            n++;
        }
        return n;
    }

    uint16_t Persist::save_retained() {
        if (_store == nullptr) return 0;
        uint16_t n = 0;
        for (uint16_t i = 0; i < _io.count(); i++) {
            const Descriptor& d = _io.descriptor(i);
            if (!retained(d)) continue;
            // Only what MOVED. A retained row is written by a law that restates
            // it every scan, so without the mark this is an erase per row per
            // period for a value the store already holds.
            const Var h = _io.var(i);
            if (!h.retain_owed()) continue;
            // The LIVE value, never the override: a totalizer forced to 1000 to
            // test an alarm must not enter the plant's history.
            if (!_put(d, K_RET, h.live().v)) continue;
            h.retain_saved();
            n++;
        }
        return n;
    }

    uint16_t Persist::flush(uint64_t now_ms) {
        _last_retain_ms = now_ms;
        return (uint16_t)(save_retained() + save_owed());
    }

    // Lifecycle

    uint32_t Persist::_retain_ms() const {
#if defined(BOTANIK_SITE_ROWS)
        const int32_t sec = (int32_t)_io.var(Row::sys_retain_sec).as_i64();
        return sec > 0 ? (uint32_t)sec * 1000u : 0u;
#else
        // No table: the cadence is the one sys_retain_sec is declared with.
        return Config::RETAIN_SAVE_MS;
#endif
    }

    void Persist::on_setup(uint64_t /*now_ms*/) {
        _load_params();
        // After the parameters, so a restored override shadows the setting
        // rather than being loaded over.
        const uint16_t n = _load_overrides();
        if (n != 0 && _logger != nullptr)
            _logger->critical(Logs::BOOT,
                              "%u override(s) restored from storage\n"
                              "the field is not being read or driven normally",
                              (unsigned)n);
        // Retained totals last: untouched by either step above.
        const uint16_t r = _load_retained();
        if (r != 0 && _logger != nullptr)
            _logger->notice(Logs::BOOT, "%u retained value(s) restored", (unsigned)r);
    }

    void Persist::on_config(uint64_t now_ms) {
        const uint32_t every = _retain_ms();
        // 0 turns the cadence off: the way down and `save` are then the whole
        // mechanism.
        if (every == 0) return;
        // A cadence, not a write-through: a flash erase per beat wears the part
        // out in days.
        if (_last_retain_ms != 0 && (now_ms - _last_retain_ms) < every) return;
        _last_retain_ms = now_ms;
        (void)save_retained();
        // The parameters a PROGRAM owes ride the same dial: a watering date and
        // a litre count are the same kind of fact, and neither is worth an erase
        // per beat.
        (void)save_owed();
    }

    void Persist::_task(Component* c, const Tasks::Tick& t) {
        static_cast<Persist*>(c)->on_config(t.now_ms);
    }

    void Persist::register_tasks() {
        _tasks->add(*this, &Persist::_task, "sys_persist",
                    Config::CONFIG_COMMIT_MS, Tasks::PHASE_CONFIG);
    }

}}  // namespace Botanik::Vars

