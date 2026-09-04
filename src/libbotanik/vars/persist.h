#ifndef VARS_PERSIST_H
#define VARS_PERSIST_H

/**
 * @file persist.h
 * @brief Persist - the component that makes a row's value survive a reboot.
 *
 * The bank is RAM-only. Every verb with a storage side is here, and each does
 * its RAM half through the Var inside it, so applying and committing stay
 * separate steps: an edit is LIVE at once and DURABLE when saved, which is what
 * makes a setting that breaks its own console recoverable by a power cycle.
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief Where a durable copy lives: one key, one width. Narrow rather
     *  than the 8-byte Scalar, because the width is part of the encoding -
     *  storing eight bytes for a u8 changes every key and HAL::Storage refuses
     *  the oversized read, reverting the whole configuration to factory.
     *
     *  An interface so a test can stub it; one implementation. */
    struct VarStore {
        virtual ~VarStore() {}
        /** @return false when the key is absent, which is normal. */
        virtual bool load(const char* key, void* out, uint16_t width,
                          uint16_t* got) = 0;
        virtual bool save(const char* key, const void* in, uint16_t width) = 0;
        virtual bool remove(const char* key) = 0;
    };

    /** @brief VarStore over HAL::Storage: one key per row, named for it.
     *
     *  Per key rather than one blob, so adding, removing or reordering a row
     *  cannot invalidate the others: there is no layout to version. */
    class KvStore : public VarStore {
    public:
        bool load(const char* key, void* out, uint16_t width, uint16_t* got) override;
        bool save(const char* key, const void* in, uint16_t width) override;
        bool remove(const char* key) override;
    };


    /** @brief The durable half of the variable model (see file header). */
    class Persist : public Botanik::Component {

        /** @name Wiring
         *  @{ */

        Bank&     _io;
        VarStore* _store;
        /** @brief Last retained sweep; 0 = none yet. */
        uint64_t  _last_retain_ms = 0;

        /** @} */

    public:
        Persist(const SetupContext& ctx, Bank& io, VarStore* store = nullptr)
            : Component(ctx, "persist"), _io(io), _store(store) {}

        /** @brief Bind the store. Wired once at bring-up, before on_setup(). */
        void set_store(VarStore* s) { _store = s; }

        void register_tasks() override;

        /** @name What goes to the retained store
         *  Two halves of one flag, told apart by kind. @{ */

        /** @brief F_RETAIN on a status row: its value IS the history. A
         *  parameter already persists, an input is overwritten by the next scan,
         *  and restoring an output would energise a contactor before anything
         *  looked at the plant. */
        static bool retainable(const Descriptor& d) {
            return (d.flags & F_RETAIN) != 0 && d.kind == K_STATUS;
        }
        /** @brief The flag's other half: F_RETAIN on a parameter, which persists
         *  through its own key already, so the flag means `params reset` leaves
         *  it alone. */
        static bool param_retained(const Descriptor& d) {
            return (d.flags & F_RETAIN) != 0 && d.kind == K_PARAM;
        }

        /** @} */
        /** @name Parameters
         *  @{ */

        /** @brief Commit @p h's value and clear its unsaved and owed marks;
         *  harmless on a row that owed nothing. */
        SetResult save(Var h);
        /** @brief Commit every parameter still outstanding, owed either way -
         *  what `params save` with no name means.
         *  @return how many were written; @p refused takes the count the store
         *  rejected. */
        uint16_t save_all(uint16_t* refused = nullptr);
        /** @brief Commit the parameters a PROGRAM owes, leaving an operator's
         *  unsaved edits alone. What the retain cadence calls. */
        uint16_t save_owed(uint16_t* refused = nullptr);

        /** @brief Put @p h back to its factory default, live and durably: the
         *  stored copy is dropped, so a reboot cannot undo it. A RETAINED
         *  parameter is refused. */
        SetResult reset(Var h);
        /** @brief Forget @p h's UNSAVED edit: the row goes back to what a reboot
         *  would give it. The STORED copy is left alone - the whole difference
         *  from reset(). */
        SetResult revert(Var h);

        /** @} */
        /** @name Overrides
         *  @{ */

        /** @brief Commit @p h's override so it survives a reboot, and the
         *  companion's belief with it: one act taken, one act made durable. */
        SetResult save_override(Var h);
        uint16_t  save_overrides(uint16_t* refused = nullptr);
        /** @brief Release @p h's override and forget the stored copy in the same
         *  call - left behind, it would come back at the next boot. */
        void      override_clear(Var h);
        uint16_t  override_clear_all();

        /** @} */
        /** @name Retained values
         *  @{ */

        /** @brief Write every retained row out. On a cadence and on the way
         *  down - never per write, which would be a flash erase per beat. */
        uint16_t save_retained();
        /** @brief Retained rows and owed parameters, NOW. What health calls on
         *  the way down; the cadence bounds what an unexpected cut costs, and
         *  this is for a stop that is not unexpected. */
        uint16_t flush(uint64_t now_ms);

        /** @} */

    protected:
        /** @brief Load the stored image, in this ORDER: parameters, then the
         *  overrides that shadow them, then the retained totals. */
        void on_setup(uint64_t now_ms) override;
        /** @brief The retain cadence, from `sys_retain_sec`. */
        void on_config(uint64_t now_ms) override;

    private:
        static void _task(Component* c, const Tasks::Tick& t);

        /** @brief The two save verbs' one loop. @p owed_only leaves an
         *  operator's unsaved edit alone. */
        uint16_t _save_params(bool owed_only, uint16_t* refused);

        void     _load_params();
        uint16_t _load_overrides();
        uint16_t _load_retained();

        /** @brief Read one row's stored value into @p out. */
        bool _get(const Descriptor& d, const char* prefix, Scalar* out) const;
        bool _put(const Descriptor& d, const char* prefix, Scalar v) const;
        bool _drop(const Descriptor& d, const char* prefix) const;
        /** @brief Write @p h's value under its own key, loudly when the store
         *  refuses: the value is live, so nothing looks wrong until the reboot
         *  that quietly reverts it. */
        SetResult _persist_param(Var h);
        /** @brief Retain cadence in ms; 0 = only on the way down and on `save`. */
        uint32_t _retain_ms() const;
    };

}}  // namespace Botanik::Vars

#endif // VARS_PERSIST_H
