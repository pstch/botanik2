/**
 * @file quality_mirror.h
 * @brief Publishes a FIELD row's quality into its `_qlt` companion.
 */

#ifndef BOTANIK_VARS_QUALITY_MIRROR_H
#define BOTANIK_VARS_QUALITY_MIRROR_H

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief Keeps every `X_qlt` companion of a TRANSPORT-fed row in step
     *  with that row's validity.
     *
     *  Quality is a property of a reading, and a driver is what knows it. A
     *  C++ law asks the row; ST has no way to ask anything, so the answer has
     *  to BE a row. This is what puts it there.
     *
     *  Only rows a transport fills are mirrored. A DERIVED row's companion is
     *  a verdict some program computed and must keep writing itself - there is
     *  no quality to copy, because nothing measured it.
     */
    class QualityMirror final : public Component {

        Bank& _io;
        /** @brief The pairs, resolved once at bring-up. Sized by the field
         *  rows a site chose to expose, not by the table. */
        struct Pair { VarRef row; VarRef qlt; };
        Pair     _pairs[Config::QUALITY_MIRROR_MAX];
        uint16_t _count = 0;
        uint16_t _refused = 0;   ///< companions past the limit, for the POST

    public:
        QualityMirror(const SetupContext& ctx, Bank& io)
            : Component(ctx, "qmirror"), _io(io) {}

        /** @brief Companions that did not fit. Non-zero is a site to fix: the
         *  laws reading them would run on a stuck FALSE. */
        uint16_t refused() const { return _refused; }
        uint16_t pairs() const   { return _count; }

        /** @brief Walk the table once and pair each `X_qlt` with its `X`. */
        void wire();

        void register_tasks() override;

        /** @brief Copy every pair once. The beat calls this through the
         *  registry; a test calls it directly. */
        void scan();

    private:
        static void _run(Component* self, const Tasks::Tick& t);
    };

}}  // namespace Botanik::Vars

#endif
